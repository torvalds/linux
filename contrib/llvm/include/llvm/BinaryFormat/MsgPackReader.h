//===- MsgPackReader.h - Simple MsgPack reader ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
///  \file
///  This is a MessagePack reader.
///
///  See https://github.com/msgpack/msgpack/blob/master/spec.md for the full
///  standard.
///
///  Typical usage:
///  \code
///  StringRef input = GetInput();
///  msgpack::Reader MPReader(input);
///  msgpack::Object Obj;
///
///  while (MPReader.read(Obj)) {
///    switch (Obj.Kind) {
///    case msgpack::Type::Int:
//       // Use Obj.Int
///      break;
///    // ...
///    }
///  }
///  \endcode
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MSGPACKREADER_H
#define LLVM_SUPPORT_MSGPACKREADER_H

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>

namespace llvm {
namespace msgpack {

/// MessagePack types as defined in the standard, with the exception of Integer
/// being divided into a signed Int and unsigned UInt variant in order to map
/// directly to C++ types.
///
/// The types map onto corresponding union members of the \c Object struct.
enum class Type : uint8_t {
  Int,
  UInt,
  Nil,
  Boolean,
  Float,
  String,
  Binary,
  Array,
  Map,
  Extension,
};

/// Extension types are composed of a user-defined type ID and an uninterpreted
/// sequence of bytes.
struct ExtensionType {
  /// User-defined extension type.
  int8_t Type;
  /// Raw bytes of the extension object.
  StringRef Bytes;
};

/// MessagePack object, represented as a tagged union of C++ types.
///
/// All types except \c Type::Nil (which has only one value, and so is
/// completely represented by the \c Kind itself) map to a exactly one union
/// member.
struct Object {
  Type Kind;
  union {
    /// Value for \c Type::Int.
    int64_t Int;
    /// Value for \c Type::Uint.
    uint64_t UInt;
    /// Value for \c Type::Boolean.
    bool Bool;
    /// Value for \c Type::Float.
    double Float;
    /// Value for \c Type::String and \c Type::Binary.
    StringRef Raw;
    /// Value for \c Type::Array and \c Type::Map.
    size_t Length;
    /// Value for \c Type::Extension.
    ExtensionType Extension;
  };

  Object() : Kind(Type::Int), Int(0) {}
};

/// Reads MessagePack objects from memory, one at a time.
class Reader {
public:
  /// Construct a reader, keeping a reference to the \p InputBuffer.
  Reader(MemoryBufferRef InputBuffer);
  /// Construct a reader, keeping a reference to the \p Input.
  Reader(StringRef Input);

  Reader(const Reader &) = delete;
  Reader &operator=(const Reader &) = delete;

  /// Read one object from the input buffer, advancing past it.
  ///
  /// The \p Obj is updated with the kind of the object read, and the
  /// corresponding union member is updated.
  ///
  /// For the collection objects (Array and Map), only the length is read, and
  /// the caller must make and additional \c N calls (in the case of Array) or
  /// \c N*2 calls (in the case of Map) to \c Read to retrieve the collection
  /// elements.
  ///
  /// \param [out] Obj filled with next object on success.
  ///
  /// \returns true when object successfully read, false when at end of
  /// input (and so \p Obj was not updated), otherwise an error.
  Expected<bool> read(Object &Obj);

private:
  MemoryBufferRef InputBuffer;
  StringRef::iterator Current;
  StringRef::iterator End;

  size_t remainingSpace() {
    // The rest of the code maintains the invariant that End >= Current, so
    // that this cast is always defined behavior.
    return static_cast<size_t>(End - Current);
  }

  template <class T> Expected<bool> readRaw(Object &Obj);
  template <class T> Expected<bool> readInt(Object &Obj);
  template <class T> Expected<bool> readUInt(Object &Obj);
  template <class T> Expected<bool> readLength(Object &Obj);
  template <class T> Expected<bool> readExt(Object &Obj);
  Expected<bool> createRaw(Object &Obj, uint32_t Size);
  Expected<bool> createExt(Object &Obj, uint32_t Size);
};

} // end namespace msgpack
} // end namespace llvm

#endif // LLVM_SUPPORT_MSGPACKREADER_H
