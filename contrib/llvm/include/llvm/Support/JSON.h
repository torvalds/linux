//===--- JSON.h - JSON values, parsing and serialization -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//
///
/// \file
/// This file supports working with JSON data.
///
/// It comprises:
///
/// - classes which hold dynamically-typed parsed JSON structures
///   These are value types that can be composed, inspected, and modified.
///   See json::Value, and the related types json::Object and json::Array.
///
/// - functions to parse JSON text into Values, and to serialize Values to text.
///   See parse(), operator<<, and format_provider.
///
/// - a convention and helpers for mapping between json::Value and user-defined
///   types. See fromJSON(), ObjectMapper, and the class comment on Value.
///
/// Typically, JSON data would be read from an external source, parsed into
/// a Value, and then converted into some native data structure before doing
/// real work on it. (And vice versa when writing).
///
/// Other serialization mechanisms you may consider:
///
/// - YAML is also text-based, and more human-readable than JSON. It's a more
///   complex format and data model, and YAML parsers aren't ubiquitous.
///   YAMLParser.h is a streaming parser suitable for parsing large documents
///   (including JSON, as YAML is a superset). It can be awkward to use
///   directly. YAML I/O (YAMLTraits.h) provides data mapping that is more
///   declarative than the toJSON/fromJSON conventions here.
///
/// - LLVM bitstream is a space- and CPU- efficient binary format. Typically it
///   encodes LLVM IR ("bitcode"), but it can be a container for other data.
///   Low-level reader/writer libraries are in Bitcode/Bitstream*.h
///
//===---------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_JSON_H
#define LLVM_SUPPORT_JSON_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include <map>

namespace llvm {
namespace json {

// === String encodings ===
//
// JSON strings are character sequences (not byte sequences like std::string).
// We need to know the encoding, and for simplicity only support UTF-8.
//
//   - When parsing, invalid UTF-8 is a syntax error like any other
//
//   - When creating Values from strings, callers must ensure they are UTF-8.
//        with asserts on, invalid UTF-8 will crash the program
//        with asserts off, we'll substitute the replacement character (U+FFFD)
//     Callers can use json::isUTF8() and json::fixUTF8() for validation.
//
//   - When retrieving strings from Values (e.g. asString()), the result will
//     always be valid UTF-8.

/// Returns true if \p S is valid UTF-8, which is required for use as JSON.
/// If it returns false, \p Offset is set to a byte offset near the first error.
bool isUTF8(llvm::StringRef S, size_t *ErrOffset = nullptr);
/// Replaces invalid UTF-8 sequences in \p S with the replacement character
/// (U+FFFD). The returned string is valid UTF-8.
/// This is much slower than isUTF8, so test that first.
std::string fixUTF8(llvm::StringRef S);

class Array;
class ObjectKey;
class Value;
template <typename T> Value toJSON(const llvm::Optional<T> &Opt);

/// An Object is a JSON object, which maps strings to heterogenous JSON values.
/// It simulates DenseMap<ObjectKey, Value>. ObjectKey is a maybe-owned string.
class Object {
  using Storage = DenseMap<ObjectKey, Value, llvm::DenseMapInfo<StringRef>>;
  Storage M;

public:
  using key_type = ObjectKey;
  using mapped_type = Value;
  using value_type = Storage::value_type;
  using iterator = Storage::iterator;
  using const_iterator = Storage::const_iterator;

  explicit Object() = default;
  // KV is a trivial key-value struct for list-initialization.
  // (using std::pair forces extra copies).
  struct KV;
  explicit Object(std::initializer_list<KV> Properties);

  iterator begin() { return M.begin(); }
  const_iterator begin() const { return M.begin(); }
  iterator end() { return M.end(); }
  const_iterator end() const { return M.end(); }

  bool empty() const { return M.empty(); }
  size_t size() const { return M.size(); }

  void clear() { M.clear(); }
  std::pair<iterator, bool> insert(KV E);
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(const ObjectKey &K, Ts &&... Args) {
    return M.try_emplace(K, std::forward<Ts>(Args)...);
  }
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(ObjectKey &&K, Ts &&... Args) {
    return M.try_emplace(std::move(K), std::forward<Ts>(Args)...);
  }

