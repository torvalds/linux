//===- llvm/Support/YAMLTraits.h --------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_YAMLTRAITS_H
#define LLVM_SUPPORT_YAMLTRAITS_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

namespace llvm {
namespace yaml {

enum class NodeKind : uint8_t {
  Scalar,
  Map,
  Sequence,
};

struct EmptyContext {};

/// This class should be specialized by any type that needs to be converted
/// to/from a YAML mapping.  For example:
///
///     struct MappingTraits<MyStruct> {
///       static void mapping(IO &io, MyStruct &s) {
///         io.mapRequired("name", s.name);
///         io.mapRequired("size", s.size);
///         io.mapOptional("age",  s.age);
///       }
///     };
template<class T>
struct MappingTraits {
  // Must provide:
  // static void mapping(IO &io, T &fields);
  // Optionally may provide:
  // static StringRef validate(IO &io, T &fields);
  //
  // The optional flow flag will cause generated YAML to use a flow mapping
  // (e.g. { a: 0, b: 1 }):
  // static const bool flow = true;
};

/// This class is similar to MappingTraits<T> but allows you to pass in
/// additional context for each map operation.  For example:
///
///     struct MappingContextTraits<MyStruct, MyContext> {
///       static void mapping(IO &io, MyStruct &s, MyContext &c) {
///         io.mapRequired("name", s.name);
///         io.mapRequired("size", s.size);
///         io.mapOptional("age",  s.age);
///         ++c.TimesMapped;
///       }
///     };
template <class T, class Context> struct MappingContextTraits {
  // Must provide:
  // static void mapping(IO &io, T &fields, Context &Ctx);
  // Optionally may provide:
  // static StringRef validate(IO &io, T &fields, Context &Ctx);
  //
  // The optional flow flag will cause generated YAML to use a flow mapping
  // (e.g. { a: 0, b: 1 }):
  // static const bool flow = true;
};

/// This class should be specialized by any integral type that converts
/// to/from a YAML scalar where there is a one-to-one mapping between
/// in-memory values and a string in YAML.  For example:
///
///     struct ScalarEnumerationTraits<Colors> {
///         static void enumeration(IO &io, Colors &value) {
///           io.enumCase(value, "red",   cRed);
///           io.enumCase(value, "blue",  cBlue);
///           io.enumCase(value, "green", cGreen);
///         }
///       };
template<typename T>
struct ScalarEnumerationTraits {
  // Must provide:
  // static void enumeration(IO &io, T &value);
};

/// This class should be specialized by any integer type that is a union
/// of bit values and the YAML representation is a flow sequence of
/// strings.  For example:
///
///      struct ScalarBitSetTraits<MyFlags> {
///        static void bitset(IO &io, MyFlags &value) {
///          io.bitSetCase(value, "big",   flagBig);
///          io.bitSetCase(value, "flat",  flagFlat);
///          io.bitSetCase(value, "round", flagRound);
///        }
///      };
template<typename T>
struct ScalarBitSetTraits {
  // Must provide:
  // static void bitset(IO &io, T &value);
};

/// Describe which type of quotes should be used when quoting is necessary.
/// Some non-printable characters need to be double-quoted, while some others
/// are fine with simple-quoting, and some don't need any quoting.
enum class QuotingType { None, Single, Double };

/// This class should be specialized by type that requires custom conversion
/// to/from a yaml scalar.  For example:
///
///    template<>
///    struct ScalarTraits<MyType> {
///      static void output(const MyType &val, void*, llvm::raw_ostream &out) {
///        // stream out custom formatting
///        out << llvm::format("%x", val);
///      }
///      static StringRef input(StringRef scalar, void*, MyType &value) {
///        // parse scalar and set `value`
///        // return empty string on success, or error string
///        return StringRef();
///      }
///      static QuotingType mustQuote(StringRef) { return QuotingType::Single; }
///    };
template<typename T>
struct ScalarTraits {
  // Must provide:
  //
  // Function to write the value as a string:
  // static void output(const T &value, void *ctxt, llvm::raw_ostream &out);
  //
  // Function to convert a string to a value.  Returns the empty
  // StringRef on success or an error string if string is malformed:
  // static StringRef input(StringRef scalar, void *ctxt, T &value);
  //
  // Function to determine if the value should be quoted.
  // static QuotingType mustQuote(StringRef);
};

/// This class should be specialized by type that requires custom conversion
/// to/from a YAML literal block scalar. For example:
///
///    template <>
///    struct BlockScalarTraits<MyType> {
///      static void output(const MyType &Value, void*, llvm::raw_ostream &Out)
///      {
///        // stream out custom formatting
///        Out << Value;
///      }
///      static StringRef input(StringRef Scalar, void*, MyType &Value) {
///        // parse scalar and set `value`
///        // return empty string on success, or error string
///        return StringRef();
///      }
///    };
template <typename T>
struct BlockScalarTraits {
  // Must provide:
  //
  // Function to write the value as a string:
  // static void output(const T &Value, void *ctx, llvm::raw_ostream &Out);
  //
  // Function to convert a string to a value.  Returns the empty
  // StringRef on success or an error string if string is malformed:
  // static StringRef input(StringRef Scalar, void *ctxt, T &Value);
  //
  // Optional:
  // static StringRef inputTag(T &Val, std::string Tag)
  // static void outputTag(const T &Val, raw_ostream &Out)
};

/// This class should be specialized by type that requires custom conversion
/// to/from a YAML scalar with optional tags. For example:
///
///    template <>
///    struct TaggedScalarTraits<MyType> {
///      static void output(const MyType &Value, void*, llvm::raw_ostream
///      &ScalarOut, llvm::raw_ostream &TagOut)
///      {
///        // stream out custom formatting including optional Tag
///        Out << Value;
///      }
///      static StringRef input(StringRef Scalar, StringRef Tag, void*, MyType
///      &Value) {
///        // parse scalar and set `value`
///        // return empty string on success, or error string
///        return StringRef();
///      }
///      static QuotingType mustQuote(const MyType &Value, StringRef) {
///        return QuotingType::Single;
///      }
///    };
template <typename T> struct TaggedScalarTraits {
  // Must provide:
  //
  // Function to write the value and tag as strings:
  // static void output(const T &Value, void *ctx, llvm::raw_ostream &ScalarOut,
  // llvm::raw_ostream &TagOut);
  //
  // Function to convert a string to a value.  Returns the empty
  // StringRef on success or an error string if string is malformed:
  // static StringRef input(StringRef Scalar, StringRef Tag, void *ctxt, T
  // &Value);
  //
  // Function to determine if the value should be quoted.
  // static QuotingType mustQuote(const T &Value, StringRef Scalar);
};

/// This class should be specialized by any type that needs to be converted
/// to/from a YAML sequence.  For example:
///
///    template<>
///    struct SequenceTraits<MyContainer> {
///      static size_t size(IO &io, MyContainer &seq) {
///        return seq.size();
///      }
///      static MyType& element(IO &, MyContainer &seq, size_t index) {
///        if ( index >= seq.size() )
///          seq.resize(index+1);
///        return seq[index];
///      }
///    };
template<typename T, typename EnableIf = void>
struct SequenceTraits {
  // Must provide:
  // static size_t size(IO &io, T &seq);
  // static T::value_type& element(IO &io, T &seq, size_t index);
  //
  // The following is option and will cause generated YAML to use
  // a flow sequence (e.g. [a,b,c]).
  // static const bool flow = true;
};

/// This class should be specialized by any type for which vectors of that
/// type need to be converted to/from a YAML sequence.
template<typename T, typename EnableIf = void>
struct SequenceElementTraits {
  // Must provide:
  // static const bool flow;
};

/// This class should be specialized by any type that needs to be converted
/// to/from a list of YAML documents.
template<typename T>
struct DocumentListTraits {
  // Must provide:
  // static size_t size(IO &io, T &seq);
  // static T::value_type& element(IO &io, T &seq, size_t index);
};

/// This class should be specialized by any type that needs to be converted
/// to/from a YAML mapping in the case where the names of the keys are not known
/// in advance, e.g. a string map.
template <typename T>
struct CustomMappingTraits {
  // static void inputOne(IO &io, StringRef key, T &elem);
  // static void output(IO &io, T &elem);
};

/// This class should be specialized by any type that can be represented as
/// a scalar, map, or sequence, decided dynamically. For example:
///
///    typedef std::unique_ptr<MyBase> MyPoly;
///
///    template<>
///    struct PolymorphicTraits<MyPoly> {
///      static NodeKind getKind(const MyPoly &poly) {
///        return poly->getKind();
///      }
///      static MyScalar& getAsScalar(MyPoly &poly) {
///        if (!poly || !isa<MyScalar>(poly))
///          poly.reset(new MyScalar());
///        return *cast<MyScalar>(poly.get());
///      }
///      // ...
///    };
template <typename T> struct PolymorphicTraits {
  // Must provide:
  // static NodeKind getKind(const T &poly);
  // static scalar_type &getAsScalar(T &poly);
  // static map_type &getAsMap(T &poly);
  // static sequence_type &getAsSequence(T &poly);
};

// Only used for better diagnostics of missing traits
template <typename T>
struct MissingTrait;

// Test if ScalarEnumerationTraits<T> is defined on type T.
template <class T>
struct has_ScalarEnumerationTraits
{
  using Signature_enumeration = void (*)(class IO&, T&);

