//===- RecordSerialization.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_RECORDSERIALIZATION_H
#define LLVM_DEBUGINFO_CODEVIEW_RECORDSERIALIZATION_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cinttypes>
#include <tuple>

namespace llvm {
namespace codeview {
using llvm::support::little32_t;
using llvm::support::ulittle16_t;
using llvm::support::ulittle32_t;

/// Limit on the size of all codeview symbol and type records, including the
/// RecordPrefix. MSVC does not emit any records larger than this.
enum : unsigned { MaxRecordLength = 0xFF00 };

struct RecordPrefix {
  ulittle16_t RecordLen;  // Record length, starting from &RecordKind.
  ulittle16_t RecordKind; // Record kind enum (SymRecordKind or TypeRecordKind)
};

/// Reinterpret a byte array as an array of characters. Does not interpret as
/// a C string, as StringRef has several helpers (split) that make that easy.
StringRef getBytesAsCharacters(ArrayRef<uint8_t> LeafData);
StringRef getBytesAsCString(ArrayRef<uint8_t> LeafData);

inline Error consume(BinaryStreamReader &Reader) { return Error::success(); }

/// Decodes a numeric "leaf" value. These are integer literals encountered in
/// the type stream. If the value is positive and less than LF_NUMERIC (1 <<
/// 15), it is emitted directly in Data. Otherwise, it has a tag like LF_CHAR
/// that indicates the bitwidth and sign of the numeric data.
Error consume(BinaryStreamReader &Reader, APSInt &Num);

/// Decodes a numeric leaf value that is known to be a particular type.
Error consume_numeric(BinaryStreamReader &Reader, uint64_t &Value);

/// Decodes signed and unsigned fixed-length integers.
Error consume(BinaryStreamReader &Reader, uint32_t &Item);
Error consume(BinaryStreamReader &Reader, int32_t &Item);

/// Decodes a null terminated string.
Error consume(BinaryStreamReader &Reader, StringRef &Item);

Error consume(StringRef &Data, APSInt &Num);
Error consume(StringRef &Data, uint32_t &Item);

/// Decodes an arbitrary object whose layout matches that of the underlying
/// byte sequence, and returns a pointer to the object.
template <typename T> Error consume(BinaryStreamReader &Reader, T *&Item) {
  return Reader.readObject(Item);
}

template <typename T, typename U> struct serialize_conditional_impl {
  serialize_conditional_impl(T &Item, U Func) : Item(Item), Func(Func) {}

  Error deserialize(BinaryStreamReader &Reader) const {
    if (!Func())
      return Error::success();
    return consume(Reader, Item);
  }

  T &Item;
  U Func;
};

template <typename T, typename U>
serialize_conditional_impl<T, U> serialize_conditional(T &Item, U Func) {
  return serialize_conditional_impl<T, U>(Item, Func);
}

template <typename T, typename U> struct serialize_array_impl {
  serialize_array_impl(ArrayRef<T> &Item, U Func) : Item(Item), Func(Func) {}

  Error deserialize(BinaryStreamReader &Reader) const {
    return Reader.readArray(Item, Func());
  }

  ArrayRef<T> &Item;
  U Func;
};

template <typename T> struct serialize_vector_tail_impl {
  serialize_vector_tail_impl(std::vector<T> &Item) : Item(Item) {}

  Error deserialize(BinaryStreamReader &Reader) const {
    T Field;
    // Stop when we run out of bytes or we hit record padding bytes.
    while (!Reader.empty() && Reader.peek() < LF_PAD0) {
      if (auto EC = consume(Reader, Field))
        return EC;
      Item.push_back(Field);
    }
    return Error::success();
  }

  std::vector<T> &Item;
};

struct serialize_null_term_string_array_impl {
  serialize_null_term_string_array_impl(std::vector<StringRef> &Item)
      : Item(Item) {}

  Error deserialize(BinaryStreamReader &Reader) const {
    if (Reader.empty())
      return make_error<CodeViewError>(cv_error_code::insufficient_buffer,
                                       "Null terminated string is empty!");

    while (Reader.peek() != 0) {
      StringRef Field;
      if (auto EC = Reader.readCString(Field))
        return EC;
      Item.push_back(Field);
    }
    return Reader.skip(1);
  }

  std::vector<StringRef> &Item;
};

template <typename T> struct serialize_arrayref_tail_impl {
  serialize_arrayref_tail_impl(ArrayRef<T> &Item) : Item(Item) {}

  Error deserialize(BinaryStreamReader &Reader) const {
    uint32_t Count = Reader.bytesRemaining() / sizeof(T);
    return Reader.readArray(Item, Count);
  }

  ArrayRef<T> &Item;
};

template <typename T> struct serialize_numeric_impl {
  serialize_numeric_impl(T &Item) : Item(Item) {}

  Error deserialize(BinaryStreamReader &Reader) const {
    return consume_numeric(Reader, Item);
  }

  T &Item;
};

template <typename T, typename U>
serialize_array_impl<T, U> serialize_array(ArrayRef<T> &Item, U Func) {
  return serialize_array_impl<T, U>(Item, Func);
}

inline serialize_null_term_string_array_impl
serialize_null_term_string_array(std::vector<StringRef> &Item) {
  return serialize_null_term_string_array_impl(Item);
}

template <typename T>
serialize_vector_tail_impl<T> serialize_array_tail(std::vector<T> &Item) {
  return serialize_vector_tail_impl<T>(Item);
}

template <typename T>
serialize_arrayref_tail_impl<T> serialize_array_tail(ArrayRef<T> &Item) {
  return serialize_arrayref_tail_impl<T>(Item);
}

template <typename T> serialize_numeric_impl<T> serialize_numeric(T &Item) {
  return serialize_numeric_impl<T>(Item);
}

template <typename T, typename U>
Error consume(BinaryStreamReader &Reader,
              const serialize_conditional_impl<T, U> &Item) {
  return Item.deserialize(Reader);
}

template <typename T, typename U>
Error consume(BinaryStreamReader &Reader,
              const serialize_array_impl<T, U> &Item) {
  return Item.deserialize(Reader);
}

inline Error consume(BinaryStreamReader &Reader,
                     const serialize_null_term_string_array_impl &Item) {
  return Item.deserialize(Reader);
}

template <typename T>
Error consume(BinaryStreamReader &Reader,
              const serialize_vector_tail_impl<T> &Item) {
  return Item.deserialize(Reader);
}

template <typename T>
Error consume(BinaryStreamReader &Reader,
              const serialize_arrayref_tail_impl<T> &Item) {
  return Item.deserialize(Reader);
}

template <typename T>
Error consume(BinaryStreamReader &Reader,
              const serialize_numeric_impl<T> &Item) {
  return Item.deserialize(Reader);
}

template <typename T, typename U, typename... Args>
Error consume(BinaryStreamReader &Reader, T &&X, U &&Y, Args &&... Rest) {
  if (auto EC = consume(Reader, X))
    return EC;
  return consume(Reader, Y, std::forward<Args>(Rest)...);
}

}
}

#endif