  iterator find(StringRef K) { return M.find_as(K); }
  const_iterator find(StringRef K) const { return M.find_as(K); }
  // operator[] acts as if Value was default-constructible as null.
  Value &operator[](const ObjectKey &K);
  Value &operator[](ObjectKey &&K);
  // Look up a property, returning nullptr if it doesn't exist.
  Value *get(StringRef K);
  const Value *get(StringRef K) const;
  // Typed accessors return None/nullptr if
  //   - the property doesn't exist
  //   - or it has the wrong type
  llvm::Optional<std::nullptr_t> getNull(StringRef K) const;
  llvm::Optional<bool> getBoolean(StringRef K) const;
  llvm::Optional<double> getNumber(StringRef K) const;
  llvm::Optional<int64_t> getInteger(StringRef K) const;
  llvm::Optional<llvm::StringRef> getString(StringRef K) const;
  const json::Object *getObject(StringRef K) const;
  json::Object *getObject(StringRef K);
  const json::Array *getArray(StringRef K) const;
  json::Array *getArray(StringRef K);
};
bool operator==(const Object &LHS, const Object &RHS);
inline bool operator!=(const Object &LHS, const Object &RHS) {
  return !(LHS == RHS);
}

/// An Array is a JSON array, which contains heterogeneous JSON values.
/// It simulates std::vector<Value>.
class Array {
  std::vector<Value> V;

public:
  using value_type = Value;
  using iterator = std::vector<Value>::iterator;
  using const_iterator = std::vector<Value>::const_iterator;

  explicit Array() = default;
  explicit Array(std::initializer_list<Value> Elements);
  template <typename Collection> explicit Array(const Collection &C) {
    for (const auto &V : C)
      emplace_back(V);
  }

  Value &operator[](size_t I) { return V[I]; }
  const Value &operator[](size_t I) const { return V[I]; }
  Value &front() { return V.front(); }
  const Value &front() const { return V.front(); }
  Value &back() { return V.back(); }
  const Value &back() const { return V.back(); }
  Value *data() { return V.data(); }
  const Value *data() const { return V.data(); }

  iterator begin() { return V.begin(); }
  const_iterator begin() const { return V.begin(); }
  iterator end() { return V.end(); }
  const_iterator end() const { return V.end(); }

  bool empty() const { return V.empty(); }
  size_t size() const { return V.size(); }

  void clear() { V.clear(); }
  void push_back(const Value &E) { V.push_back(E); }
  void push_back(Value &&E) { V.push_back(std::move(E)); }
  template <typename... Args> void emplace_back(Args &&... A) {
    V.emplace_back(std::forward<Args>(A)...);
  }
  void pop_back() { V.pop_back(); }
  // FIXME: insert() takes const_iterator since C++11, old libstdc++ disagrees.
  iterator insert(iterator P, const Value &E) { return V.insert(P, E); }
  iterator insert(iterator P, Value &&E) {
    return V.insert(P, std::move(E));
  }
  template <typename It> iterator insert(iterator P, It A, It Z) {
    return V.insert(P, A, Z);
  }
  template <typename... Args> iterator emplace(const_iterator P, Args &&... A) {
    return V.emplace(P, std::forward<Args>(A)...);
  }