  template <typename U>
  static char test(SameType<Signature_enumeration, &U::enumeration>*);

  template <typename U>
  static double test(...);

  static bool const value =
    (sizeof(test<ScalarEnumerationTraits<T>>(nullptr)) == 1);
};

// Test if ScalarBitSetTraits<T> is defined on type T.
template <class T>
struct has_ScalarBitSetTraits
{
  using Signature_bitset = void (*)(class IO&, T&);

  template <typename U>
  static char test(SameType<Signature_bitset, &U::bitset>*);

  template <typename U>
  static double test(...);

  static bool const value = (sizeof(test<ScalarBitSetTraits<T>>(nullptr)) == 1);
};

// Test if ScalarTraits<T> is defined on type T.
template <class T>
struct has_ScalarTraits
{
  using Signature_input = StringRef (*)(StringRef, void*, T&);
  using Signature_output = void (*)(const T&, void*, raw_ostream&);
  using Signature_mustQuote = QuotingType (*)(StringRef);

  template <typename U>
  static char test(SameType<Signature_input, &U::input> *,
                   SameType<Signature_output, &U::output> *,
                   SameType<Signature_mustQuote, &U::mustQuote> *);

  template <typename U>
  static double test(...);

  static bool const value =
      (sizeof(test<ScalarTraits<T>>(nullptr, nullptr, nullptr)) == 1);
};

// Test if BlockScalarTraits<T> is defined on type T.
template <class T>
struct has_BlockScalarTraits
{
  using Signature_input = StringRef (*)(StringRef, void *, T &);
  using Signature_output = void (*)(const T &, void *, raw_ostream &);

  template <typename U>
  static char test(SameType<Signature_input, &U::input> *,
                   SameType<Signature_output, &U::output> *);

  template <typename U>
  static double test(...);

  static bool const value =
      (sizeof(test<BlockScalarTraits<T>>(nullptr, nullptr)) == 1);
};

// Test if TaggedScalarTraits<T> is defined on type T.
template <class T> struct has_TaggedScalarTraits {
  using Signature_input = StringRef (*)(StringRef, StringRef, void *, T &);
  using Signature_output = void (*)(const T &, void *, raw_ostream &,
                                    raw_ostream &);
  using Signature_mustQuote = QuotingType (*)(const T &, StringRef);

  template <typename U>
  static char test(SameType<Signature_input, &U::input> *,
                   SameType<Signature_output, &U::output> *,
                   SameType<Signature_mustQuote, &U::mustQuote> *);

  template <typename U> static double test(...);

  static bool const value =
      (sizeof(test<TaggedScalarTraits<T>>(nullptr, nullptr, nullptr)) == 1);
};

// Test if MappingContextTraits<T> is defined on type T.
template <class T, class Context> struct has_MappingTraits {
  using Signature_mapping = void (*)(class IO &, T &, Context &);

  template <typename U>
  static char test(SameType<Signature_mapping, &U::mapping>*);

  template <typename U>
  static double test(...);

  static bool const value =
      (sizeof(test<MappingContextTraits<T, Context>>(nullptr)) == 1);
};

// Test if MappingTraits<T> is defined on type T.
template <class T> struct has_MappingTraits<T, EmptyContext> {
  using Signature_mapping = void (*)(class IO &, T &);

  template <typename U>
  static char test(SameType<Signature_mapping, &U::mapping> *);

  template <typename U> static double test(...);

  static bool const value = (sizeof(test<MappingTraits<T>>(nullptr)) == 1);
};

// Test if MappingContextTraits<T>::validate() is defined on type T.
template <class T, class Context> struct has_MappingValidateTraits {
  using Signature_validate = StringRef (*)(class IO &, T &, Context &);

  template <typename U>
  static char test(SameType<Signature_validate, &U::validate>*);

  template <typename U>
  static double test(...);

  static bool const value =
      (sizeof(test<MappingContextTraits<T, Context>>(nullptr)) == 1);
};

// Test if MappingTraits<T>::validate() is defined on type T.
template <class T> struct has_MappingValidateTraits<T, EmptyContext> {
  using Signature_validate = StringRef (*)(class IO &, T &);

  template <typename U>
  static char test(SameType<Signature_validate, &U::validate> *);

  template <typename U> static double test(...);

  static bool const value = (sizeof(test<MappingTraits<T>>(nullptr)) == 1);
};

// Test if SequenceTraits<T> is defined on type T.
template <class T>
struct has_SequenceMethodTraits
{
  using Signature_size = size_t (*)(class IO&, T&);

  template <typename U>
  static char test(SameType<Signature_size, &U::size>*);

  template <typename U>
  static double test(...);

  static bool const value =  (sizeof(test<SequenceTraits<T>>(nullptr)) == 1);
};

// Test if CustomMappingTraits<T> is defined on type T.
template <class T>
struct has_CustomMappingTraits
{
  using Signature_input = void (*)(IO &io, StringRef key, T &v);

  template <typename U>
  static char test(SameType<Signature_input, &U::inputOne>*);

  template <typename U>
  static double test(...);

  static bool const value =
      (sizeof(test<CustomMappingTraits<T>>(nullptr)) == 1);
};

// has_FlowTraits<int> will cause an error with some compilers because
// it subclasses int.  Using this wrapper only instantiates the
// real has_FlowTraits only if the template type is a class.
template <typename T, bool Enabled = std::is_class<T>::value>
class has_FlowTraits
{
public:
   static const bool value = false;
};

// Some older gcc compilers don't support straight forward tests
// for members, so test for ambiguity cause by the base and derived
// classes both defining the member.
template <class T>
struct has_FlowTraits<T, true>
{
  struct Fallback { bool flow; };
  struct Derived : T, Fallback { };

  template<typename C>
  static char (&f(SameType<bool Fallback::*, &C::flow>*))[1];

  template<typename C>
  static char (&f(...))[2];

  static bool const value = sizeof(f<Derived>(nullptr)) == 2;
};

// Test if SequenceTraits<T> is defined on type T
template<typename T>
struct has_SequenceTraits : public std::integral_constant<bool,
                                      has_SequenceMethodTraits<T>::value > { };

// Test if DocumentListTraits<T> is defined on type T
template <class T>
struct has_DocumentListTraits
{
  using Signature_size = size_t (*)(class IO &, T &);

  template <typename U>
  static char test(SameType<Signature_size, &U::size>*);

  template <typename U>
  static double test(...);