  friend bool operator==(const Array &L, const Array &R) { return L.V == R.V; }
};
inline bool operator!=(const Array &L, const Array &R) { return !(L == R); }

/// A Value is an JSON value of unknown type.
/// They can be copied, but should generally be moved.
///
/// === Composing values ===
///
/// You can implicitly construct Values from:
///   - strings: std::string, SmallString, formatv, StringRef, char*
///              (char*, and StringRef are references, not copies!)
///   - numbers
///   - booleans
///   - null: nullptr
///   - arrays: {"foo", 42.0, false}
///   - serializable things: types with toJSON(const T&)->Value, found by ADL
///
/// They can also be constructed from object/array helpers:
///   - json::Object is a type like map<ObjectKey, Value>
///   - json::Array is a type like vector<Value>
/// These can be list-initialized, or used to build up collections in a loop.
/// json::ary(Collection) converts all items in a collection to Values.
///
/// === Inspecting values ===
///
/// Each Value is one of the JSON kinds:
///   null    (nullptr_t)
///   boolean (bool)
///   number  (double or int64)
///   string  (StringRef)
///   array   (json::Array)
///   object  (json::Object)
///
/// The kind can be queried directly, or implicitly via the typed accessors:
///   if (Optional<StringRef> S = E.getAsString()
///     assert(E.kind() == Value::String);
///
/// Array and Object also have typed indexing accessors for easy traversal:
///   Expected<Value> E = parse(R"( {"options": {"font": "sans-serif"}} )");
///   if (Object* O = E->getAsObject())
///     if (Object* Opts = O->getObject("options"))
///       if (Optional<StringRef> Font = Opts->getString("font"))
///         assert(Opts->at("font").kind() == Value::String);
///
/// === Converting JSON values to C++ types ===
///
/// The convention is to have a deserializer function findable via ADL:
///     fromJSON(const json::Value&, T&)->bool
/// Deserializers are provided for:
///   - bool
///   - int and int64_t
///   - double
///   - std::string
///   - vector<T>, where T is deserializable
///   - map<string, T>, where T is deserializable
///   - Optional<T>, where T is deserializable
/// ObjectMapper can help writing fromJSON() functions for object types.
///
/// For conversion in the other direction, the serializer function is:
///    toJSON(const T&) -> json::Value
/// If this exists, then it also allows constructing Value from T, and can
/// be used to serialize vector<T>, map<string, T>, and Optional<T>.
///
/// === Serialization ===
///
/// Values can be serialized to JSON:
///   1) raw_ostream << Value                    // Basic formatting.
///   2) raw_ostream << formatv("{0}", Value)    // Basic formatting.
///   3) raw_ostream << formatv("{0:2}", Value)  // Pretty-print with indent 2.
///
/// And parsed:
///   Expected<Value> E = json::parse("[1, 2, null]");
///   assert(E && E->kind() == Value::Array);
class Value {
public:
  enum Kind {
    Null,
    Boolean,
    /// Number values can store both int64s and doubles at full precision,
    /// depending on what they were constructed/parsed from.
    Number,
    String,
    Array,
    Object,
  };

  // It would be nice to have Value() be null. But that would make {} null too.
  Value(const Value &M) { copyFrom(M); }
  Value(Value &&M) { moveFrom(std::move(M)); }
  Value(std::initializer_list<Value> Elements);
  Value(json::Array &&Elements) : Type(T_Array) {
    create<json::Array>(std::move(Elements));
  }
  template <typename Elt>
  Value(const std::vector<Elt> &C) : Value(json::Array(C)) {}
  Value(json::Object &&Properties) : Type(T_Object) {
    create<json::Object>(std::move(Properties));
  }
  template <typename Elt>
  Value(const std::map<std::string, Elt> &C) : Value(json::Object(C)) {}
  // Strings: types with value semantics. Must be valid UTF-8.
  Value(std::string V) : Type(T_String) {
    if (LLVM_UNLIKELY(!isUTF8(V))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      V = fixUTF8(std::move(V));
    }
    create<std::string>(std::move(V));
  }
  Value(const llvm::SmallVectorImpl<char> &V)
      : Value(std::string(V.begin(), V.end())){};
  Value(const llvm::formatv_object_base &V) : Value(V.str()){};
  // Strings: types with reference semantics. Must be valid UTF-8.
  Value(StringRef V) : Type(T_StringRef) {
    create<llvm::StringRef>(V);
    if (LLVM_UNLIKELY(!isUTF8(V))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      *this = Value(fixUTF8(V));
    }
  }
  Value(const char *V) : Value(StringRef(V)) {}
  Value(std::nullptr_t) : Type(T_Null) {}
  // Boolean (disallow implicit conversions).
  // (The last template parameter is a dummy to keep templates distinct.)
  template <
      typename T,
      typename = typename std::enable_if<std::is_same<T, bool>::value>::type,
      bool = false>
  Value(T B) : Type(T_Boolean) {
    create<bool>(B);
  }
  // Integers (except boolean). Must be non-narrowing convertible to int64_t.
  template <
      typename T,
      typename = typename std::enable_if<std::is_integral<T>::value>::type,
      typename = typename std::enable_if<!std::is_same<T, bool>::value>::type>
  Value(T I) : Type(T_Integer) {
    create<int64_t>(int64_t{I});
  }
  // Floating point. Must be non-narrowing convertible to double.
  template <typename T,
            typename =
                typename std::enable_if<std::is_floating_point<T>::value>::type,
            double * = nullptr>
  Value(T D) : Type(T_Double) {
    create<double>(double{D});
  }
  // Serializable types: with a toJSON(const T&)->Value function, found by ADL.
  template <typename T,
            typename = typename std::enable_if<std::is_same<
                Value, decltype(toJSON(*(const T *)nullptr))>::value>,
            Value * = nullptr>
  Value(const T &V) : Value(toJSON(V)) {}

  Value &operator=(const Value &M) {
    destroy();
    copyFrom(M);
    return *this;
  }
  Value &operator=(Value &&M) {
    destroy();
    moveFrom(std::move(M));
    return *this;
  }
  ~Value() { destroy(); }

  Kind kind() const {
    switch (Type) {
    case T_Null:
      return Null;
    case T_Boolean:
      return Boolean;
    case T_Double:
    case T_Integer:
      return Number;
    case T_String:
    case T_StringRef:
      return String;
    case T_Object:
      return Object;
    case T_Array:
      return Array;
    }
    llvm_unreachable("Unknown kind");
  }

  // Typed accessors return None/nullptr if the Value is not of this type.
  llvm::Optional<std::nullptr_t> getAsNull() const {
    if (LLVM_LIKELY(Type == T_Null))
      return nullptr;
    return llvm::None;
  }
  llvm::Optional<bool> getAsBoolean() const {
    if (LLVM_LIKELY(Type == T_Boolean))
      return as<bool>();
    return llvm::None;
  }
  llvm::Optional<double> getAsNumber() const {
    if (LLVM_LIKELY(Type == T_Double))
      return as<double>();
    if (LLVM_LIKELY(Type == T_Integer))
      return as<int64_t>();
    return llvm::None;
  }
  // Succeeds if the Value is a Number, and exactly representable as int64_t.
  llvm::Optional<int64_t> getAsInteger() const {
    if (LLVM_LIKELY(Type == T_Integer))
      return as<int64_t>();
    if (LLVM_LIKELY(Type == T_Double)) {
      double D = as<double>();
      if (LLVM_LIKELY(std::modf(D, &D) == 0.0 &&
                      D >= double(std::numeric_limits<int64_t>::min()) &&
                      D <= double(std::numeric_limits<int64_t>::max())))
        return D;
    }
    return llvm::None;
  }
  llvm::Optional<llvm::StringRef> getAsString() const {
    if (Type == T_String)
      return llvm::StringRef(as<std::string>());
    if (LLVM_LIKELY(Type == T_StringRef))
      return as<llvm::StringRef>();
    return llvm::None;
  }
  const json::Object *getAsObject() const {
    return LLVM_LIKELY(Type == T_Object) ? &as<json::Object>() : nullptr;
  }
  json::Object *getAsObject() {
    return LLVM_LIKELY(Type == T_Object) ? &as<json::Object>() : nullptr;
  }
  const json::Array *getAsArray() const {
    return LLVM_LIKELY(Type == T_Array) ? &as<json::Array>() : nullptr;
  }
  json::Array *getAsArray() {
    return LLVM_LIKELY(Type == T_Array) ? &as<json::Array>() : nullptr;
  }

  /// Serializes this Value to JSON, writing it to the provided stream.
  /// The formatting is compact (no extra whitespace) and deterministic.
  /// For pretty-printing, use the formatv() format_provider below.
  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &, const Value &);

private:
  void destroy();
  void copyFrom(const Value &M);
  // We allow moving from *const* Values, by marking all members as mutable!
  // This hack is needed to support initializer-list syntax efficiently.
  // (std::initializer_list<T> is a container of const T).
  void moveFrom(const Value &&M);
  friend class Array;
  friend class Object;