  static bool const value = (sizeof(test<DocumentListTraits<T>>(nullptr))==1);
};

template <class T> struct has_PolymorphicTraits {
  using Signature_getKind = NodeKind (*)(const T &);

  template <typename U>
  static char test(SameType<Signature_getKind, &U::getKind> *);

  template <typename U> static double test(...);

  static bool const value = (sizeof(test<PolymorphicTraits<T>>(nullptr)) == 1);
};

inline bool isNumeric(StringRef S) {
  const static auto skipDigits = [](StringRef Input) {
    return Input.drop_front(
        std::min(Input.find_first_not_of("0123456789"), Input.size()));
  };

  // Make S.front() and S.drop_front().front() (if S.front() is [+-]) calls
  // safe.
  if (S.empty() || S.equals("+") || S.equals("-"))
    return false;

  if (S.equals(".nan") || S.equals(".NaN") || S.equals(".NAN"))
    return true;

  // Infinity and decimal numbers can be prefixed with sign.
  StringRef Tail = (S.front() == '-' || S.front() == '+') ? S.drop_front() : S;

  // Check for infinity first, because checking for hex and oct numbers is more
  // expensive.
  if (Tail.equals(".inf") || Tail.equals(".Inf") || Tail.equals(".INF"))
    return true;

  // Section 10.3.2 Tag Resolution
  // YAML 1.2 Specification prohibits Base 8 and Base 16 numbers prefixed with
  // [-+], so S should be used instead of Tail.
  if (S.startswith("0o"))
    return S.size() > 2 &&
           S.drop_front(2).find_first_not_of("01234567") == StringRef::npos;

  if (S.startswith("0x"))
    return S.size() > 2 && S.drop_front(2).find_first_not_of(
                               "0123456789abcdefABCDEF") == StringRef::npos;

  // Parse float: [-+]? (\. [0-9]+ | [0-9]+ (\. [0-9]* )?) ([eE] [-+]? [0-9]+)?
  S = Tail;

  // Handle cases when the number starts with '.' and hence needs at least one
  // digit after dot (as opposed by number which has digits before the dot), but
  // doesn't have one.
  if (S.startswith(".") &&
      (S.equals(".") ||
       (S.size() > 1 && std::strchr("0123456789", S[1]) == nullptr)))
    return false;

  if (S.startswith("E") || S.startswith("e"))
    return false;

  enum ParseState {
    Default,
    FoundDot,
    FoundExponent,
  };
  ParseState State = Default;

  S = skipDigits(S);

  // Accept decimal integer.
  if (S.empty())
    return true;

  if (S.front() == '.') {
    State = FoundDot;
    S = S.drop_front();
  } else if (S.front() == 'e' || S.front() == 'E') {
    State = FoundExponent;
    S = S.drop_front();
  } else {
    return false;
  }

  if (State == FoundDot) {
    S = skipDigits(S);
    if (S.empty())
      return true;

    if (S.front() == 'e' || S.front() == 'E') {
      State = FoundExponent;
      S = S.drop_front();
    } else {
      return false;
    }
  }

  assert(State == FoundExponent && "Should have found exponent at this point.");
  if (S.empty())
    return false;

  if (S.front() == '+' || S.front() == '-') {
    S = S.drop_front();
    if (S.empty())
      return false;
  }

  return skipDigits(S).empty();
}

inline bool isNull(StringRef S) {
  return S.equals("null") || S.equals("Null") || S.equals("NULL") ||
         S.equals("~");
}

inline bool isBool(StringRef S) {
  return S.equals("true") || S.equals("True") || S.equals("TRUE") ||
         S.equals("false") || S.equals("False") || S.equals("FALSE");
}

// 5.1. Character Set
// The allowed character range explicitly excludes the C0 control block #x0-#x1F
// (except for TAB #x9, LF #xA, and CR #xD which are allowed), DEL #x7F, the C1
// control block #x80-#x9F (except for NEL #x85 which is allowed), the surrogate
// block #xD800-#xDFFF, #xFFFE, and #xFFFF.
inline QuotingType needsQuotes(StringRef S) {
  if (S.empty())
    return QuotingType::Single;
  if (isspace(S.front()) || isspace(S.back()))
    return QuotingType::Single;
  if (isNull(S))
    return QuotingType::Single;
  if (isBool(S))
    return QuotingType::Single;
  if (isNumeric(S))
    return QuotingType::Single;

  // 7.3.3 Plain Style
  // Plain scalars must not begin with most indicators, as this would cause
  // ambiguity with other YAML constructs.
  static constexpr char Indicators[] = R"(-?:\,[]{}#&*!|>'"%@`)";
  if (S.find_first_of(Indicators) == 0)
    return QuotingType::Single;

  QuotingType MaxQuotingNeeded = QuotingType::None;
  for (unsigned char C : S) {
    // Alphanum is safe.
    if (isAlnum(C))
      continue;

    switch (C) {
    // Safe scalar characters.
    case '_':
    case '-':
    case '^':
    case '.':
    case ',':
    case ' ':
    // TAB (0x9) is allowed in unquoted strings.
    case 0x9:
      continue;
    // LF(0xA) and CR(0xD) may delimit values and so require at least single
    // quotes.
    case 0xA:
    case 0xD:
      MaxQuotingNeeded = QuotingType::Single;
      continue;
    // DEL (0x7F) are excluded from the allowed character range.
    case 0x7F:
      return QuotingType::Double;
    // Forward slash is allowed to be unquoted, but we quote it anyway.  We have
    // many tests that use FileCheck against YAML output, and this output often
    // contains paths.  If we quote backslashes but not forward slashes then
    // paths will come out either quoted or unquoted depending on which platform
    // the test is run on, making FileCheck comparisons difficult.
    case '/':
    default: {
      // C0 control block (0x0 - 0x1F) is excluded from the allowed character
      // range.
      if (C <= 0x1F)
        return QuotingType::Double;

      // Always double quote UTF-8.
      if ((C & 0x80) != 0)
        return QuotingType::Double;

      // The character is not safe, at least simple quoting needed.
      MaxQuotingNeeded = QuotingType::Single;
    }
    }
  }

  return MaxQuotingNeeded;
}

template <typename T, typename Context>
struct missingTraits
    : public std::integral_constant<bool,
                                    !has_ScalarEnumerationTraits<T>::value &&
                                        !has_ScalarBitSetTraits<T>::value &&
                                        !has_ScalarTraits<T>::value &&
                                        !has_BlockScalarTraits<T>::value &&
                                        !has_TaggedScalarTraits<T>::value &&
                                        !has_MappingTraits<T, Context>::value &&
                                        !has_SequenceTraits<T>::value &&
                                        !has_CustomMappingTraits<T>::value &&
                                        !has_DocumentListTraits<T>::value &&
                                        !has_PolymorphicTraits<T>::value> {};

template <typename T, typename Context>
struct validatedMappingTraits
    : public std::integral_constant<
          bool, has_MappingTraits<T, Context>::value &&
                    has_MappingValidateTraits<T, Context>::value> {};

template <typename T, typename Context>
struct unvalidatedMappingTraits
    : public std::integral_constant<
          bool, has_MappingTraits<T, Context>::value &&
                    !has_MappingValidateTraits<T, Context>::value> {};

// Base class for Input and Output.
class IO {
public:
  IO(void *Ctxt = nullptr);
  virtual ~IO();

  virtual bool outputting() = 0;

  virtual unsigned beginSequence() = 0;
  virtual bool preflightElement(unsigned, void *&) = 0;
  virtual void postflightElement(void*) = 0;
  virtual void endSequence() = 0;
  virtual bool canElideEmptySequence() = 0;

  virtual unsigned beginFlowSequence() = 0;
  virtual bool preflightFlowElement(unsigned, void *&) = 0;
  virtual void postflightFlowElement(void*) = 0;
  virtual void endFlowSequence() = 0;

  virtual bool mapTag(StringRef Tag, bool Default=false) = 0;
  virtual void beginMapping() = 0;
  virtual void endMapping() = 0;
  virtual bool preflightKey(const char*, bool, bool, bool &, void *&) = 0;
  virtual void postflightKey(void*) = 0;
  virtual std::vector<StringRef> keys() = 0;

  virtual void beginFlowMapping() = 0;
  virtual void endFlowMapping() = 0;

  virtual void beginEnumScalar() = 0;
  virtual bool matchEnumScalar(const char*, bool) = 0;
  virtual bool matchEnumFallback() = 0;
  virtual void endEnumScalar() = 0;

  virtual bool beginBitSetScalar(bool &) = 0;
  virtual bool bitSetMatch(const char*, bool) = 0;
  virtual void endBitSetScalar() = 0;

  virtual void scalarString(StringRef &, QuotingType) = 0;
  virtual void blockScalarString(StringRef &) = 0;
  virtual void scalarTag(std::string &) = 0;

  virtual NodeKind getNodeKind() = 0;

  virtual void setError(const Twine &) = 0;

  template <typename T>
  void enumCase(T &Val, const char* Str, const T ConstVal) {
    if ( matchEnumScalar(Str, outputting() && Val == ConstVal) ) {
      Val = ConstVal;
    }
  }

  // allow anonymous enum values to be used with LLVM_YAML_STRONG_TYPEDEF
  template <typename T>
  void enumCase(T &Val, const char* Str, const uint32_t ConstVal) {
    if ( matchEnumScalar(Str, outputting() && Val == static_cast<T>(ConstVal)) ) {
      Val = ConstVal;
    }
  }

  template <typename FBT, typename T>
  void enumFallback(T &Val) {
    if (matchEnumFallback()) {
      EmptyContext Context;
      // FIXME: Force integral conversion to allow strong typedefs to convert.
      FBT Res = static_cast<typename FBT::BaseType>(Val);
      yamlize(*this, Res, true, Context);
      Val = static_cast<T>(static_cast<typename FBT::BaseType>(Res));
    }
  }

  template <typename T>
  void bitSetCase(T &Val, const char* Str, const T ConstVal) {
    if ( bitSetMatch(Str, outputting() && (Val & ConstVal) == ConstVal) ) {
      Val = static_cast<T>(Val | ConstVal);
    }
  }

  // allow anonymous enum values to be used with LLVM_YAML_STRONG_TYPEDEF
  template <typename T>
  void bitSetCase(T &Val, const char* Str, const uint32_t ConstVal) {
    if ( bitSetMatch(Str, outputting() && (Val & ConstVal) == ConstVal) ) {
      Val = static_cast<T>(Val | ConstVal);
    }
  }

  template <typename T>
  void maskedBitSetCase(T &Val, const char *Str, T ConstVal, T Mask) {
    if (bitSetMatch(Str, outputting() && (Val & Mask) == ConstVal))
      Val = Val | ConstVal;
  }

  template <typename T>
  void maskedBitSetCase(T &Val, const char *Str, uint32_t ConstVal,
                        uint32_t Mask) {
    if (bitSetMatch(Str, outputting() && (Val & Mask) == ConstVal))
      Val = Val | ConstVal;
  }

  void *getContext();
  void setContext(void *);

  template <typename T> void mapRequired(const char *Key, T &Val) {
    EmptyContext Ctx;
    this->processKey(Key, Val, true, Ctx);
  }

  template <typename T, typename Context>
  void mapRequired(const char *Key, T &Val, Context &Ctx) {
    this->processKey(Key, Val, true, Ctx);
  }

  template <typename T> void mapOptional(const char *Key, T &Val) {
    EmptyContext Ctx;
    mapOptionalWithContext(Key, Val, Ctx);
  }

  template <typename T>
  void mapOptional(const char *Key, T &Val, const T &Default) {
    EmptyContext Ctx;
    mapOptionalWithContext(Key, Val, Default, Ctx);
  }

  template <typename T, typename Context>
  typename std::enable_if<has_SequenceTraits<T>::value, void>::type
  mapOptionalWithContext(const char *Key, T &Val, Context &Ctx) {
    // omit key/value instead of outputting empty sequence
    if (this->canElideEmptySequence() && !(Val.begin() != Val.end()))
      return;
    this->processKey(Key, Val, false, Ctx);
  }

  template <typename T, typename Context>
  void mapOptionalWithContext(const char *Key, Optional<T> &Val, Context &Ctx) {
    this->processKeyWithDefault(Key, Val, Optional<T>(), /*Required=*/false,
                                Ctx);
  }

  template <typename T, typename Context>
  typename std::enable_if<!has_SequenceTraits<T>::value, void>::type
  mapOptionalWithContext(const char *Key, T &Val, Context &Ctx) {
    this->processKey(Key, Val, false, Ctx);
  }

  template <typename T, typename Context>
  void mapOptionalWithContext(const char *Key, T &Val, const T &Default,
                              Context &Ctx) {
    this->processKeyWithDefault(Key, Val, Default, false, Ctx);
  }

private:
  template <typename T, typename Context>
  void processKeyWithDefault(const char *Key, Optional<T> &Val,
                             const Optional<T> &DefaultValue, bool Required,
                             Context &Ctx) {
    assert(DefaultValue.hasValue() == false &&
           "Optional<T> shouldn't have a value!");
    void *SaveInfo;
    bool UseDefault = true;
    const bool sameAsDefault = outputting() && !Val.hasValue();
    if (!outputting() && !Val.hasValue())
      Val = T();
    if (Val.hasValue() &&
        this->preflightKey(Key, Required, sameAsDefault, UseDefault,
                           SaveInfo)) {
      yamlize(*this, Val.getValue(), Required, Ctx);
      this->postflightKey(SaveInfo);
    } else {
      if (UseDefault)
        Val = DefaultValue;
    }
  }

  template <typename T, typename Context>
  void processKeyWithDefault(const char *Key, T &Val, const T &DefaultValue,
                             bool Required, Context &Ctx) {
    void *SaveInfo;
    bool UseDefault;
    const bool sameAsDefault = outputting() && Val == DefaultValue;
    if ( this->preflightKey(Key, Required, sameAsDefault, UseDefault,
                                                                  SaveInfo) ) {
      yamlize(*this, Val, Required, Ctx);
      this->postflightKey(SaveInfo);
    }
    else {
      if ( UseDefault )
        Val = DefaultValue;
    }
  }

  template <typename T, typename Context>
  void processKey(const char *Key, T &Val, bool Required, Context &Ctx) {
    void *SaveInfo;
    bool UseDefault;
    if ( this->preflightKey(Key, Required, false, UseDefault, SaveInfo) ) {
      yamlize(*this, Val, Required, Ctx);
      this->postflightKey(SaveInfo);
    }
  }

private:
  void *Ctxt;
};

namespace detail {

template <typename T, typename Context>
void doMapping(IO &io, T &Val, Context &Ctx) {
  MappingContextTraits<T, Context>::mapping(io, Val, Ctx);
}

template <typename T> void doMapping(IO &io, T &Val, EmptyContext &Ctx) {
  MappingTraits<T>::mapping(io, Val);
}

} // end namespace detail

template <typename T>
typename std::enable_if<has_ScalarEnumerationTraits<T>::value, void>::type
yamlize(IO &io, T &Val, bool, EmptyContext &Ctx) {
  io.beginEnumScalar();
  ScalarEnumerationTraits<T>::enumeration(io, Val);
  io.endEnumScalar();
}

template <typename T>
typename std::enable_if<has_ScalarBitSetTraits<T>::value, void>::type
yamlize(IO &io, T &Val, bool, EmptyContext &Ctx) {
  bool DoClear;
  if ( io.beginBitSetScalar(DoClear) ) {
    if ( DoClear )
      Val = static_cast<T>(0);
    ScalarBitSetTraits<T>::bitset(io, Val);
    io.endBitSetScalar();
  }
}

template <typename T>
typename std::enable_if<has_ScalarTraits<T>::value, void>::type
yamlize(IO &io, T &Val, bool, EmptyContext &Ctx) {
  if ( io.outputting() ) {
    std::string Storage;
    raw_string_ostream Buffer(Storage);
    ScalarTraits<T>::output(Val, io.getContext(), Buffer);
    StringRef Str = Buffer.str();
    io.scalarString(Str, ScalarTraits<T>::mustQuote(Str));
  }
  else {
    StringRef Str;
    io.scalarString(Str, ScalarTraits<T>::mustQuote(Str));
    StringRef Result = ScalarTraits<T>::input(Str, io.getContext(), Val);
    if ( !Result.empty() ) {
      io.setError(Twine(Result));
    }
  }
}

template <typename T>
typename std::enable_if<has_BlockScalarTraits<T>::value, void>::type
yamlize(IO &YamlIO, T &Val, bool, EmptyContext &Ctx) {
  if (YamlIO.outputting()) {
    std::string Storage;
    raw_string_ostream Buffer(Storage);
    BlockScalarTraits<T>::output(Val, YamlIO.getContext(), Buffer);
    StringRef Str = Buffer.str();
    YamlIO.blockScalarString(Str);
  } else {
    StringRef Str;
    YamlIO.blockScalarString(Str);
    StringRef Result =
        BlockScalarTraits<T>::input(Str, YamlIO.getContext(), Val);
    if (!Result.empty())
      YamlIO.setError(Twine(Result));
  }
}

template <typename T>
typename std::enable_if<has_TaggedScalarTraits<T>::value, void>::type
yamlize(IO &io, T &Val, bool, EmptyContext &Ctx) {
  if (io.outputting()) {
    std::string ScalarStorage, TagStorage;
    raw_string_ostream ScalarBuffer(ScalarStorage), TagBuffer(TagStorage);
    TaggedScalarTraits<T>::output(Val, io.getContext(), ScalarBuffer,
                                  TagBuffer);
    io.scalarTag(TagBuffer.str());
    StringRef ScalarStr = ScalarBuffer.str();
    io.scalarString(ScalarStr,
                    TaggedScalarTraits<T>::mustQuote(Val, ScalarStr));
  } else {
    std::string Tag;
    io.scalarTag(Tag);
    StringRef Str;
    io.scalarString(Str, QuotingType::None);
    StringRef Result =
        TaggedScalarTraits<T>::input(Str, Tag, io.getContext(), Val);
    if (!Result.empty()) {
      io.setError(Twine(Result));
    }
  }
}

template <typename T, typename Context>
typename std::enable_if<validatedMappingTraits<T, Context>::value, void>::type
yamlize(IO &io, T &Val, bool, Context &Ctx) {
  if (has_FlowTraits<MappingTraits<T>>::value)
    io.beginFlowMapping();
  else
    io.beginMapping();
  if (io.outputting()) {
    StringRef Err = MappingTraits<T>::validate(io, Val);
    if (!Err.empty()) {
      errs() << Err << "\n";
      assert(Err.empty() && "invalid struct trying to be written as yaml");
    }
  }
  detail::doMapping(io, Val, Ctx);
  if (!io.outputting()) {
    StringRef Err = MappingTraits<T>::validate(io, Val);
    if (!Err.empty())
      io.setError(Err);
  }
  if (has_FlowTraits<MappingTraits<T>>::value)
    io.endFlowMapping();
  else
    io.endMapping();
}

template <typename T, typename Context>
typename std::enable_if<unvalidatedMappingTraits<T, Context>::value, void>::type
yamlize(IO &io, T &Val, bool, Context &Ctx) {
  if (has_FlowTraits<MappingTraits<T>>::value) {
    io.beginFlowMapping();
    detail::doMapping(io, Val, Ctx);
    io.endFlowMapping();
  } else {
    io.beginMapping();
    detail::doMapping(io, Val, Ctx);
    io.endMapping();
  }
}

template <typename T>
typename std::enable_if<has_CustomMappingTraits<T>::value, void>::type
yamlize(IO &io, T &Val, bool, EmptyContext &Ctx) {
  if ( io.outputting() ) {
    io.beginMapping();
    CustomMappingTraits<T>::output(io, Val);
    io.endMapping();
  } else {
    io.beginMapping();
    for (StringRef key : io.keys())
      CustomMappingTraits<T>::inputOne(io, key, Val);
    io.endMapping();
  }
}

template <typename T>
typename std::enable_if<has_PolymorphicTraits<T>::value, void>::type
yamlize(IO &io, T &Val, bool, EmptyContext &Ctx) {
  switch (io.outputting() ? PolymorphicTraits<T>::getKind(Val)
                          : io.getNodeKind()) {
  case NodeKind::Scalar:
    return yamlize(io, PolymorphicTraits<T>::getAsScalar(Val), true, Ctx);
  case NodeKind::Map:
    return yamlize(io, PolymorphicTraits<T>::getAsMap(Val), true, Ctx);
  case NodeKind::Sequence:
    return yamlize(io, PolymorphicTraits<T>::getAsSequence(Val), true, Ctx);
  }
}

template <typename T>
typename std::enable_if<missingTraits<T, EmptyContext>::value, void>::type
yamlize(IO &io, T &Val, bool, EmptyContext &Ctx) {
  char missing_yaml_trait_for_type[sizeof(MissingTrait<T>)];
}

template <typename T, typename Context>
typename std::enable_if<has_SequenceTraits<T>::value, void>::type
yamlize(IO &io, T &Seq, bool, Context &Ctx) {
  if ( has_FlowTraits< SequenceTraits<T>>::value ) {
    unsigned incnt = io.beginFlowSequence();
    unsigned count = io.outputting() ? SequenceTraits<T>::size(io, Seq) : incnt;
    for(unsigned i=0; i < count; ++i) {
      void *SaveInfo;
      if ( io.preflightFlowElement(i, SaveInfo) ) {
        yamlize(io, SequenceTraits<T>::element(io, Seq, i), true, Ctx);
        io.postflightFlowElement(SaveInfo);
      }
    }
    io.endFlowSequence();
  }
  else {
    unsigned incnt = io.beginSequence();
    unsigned count = io.outputting() ? SequenceTraits<T>::size(io, Seq) : incnt;
    for(unsigned i=0; i < count; ++i) {
      void *SaveInfo;
      if ( io.preflightElement(i, SaveInfo) ) {
        yamlize(io, SequenceTraits<T>::element(io, Seq, i), true, Ctx);
        io.postflightElement(SaveInfo);
      }
    }
    io.endSequence();
  }
}

template<>
struct ScalarTraits<bool> {
  static void output(const bool &, void* , raw_ostream &);
  static StringRef input(StringRef, void *, bool &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template<>
struct ScalarTraits<StringRef> {
  static void output(const StringRef &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, StringRef &);
  static QuotingType mustQuote(StringRef S) { return needsQuotes(S); }
};

template<>
struct ScalarTraits<std::string> {
  static void output(const std::string &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, std::string &);
  static QuotingType mustQuote(StringRef S) { return needsQuotes(S); }
};

template<>
struct ScalarTraits<uint8_t> {
  static void output(const uint8_t &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, uint8_t &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template<>
struct ScalarTraits<uint16_t> {
  static void output(const uint16_t &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, uint16_t &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template<>
struct ScalarTraits<uint32_t> {
  static void output(const uint32_t &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, uint32_t &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template<>
struct ScalarTraits<uint64_t> {
  static void output(const uint64_t &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, uint64_t &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template<>
struct ScalarTraits<int8_t> {
  static void output(const int8_t &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, int8_t &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template<>
struct ScalarTraits<int16_t> {
  static void output(const int16_t &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, int16_t &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template<>
struct ScalarTraits<int32_t> {
  static void output(const int32_t &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, int32_t &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template<>
struct ScalarTraits<int64_t> {
  static void output(const int64_t &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, int64_t &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template<>
struct ScalarTraits<float> {
  static void output(const float &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, float &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template<>
struct ScalarTraits<double> {
  static void output(const double &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, double &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

// For endian types, we just use the existing ScalarTraits for the underlying
// type.  This way endian aware types are supported whenever a ScalarTraits
// is defined for the underlying type.
template <typename value_type, support::endianness endian, size_t alignment>
struct ScalarTraits<support::detail::packed_endian_specific_integral<
    value_type, endian, alignment>> {
  using endian_type =
      support::detail::packed_endian_specific_integral<value_type, endian,
                                                       alignment>;

  static void output(const endian_type &E, void *Ctx, raw_ostream &Stream) {
    ScalarTraits<value_type>::output(static_cast<value_type>(E), Ctx, Stream);
  }

  static StringRef input(StringRef Str, void *Ctx, endian_type &E) {
    value_type V;
    auto R = ScalarTraits<value_type>::input(Str, Ctx, V);
    E = static_cast<endian_type>(V);
    return R;
  }

  static QuotingType mustQuote(StringRef Str) {
    return ScalarTraits<value_type>::mustQuote(Str);
  }
};

// Utility for use within MappingTraits<>::mapping() method
// to [de]normalize an object for use with YAML conversion.
template <typename TNorm, typename TFinal>
struct MappingNormalization {
  MappingNormalization(IO &i_o, TFinal &Obj)
      : io(i_o), BufPtr(nullptr), Result(Obj) {
    if ( io.outputting() ) {
      BufPtr = new (&Buffer) TNorm(io, Obj);
    }
    else {
      BufPtr = new (&Buffer) TNorm(io);
    }
  }

  ~MappingNormalization() {
    if ( ! io.outputting() ) {
      Result = BufPtr->denormalize(io);
    }
    BufPtr->~TNorm();
  }

  TNorm* operator->() { return BufPtr; }

private:
  using Storage = AlignedCharArrayUnion<TNorm>;

  Storage       Buffer;
  IO           &io;
  TNorm        *BufPtr;
  TFinal       &Result;
};

// Utility for use within MappingTraits<>::mapping() method
// to [de]normalize an object for use with YAML conversion.
template <typename TNorm, typename TFinal>
struct MappingNormalizationHeap {
  MappingNormalizationHeap(IO &i_o, TFinal &Obj, BumpPtrAllocator *allocator)
    : io(i_o), Result(Obj) {
    if ( io.outputting() ) {
      BufPtr = new (&Buffer) TNorm(io, Obj);
    }
    else if (allocator) {
      BufPtr = allocator->Allocate<TNorm>();
      new (BufPtr) TNorm(io);
    } else {
      BufPtr = new TNorm(io);
    }
  }

  ~MappingNormalizationHeap() {
    if ( io.outputting() ) {
      BufPtr->~TNorm();
    }
    else {
      Result = BufPtr->denormalize(io);
    }
  }

  TNorm* operator->() { return BufPtr; }

private:
  using Storage = AlignedCharArrayUnion<TNorm>;

  Storage       Buffer;
  IO           &io;
  TNorm        *BufPtr = nullptr;
  TFinal       &Result;
};

///
/// The Input class is used to parse a yaml document into in-memory structs
/// and vectors.
///
/// It works by using YAMLParser to do a syntax parse of the entire yaml
/// document, then the Input class builds a graph of HNodes which wraps
/// each yaml Node.  The extra layer is buffering.  The low level yaml
/// parser only lets you look at each node once.  The buffering layer lets
/// you search and interate multiple times.  This is necessary because
/// the mapRequired() method calls may not be in the same order
/// as the keys in the document.
///
class Input : public IO {
public:
  // Construct a yaml Input object from a StringRef and optional
  // user-data. The DiagHandler can be specified to provide
  // alternative error reporting.
  Input(StringRef InputContent,
        void *Ctxt = nullptr,
        SourceMgr::DiagHandlerTy DiagHandler = nullptr,
        void *DiagHandlerCtxt = nullptr);
  Input(MemoryBufferRef Input,
        void *Ctxt = nullptr,
        SourceMgr::DiagHandlerTy DiagHandler = nullptr,
        void *DiagHandlerCtxt = nullptr);
  ~Input() override;

  // Check if there was an syntax or semantic error during parsing.
  std::error_code error();

private:
  bool outputting() override;
  bool mapTag(StringRef, bool) override;
  void beginMapping() override;
  void endMapping() override;
  bool preflightKey(const char *, bool, bool, bool &, void *&) override;
  void postflightKey(void *) override;
  std::vector<StringRef> keys() override;
  void beginFlowMapping() override;
  void endFlowMapping() override;
  unsigned beginSequence() override;
  void endSequence() override;
  bool preflightElement(unsigned index, void *&) override;
  void postflightElement(void *) override;
  unsigned beginFlowSequence() override;
  bool preflightFlowElement(unsigned , void *&) override;
  void postflightFlowElement(void *) override;
  void endFlowSequence() override;
  void beginEnumScalar() override;
  bool matchEnumScalar(const char*, bool) override;
  bool matchEnumFallback() override;
  void endEnumScalar() override;
  bool beginBitSetScalar(bool &) override;
  bool bitSetMatch(const char *, bool ) override;
  void endBitSetScalar() override;
  void scalarString(StringRef &, QuotingType) override;
  void blockScalarString(StringRef &) override;
  void scalarTag(std::string &) override;
  NodeKind getNodeKind() override;
  void setError(const Twine &message) override;
  bool canElideEmptySequence() override;

  class HNode {
    virtual void anchor();

  public:
    HNode(Node *n) : _node(n) { }
    virtual ~HNode() = default;

    static bool classof(const HNode *) { return true; }

    Node *_node;
  };

  class EmptyHNode : public HNode {
    void anchor() override;

  public:
    EmptyHNode(Node *n) : HNode(n) { }

    static bool classof(const HNode *n) { return NullNode::classof(n->_node); }

    static bool classof(const EmptyHNode *) { return true; }
  };

  class ScalarHNode : public HNode {
    void anchor() override;

  public:
    ScalarHNode(Node *n, StringRef s) : HNode(n), _value(s) { }

    StringRef value() const { return _value; }

    static bool classof(const HNode *n) {
      return ScalarNode::classof(n->_node) ||
             BlockScalarNode::classof(n->_node);
    }

    static bool classof(const ScalarHNode *) { return true; }

  protected:
    StringRef _value;
  };

  class MapHNode : public HNode {
    void anchor() override;

  public:
    MapHNode(Node *n) : HNode(n) { }

    static bool classof(const HNode *n) {
      return MappingNode::classof(n->_node);
    }

    static bool classof(const MapHNode *) { return true; }

    using NameToNode = StringMap<std::unique_ptr<HNode>>;

    NameToNode Mapping;
    SmallVector<std::string, 6> ValidKeys;
  };

  class SequenceHNode : public HNode {
    void anchor() override;

  public:
    SequenceHNode(Node *n) : HNode(n) { }

    static bool classof(const HNode *n) {
      return SequenceNode::classof(n->_node);
    }

    static bool classof(const SequenceHNode *) { return true; }

    std::vector<std::unique_ptr<HNode>> Entries;
  };

  std::unique_ptr<Input::HNode> createHNodes(Node *node);
  void setError(HNode *hnode, const Twine &message);
  void setError(Node *node, const Twine &message);

public:
  // These are only used by operator>>. They could be private
  // if those templated things could be made friends.
  bool setCurrentDocument();
  bool nextDocument();

  /// Returns the current node that's being parsed by the YAML Parser.
  const Node *getCurrentNode() const;

private:
  SourceMgr                           SrcMgr; // must be before Strm
  std::unique_ptr<llvm::yaml::Stream> Strm;
  std::unique_ptr<HNode>              TopNode;
  std::error_code                     EC;
  BumpPtrAllocator                    StringAllocator;
  document_iterator                   DocIterator;
  std::vector<bool>                   BitValuesUsed;
  HNode *CurrentNode = nullptr;
  bool                                ScalarMatchFound;
};

///
/// The Output class is used to generate a yaml document from in-memory structs
/// and vectors.
///
class Output : public IO {
public:
  Output(raw_ostream &, void *Ctxt = nullptr, int WrapColumn = 70);
  ~Output() override;

  /// Set whether or not to output optional values which are equal
  /// to the default value.  By default, when outputting if you attempt
  /// to write a value that is equal to the default, the value gets ignored.
  /// Sometimes, it is useful to be able to see these in the resulting YAML
  /// anyway.
  void setWriteDefaultValues(bool Write) { WriteDefaultValues = Write; }

  bool outputting() override;
  bool mapTag(StringRef, bool) override;
  void beginMapping() override;
  void endMapping() override;
  bool preflightKey(const char *key, bool, bool, bool &, void *&) override;
  void postflightKey(void *) override;
  std::vector<StringRef> keys() override;
  void beginFlowMapping() override;
  void endFlowMapping() override;
  unsigned beginSequence() override;
  void endSequence() override;
  bool preflightElement(unsigned, void *&) override;
  void postflightElement(void *) override;
  unsigned beginFlowSequence() override;
  bool preflightFlowElement(unsigned, void *&) override;
  void postflightFlowElement(void *) override;
  void endFlowSequence() override;
  void beginEnumScalar() override;
  bool matchEnumScalar(const char*, bool) override;
  bool matchEnumFallback() override;
  void endEnumScalar() override;
  bool beginBitSetScalar(bool &) override;
  bool bitSetMatch(const char *, bool ) override;
  void endBitSetScalar() override;
  void scalarString(StringRef &, QuotingType) override;
  void blockScalarString(StringRef &) override;
  void scalarTag(std::string &) override;
  NodeKind getNodeKind() override;
  void setError(const Twine &message) override;
  bool canElideEmptySequence() override;

  // These are only used by operator<<. They could be private
  // if that templated operator could be made a friend.
  void beginDocuments();
  bool preflightDocument(unsigned);
  void postflightDocument();
  void endDocuments();

private:
  void output(StringRef s);
  void outputUpToEndOfLine(StringRef s);
  void newLineCheck();
  void outputNewLine();
  void paddedKey(StringRef key);
  void flowKey(StringRef Key);

  enum InState {
    inSeqFirstElement,
    inSeqOtherElement,
    inFlowSeqFirstElement,
    inFlowSeqOtherElement,
    inMapFirstKey,
    inMapOtherKey,
    inFlowMapFirstKey,
    inFlowMapOtherKey
  };

  static bool inSeqAnyElement(InState State);
  static bool inFlowSeqAnyElement(InState State);
  static bool inMapAnyKey(InState State);
  static bool inFlowMapAnyKey(InState State);

  raw_ostream &Out;
  int WrapColumn;
  SmallVector<InState, 8> StateStack;
  int Column = 0;
  int ColumnAtFlowStart = 0;
  int ColumnAtMapFlowStart = 0;
  bool NeedBitValueComma = false;
  bool NeedFlowSequenceComma = false;
  bool EnumerationMatchFound = false;
  bool NeedsNewLine = false;
  bool WriteDefaultValues = false;
};

/// YAML I/O does conversion based on types. But often native data types
/// are just a typedef of built in intergral types (e.g. int).  But the C++
/// type matching system sees through the typedef and all the typedefed types
/// look like a built in type. This will cause the generic YAML I/O conversion
/// to be used. To provide better control over the YAML conversion, you can
/// use this macro instead of typedef.  It will create a class with one field
/// and automatic conversion operators to and from the base type.
/// Based on BOOST_STRONG_TYPEDEF
#define LLVM_YAML_STRONG_TYPEDEF(_base, _type)                                 \
    struct _type {                                                             \
        _type() = default;                                                     \
        _type(const _base v) : value(v) {}                                     \
        _type(const _type &v) = default;                                       \
        _type &operator=(const _type &rhs) = default;                          \
        _type &operator=(const _base &rhs) { value = rhs; return *this; }      \
        operator const _base & () const { return value; }                      \
        bool operator==(const _type &rhs) const { return value == rhs.value; } \
        bool operator==(const _base &rhs) const { return value == rhs; }       \
        bool operator<(const _type &rhs) const { return value < rhs.value; }   \
        _base value;                                                           \
        using BaseType = _base;                                                \
    };

///
/// Use these types instead of uintXX_t in any mapping to have
/// its yaml output formatted as hexadecimal.
///
LLVM_YAML_STRONG_TYPEDEF(uint8_t, Hex8)
LLVM_YAML_STRONG_TYPEDEF(uint16_t, Hex16)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, Hex32)
LLVM_YAML_STRONG_TYPEDEF(uint64_t, Hex64)

template<>
struct ScalarTraits<Hex8> {
  static void output(const Hex8 &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, Hex8 &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template<>
struct ScalarTraits<Hex16> {
  static void output(const Hex16 &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, Hex16 &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template<>
struct ScalarTraits<Hex32> {
  static void output(const Hex32 &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, Hex32 &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template<>
struct ScalarTraits<Hex64> {
  static void output(const Hex64 &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, Hex64 &);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

// Define non-member operator>> so that Input can stream in a document list.
template <typename T>
inline
typename std::enable_if<has_DocumentListTraits<T>::value, Input &>::type
operator>>(Input &yin, T &docList) {
  int i = 0;
  EmptyContext Ctx;
  while ( yin.setCurrentDocument() ) {
    yamlize(yin, DocumentListTraits<T>::element(yin, docList, i), true, Ctx);
    if ( yin.error() )
      return yin;
    yin.nextDocument();
    ++i;
  }
  return yin;
}

// Define non-member operator>> so that Input can stream in a map as a document.
template <typename T>
inline typename std::enable_if<has_MappingTraits<T, EmptyContext>::value,
                               Input &>::type
operator>>(Input &yin, T &docMap) {
  EmptyContext Ctx;
  yin.setCurrentDocument();
  yamlize(yin, docMap, true, Ctx);
  return yin;
}

// Define non-member operator>> so that Input can stream in a sequence as
// a document.
template <typename T>
inline
typename std::enable_if<has_SequenceTraits<T>::value, Input &>::type
operator>>(Input &yin, T &docSeq) {
  EmptyContext Ctx;
  if (yin.setCurrentDocument())
    yamlize(yin, docSeq, true, Ctx);
  return yin;
}

// Define non-member operator>> so that Input can stream in a block scalar.
template <typename T>
inline
typename std::enable_if<has_BlockScalarTraits<T>::value, Input &>::type
operator>>(Input &In, T &Val) {
  EmptyContext Ctx;
  if (In.setCurrentDocument())
    yamlize(In, Val, true, Ctx);
  return In;
}

// Define non-member operator>> so that Input can stream in a string map.
template <typename T>
inline
typename std::enable_if<has_CustomMappingTraits<T>::value, Input &>::type
operator>>(Input &In, T &Val) {
  EmptyContext Ctx;
  if (In.setCurrentDocument())
    yamlize(In, Val, true, Ctx);
  return In;
}

// Define non-member operator>> so that Input can stream in a polymorphic type.
template <typename T>
inline typename std::enable_if<has_PolymorphicTraits<T>::value, Input &>::type
operator>>(Input &In, T &Val) {
  EmptyContext Ctx;
  if (In.setCurrentDocument())
    yamlize(In, Val, true, Ctx);
  return In;
}

// Provide better error message about types missing a trait specialization
template <typename T>
inline typename std::enable_if<missingTraits<T, EmptyContext>::value,
                               Input &>::type
operator>>(Input &yin, T &docSeq) {
  char missing_yaml_trait_for_type[sizeof(MissingTrait<T>)];
  return yin;
}

// Define non-member operator<< so that Output can stream out document list.
template <typename T>
inline
typename std::enable_if<has_DocumentListTraits<T>::value, Output &>::type
operator<<(Output &yout, T &docList) {
  EmptyContext Ctx;
  yout.beginDocuments();
  const size_t count = DocumentListTraits<T>::size(yout, docList);
  for(size_t i=0; i < count; ++i) {
    if ( yout.preflightDocument(i) ) {
      yamlize(yout, DocumentListTraits<T>::element(yout, docList, i), true,
              Ctx);
      yout.postflightDocument();
    }
  }
  yout.endDocuments();
  return yout;
}

// Define non-member operator<< so that Output can stream out a map.
template <typename T>
inline typename std::enable_if<has_MappingTraits<T, EmptyContext>::value,
                               Output &>::type
operator<<(Output &yout, T &map) {
  EmptyContext Ctx;
  yout.beginDocuments();
  if ( yout.preflightDocument(0) ) {
    yamlize(yout, map, true, Ctx);
    yout.postflightDocument();
  }
  yout.endDocuments();
  return yout;
}

// Define non-member operator<< so that Output can stream out a sequence.
template <typename T>
inline
typename std::enable_if<has_SequenceTraits<T>::value, Output &>::type
operator<<(Output &yout, T &seq) {
  EmptyContext Ctx;
  yout.beginDocuments();
  if ( yout.preflightDocument(0) ) {
    yamlize(yout, seq, true, Ctx);
    yout.postflightDocument();
  }
  yout.endDocuments();
  return yout;
}

// Define non-member operator<< so that Output can stream out a block scalar.
template <typename T>
inline
typename std::enable_if<has_BlockScalarTraits<T>::value, Output &>::type
operator<<(Output &Out, T &Val) {
  EmptyContext Ctx;
  Out.beginDocuments();
  if (Out.preflightDocument(0)) {
    yamlize(Out, Val, true, Ctx);
    Out.postflightDocument();
  }
  Out.endDocuments();
  return Out;
}

// Define non-member operator<< so that Output can stream out a string map.
template <typename T>
inline
typename std::enable_if<has_CustomMappingTraits<T>::value, Output &>::type
operator<<(Output &Out, T &Val) {
  EmptyContext Ctx;
  Out.beginDocuments();
  if (Out.preflightDocument(0)) {
    yamlize(Out, Val, true, Ctx);
    Out.postflightDocument();
  }
  Out.endDocuments();
  return Out;
}

// Define non-member operator<< so that Output can stream out a polymorphic
// type.
template <typename T>
inline typename std::enable_if<has_PolymorphicTraits<T>::value, Output &>::type
operator<<(Output &Out, T &Val) {
  EmptyContext Ctx;
  Out.beginDocuments();
  if (Out.preflightDocument(0)) {
    // FIXME: The parser does not support explicit documents terminated with a
    // plain scalar; the end-marker is included as part of the scalar token.
    assert(PolymorphicTraits<T>::getKind(Val) != NodeKind::Scalar && "plain scalar documents are not supported");
    yamlize(Out, Val, true, Ctx);
    Out.postflightDocument();
  }
  Out.endDocuments();
  return Out;
}

// Provide better error message about types missing a trait specialization
template <typename T>
inline typename std::enable_if<missingTraits<T, EmptyContext>::value,
                               Output &>::type
operator<<(Output &yout, T &seq) {
  char missing_yaml_trait_for_type[sizeof(MissingTrait<T>)];
  return yout;
}

template <bool B> struct IsFlowSequenceBase {};
template <> struct IsFlowSequenceBase<true> { static const bool flow = true; };

template <typename T, bool Flow>
struct SequenceTraitsImpl : IsFlowSequenceBase<Flow> {
private:
  using type = typename T::value_type;

public:
  static size_t size(IO &io, T &seq) { return seq.size(); }

  static type &element(IO &io, T &seq, size_t index) {
    if (index >= seq.size())
      seq.resize(index + 1);
    return seq[index];
  }
};

// Simple helper to check an expression can be used as a bool-valued template
// argument.
template <bool> struct CheckIsBool { static const bool value = true; };

// If T has SequenceElementTraits, then vector<T> and SmallVector<T, N> have
// SequenceTraits that do the obvious thing.
template <typename T>
struct SequenceTraits<std::vector<T>,
                      typename std::enable_if<CheckIsBool<
                          SequenceElementTraits<T>::flow>::value>::type>
    : SequenceTraitsImpl<std::vector<T>, SequenceElementTraits<T>::flow> {};
template <typename T, unsigned N>
struct SequenceTraits<SmallVector<T, N>,
                      typename std::enable_if<CheckIsBool<
                          SequenceElementTraits<T>::flow>::value>::type>
    : SequenceTraitsImpl<SmallVector<T, N>, SequenceElementTraits<T>::flow> {};

// Sequences of fundamental types use flow formatting.
template <typename T>
struct SequenceElementTraits<
    T, typename std::enable_if<std::is_fundamental<T>::value>::type> {
  static const bool flow = true;
};

// Sequences of strings use block formatting.
template<> struct SequenceElementTraits<std::string> {
  static const bool flow = false;
};
template<> struct SequenceElementTraits<StringRef> {
  static const bool flow = false;
};
template<> struct SequenceElementTraits<std::pair<std::string, std::string>> {
  static const bool flow = false;
};

/// Implementation of CustomMappingTraits for std::map<std::string, T>.
template <typename T> struct StdMapStringCustomMappingTraitsImpl {
  using map_type = std::map<std::string, T>;

  static void inputOne(IO &io, StringRef key, map_type &v) {
    io.mapRequired(key.str().c_str(), v[key]);
  }

  static void output(IO &io, map_type &v) {
    for (auto &p : v)
      io.mapRequired(p.first.c_str(), p.second);
  }
};

} // end namespace yaml
} // end namespace llvm

#define LLVM_YAML_IS_SEQUENCE_VECTOR_IMPL(TYPE, FLOW)                          \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  static_assert(                                                               \
      !std::is_fundamental<TYPE>::value &&                                     \
      !std::is_same<TYPE, std::string>::value &&                               \
      !std::is_same<TYPE, llvm::StringRef>::value,                             \
      "only use LLVM_YAML_IS_SEQUENCE_VECTOR for types you control");          \
  template <> struct SequenceElementTraits<TYPE> {                             \
    static const bool flow = FLOW;                                             \
  };                                                                           \
  }                                                                            \
  }

/// Utility for declaring that a std::vector of a particular type
/// should be considered a YAML sequence.
#define LLVM_YAML_IS_SEQUENCE_VECTOR(type)                                     \
  LLVM_YAML_IS_SEQUENCE_VECTOR_IMPL(type, false)

/// Utility for declaring that a std::vector of a particular type
/// should be considered a YAML flow sequence.
#define LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(type)                                \
  LLVM_YAML_IS_SEQUENCE_VECTOR_IMPL(type, true)

#define LLVM_YAML_DECLARE_MAPPING_TRAITS(Type)                                 \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  template <> struct MappingTraits<Type> {                                     \
    static void mapping(IO &IO, Type &Obj);                                    \
  };                                                                           \
  }                                                                            \
  }

#define LLVM_YAML_DECLARE_ENUM_TRAITS(Type)                                    \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  template <> struct ScalarEnumerationTraits<Type> {                           \
    static void enumeration(IO &io, Type &Value);                              \
  };                                                                           \
  }                                                                            \
  }

#define LLVM_YAML_DECLARE_BITSET_TRAITS(Type)                                  \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  template <> struct ScalarBitSetTraits<Type> {                                \
    static void bitset(IO &IO, Type &Options);                                 \
  };                                                                           \
  }                                                                            \
  }

#define LLVM_YAML_DECLARE_SCALAR_TRAITS(Type, MustQuote)                       \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  template <> struct ScalarTraits<Type> {                                      \
    static void output(const Type &Value, void *ctx, raw_ostream &Out);        \
    static StringRef input(StringRef Scalar, void *ctxt, Type &Value);         \
    static QuotingType mustQuote(StringRef) { return MustQuote; }              \
  };                                                                           \
  }                                                                            \
  }

/// Utility for declaring that a std::vector of a particular type
/// should be considered a YAML document list.
#define LLVM_YAML_IS_DOCUMENT_LIST_VECTOR(_type)                               \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  template <unsigned N>                                                        \
  struct DocumentListTraits<SmallVector<_type, N>>                             \
      : public SequenceTraitsImpl<SmallVector<_type, N>, false> {};            \
  template <>                                                                  \
  struct DocumentListTraits<std::vector<_type>>                                \
      : public SequenceTraitsImpl<std::vector<_type>, false> {};               \
  }                                                                            \
  }

/// Utility for declaring that std::map<std::string, _type> should be considered
/// a YAML map.
#define LLVM_YAML_IS_STRING_MAP(_type)                                         \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  template <>                                                                  \
  struct CustomMappingTraits<std::map<std::string, _type>>                     \
      : public StdMapStringCustomMappingTraitsImpl<_type> {};                  \
  }                                                                            \
  }

#endif // LLVM_SUPPORT_YAMLTRAITS_H