  template <typename T, typename... U> void create(U &&... V) {
    new (reinterpret_cast<T *>(Union.buffer)) T(std::forward<U>(V)...);
  }
  template <typename T> T &as() const {
    // Using this two-step static_cast via void * instead of reinterpret_cast
    // silences a -Wstrict-aliasing false positive from GCC6 and earlier.
    void *Storage = static_cast<void *>(Union.buffer);
    return *static_cast<T *>(Storage);
  }

  template <typename Indenter>
  void print(llvm::raw_ostream &, const Indenter &) const;
  friend struct llvm::format_provider<llvm::json::Value>;

  enum ValueType : char {
    T_Null,
    T_Boolean,
    T_Double,
    T_Integer,
    T_StringRef,
    T_String,
    T_Object,
    T_Array,
  };
  // All members mutable, see moveFrom().
  mutable ValueType Type;
  mutable llvm::AlignedCharArrayUnion<bool, double, int64_t, llvm::StringRef,
                                      std::string, json::Array, json::Object>
      Union;
  friend bool operator==(const Value &, const Value &);
};

bool operator==(const Value &, const Value &);
inline bool operator!=(const Value &L, const Value &R) { return !(L == R); }
llvm::raw_ostream &operator<<(llvm::raw_ostream &, const Value &);

/// ObjectKey is a used to capture keys in Object. Like Value but:
///   - only strings are allowed
///   - it's optimized for the string literal case (Owned == nullptr)
/// Like Value, strings must be UTF-8. See isUTF8 documentation for details.
class ObjectKey {
public:
  ObjectKey(const char *S) : ObjectKey(StringRef(S)) {}
  ObjectKey(std::string S) : Owned(new std::string(std::move(S))) {
    if (LLVM_UNLIKELY(!isUTF8(*Owned))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      *Owned = fixUTF8(std::move(*Owned));
    }
    Data = *Owned;
  }
  ObjectKey(llvm::StringRef S) : Data(S) {
    if (LLVM_UNLIKELY(!isUTF8(Data))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      *this = ObjectKey(fixUTF8(S));
    }
  }
  ObjectKey(const llvm::SmallVectorImpl<char> &V)
      : ObjectKey(std::string(V.begin(), V.end())) {}
  ObjectKey(const llvm::formatv_object_base &V) : ObjectKey(V.str()) {}

  ObjectKey(const ObjectKey &C) { *this = C; }
  ObjectKey(ObjectKey &&C) : ObjectKey(static_cast<const ObjectKey &&>(C)) {}
  ObjectKey &operator=(const ObjectKey &C) {
    if (C.Owned) {
      Owned.reset(new std::string(*C.Owned));
      Data = *Owned;
    } else {
      Data = C.Data;
    }
    return *this;
  }
  ObjectKey &operator=(ObjectKey &&) = default;

  operator llvm::StringRef() const { return Data; }
  std::string str() const { return Data.str(); }

private:
  // FIXME: this is unneccesarily large (3 pointers). Pointer + length + owned
  // could be 2 pointers at most.
  std::unique_ptr<std::string> Owned;
  llvm::StringRef Data;
};

inline bool operator==(const ObjectKey &L, const ObjectKey &R) {
  return llvm::StringRef(L) == llvm::StringRef(R);
}
inline bool operator!=(const ObjectKey &L, const ObjectKey &R) {
  return !(L == R);
}
inline bool operator<(const ObjectKey &L, const ObjectKey &R) {
  return StringRef(L) < StringRef(R);
}

struct Object::KV {
  ObjectKey K;
  Value V;
};

inline Object::Object(std::initializer_list<KV> Properties) {
  for (const auto &P : Properties) {
    auto R = try_emplace(P.K, nullptr);
    if (R.second)
      R.first->getSecond().moveFrom(std::move(P.V));
  }
}
inline std::pair<Object::iterator, bool> Object::insert(KV E) {
  return try_emplace(std::move(E.K), std::move(E.V));
}

// Standard deserializers are provided for primitive types.
// See comments on Value.
inline bool fromJSON(const Value &E, std::string &Out) {
  if (auto S = E.getAsString()) {
    Out = *S;
    return true;
  }
  return false;
}
inline bool fromJSON(const Value &E, int &Out) {
  if (auto S = E.getAsInteger()) {
    Out = *S;
    return true;
  }
  return false;
}
inline bool fromJSON(const Value &E, int64_t &Out) {
  if (auto S = E.getAsInteger()) {
    Out = *S;
    return true;
  }
  return false;
}
inline bool fromJSON(const Value &E, double &Out) {
  if (auto S = E.getAsNumber()) {
    Out = *S;
    return true;
  }
  return false;
}
inline bool fromJSON(const Value &E, bool &Out) {
  if (auto S = E.getAsBoolean()) {
    Out = *S;
    return true;
  }
  return false;
}
template <typename T> bool fromJSON(const Value &E, llvm::Optional<T> &Out) {
  if (E.getAsNull()) {
    Out = llvm::None;
    return true;
  }
  T Result;
  if (!fromJSON(E, Result))
    return false;
  Out = std::move(Result);
  return true;
}
template <typename T> bool fromJSON(const Value &E, std::vector<T> &Out) {
  if (auto *A = E.getAsArray()) {
    Out.clear();
    Out.resize(A->size());
    for (size_t I = 0; I < A->size(); ++I)
      if (!fromJSON((*A)[I], Out[I]))
        return false;
    return true;
  }
  return false;
}
template <typename T>
bool fromJSON(const Value &E, std::map<std::string, T> &Out) {
  if (auto *O = E.getAsObject()) {
    Out.clear();
    for (const auto &KV : *O)
      if (!fromJSON(KV.second, Out[llvm::StringRef(KV.first)]))
        return false;
    return true;
  }
  return false;
}

// Allow serialization of Optional<T> for supported T.
template <typename T> Value toJSON(const llvm::Optional<T> &Opt) {
  return Opt ? Value(*Opt) : Value(nullptr);
}

/// Helper for mapping JSON objects onto protocol structs.
///
/// Example:
/// \code
///   bool fromJSON(const Value &E, MyStruct &R) {
///     ObjectMapper O(E);
///     if (!O || !O.map("mandatory_field", R.MandatoryField))
///       return false;
///     O.map("optional_field", R.OptionalField);
///     return true;
///   }
/// \endcode
class ObjectMapper {
public:
  ObjectMapper(const Value &E) : O(E.getAsObject()) {}

  /// True if the expression is an object.
  /// Must be checked before calling map().
  operator bool() { return O; }

  /// Maps a property to a field, if it exists.
  template <typename T> bool map(StringRef Prop, T &Out) {
    assert(*this && "Must check this is an object before calling map()");
    if (const Value *E = O->get(Prop))
      return fromJSON(*E, Out);
    return false;
  }

  /// Maps a property to a field, if it exists.
  /// (Optional requires special handling, because missing keys are OK).
  template <typename T> bool map(StringRef Prop, llvm::Optional<T> &Out) {
    assert(*this && "Must check this is an object before calling map()");
    if (const Value *E = O->get(Prop))
      return fromJSON(*E, Out);
    Out = llvm::None;
    return true;
  }

private:
  const Object *O;
};

/// Parses the provided JSON source, or returns a ParseError.
/// The returned Value is self-contained and owns its strings (they do not refer
/// to the original source).
llvm::Expected<Value> parse(llvm::StringRef JSON);

class ParseError : public llvm::ErrorInfo<ParseError> {
  const char *Msg;
  unsigned Line, Column, Offset;

public:
  static char ID;
  ParseError(const char *Msg, unsigned Line, unsigned Column, unsigned Offset)
      : Msg(Msg), Line(Line), Column(Column), Offset(Offset) {}
  void log(llvm::raw_ostream &OS) const override {
    OS << llvm::formatv("[{0}:{1}, byte={2}]: {3}", Line, Column, Offset, Msg);
  }
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }
};
} // namespace json

/// Allow printing json::Value with formatv().
/// The default style is basic/compact formatting, like operator<<.
/// A format string like formatv("{0:2}", Value) pretty-prints with indent 2.
template <> struct format_provider<llvm::json::Value> {
  static void format(const llvm::json::Value &, raw_ostream &, StringRef);
};
} // namespace llvm

#endif
