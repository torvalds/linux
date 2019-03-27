//===- BinaryStreamWriter.h - Writes objects to a BinaryStream ---*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BINARYSTREAMWRITER_H
#define LLVM_SUPPORT_BINARYSTREAMWRITER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamError.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <type_traits>
#include <utility>

namespace llvm {

/// Provides write only access to a subclass of `WritableBinaryStream`.
/// Provides bounds checking and helpers for writing certain common data types
/// such as null-terminated strings, integers in various flavors of endianness,
/// etc.  Can be subclassed to provide reading and writing of custom datatypes,
/// although no methods are overridable.
class BinaryStreamWriter {
public:
  BinaryStreamWriter() = default;
  explicit BinaryStreamWriter(WritableBinaryStreamRef Ref);
  explicit BinaryStreamWriter(WritableBinaryStream &Stream);
  explicit BinaryStreamWriter(MutableArrayRef<uint8_t> Data,
                              llvm::support::endianness Endian);

  BinaryStreamWriter(const BinaryStreamWriter &Other)
      : Stream(Other.Stream), Offset(Other.Offset) {}

  BinaryStreamWriter &operator=(const BinaryStreamWriter &Other) {
    Stream = Other.Stream;
    Offset = Other.Offset;
    return *this;
  }

  virtual ~BinaryStreamWriter() {}

  /// Write the bytes specified in \p Buffer to the underlying stream.
  /// On success, updates the offset so that subsequent writes will occur
  /// at the next unwritten position.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  Error writeBytes(ArrayRef<uint8_t> Buffer);

  /// Write the integer \p Value to the underlying stream in the
  /// specified endianness.  On success, updates the offset so that
  /// subsequent writes occur at the next unwritten position.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  template <typename T> Error writeInteger(T Value) {
    static_assert(std::is_integral<T>::value,
                  "Cannot call writeInteger with non-integral value!");
    uint8_t Buffer[sizeof(T)];
    llvm::support::endian::write<T, llvm::support::unaligned>(
        Buffer, Value, Stream.getEndian());
    return writeBytes(Buffer);
  }

  /// Similar to writeInteger
  template <typename T> Error writeEnum(T Num) {
    static_assert(std::is_enum<T>::value,
                  "Cannot call writeEnum with non-Enum type");

    using U = typename std::underlying_type<T>::type;
    return writeInteger<U>(static_cast<U>(Num));
  }

  /// Write the string \p Str to the underlying stream followed by a null
  /// terminator.  On success, updates the offset so that subsequent writes
  /// occur at the next unwritten position.  \p Str need not be null terminated
  /// on input.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  Error writeCString(StringRef Str);

  /// Write the string \p Str to the underlying stream without a null
  /// terminator.  On success, updates the offset so that subsequent writes
  /// occur at the next unwritten position.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  Error writeFixedString(StringRef Str);

  /// Efficiently reads all data from \p Ref, and writes it to this stream.
  /// This operation will not invoke any copies of the source data, regardless
  /// of the source stream's implementation.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  Error writeStreamRef(BinaryStreamRef Ref);

  /// Efficiently reads \p Size bytes from \p Ref, and writes it to this stream.
  /// This operation will not invoke any copies of the source data, regardless
  /// of the source stream's implementation.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  Error writeStreamRef(BinaryStreamRef Ref, uint32_t Size);

  /// Writes the object \p Obj to the underlying stream, as if by using memcpy.
  /// It is up to the caller to ensure that type of \p Obj can be safely copied
  /// in this fashion, as no checks are made to ensure that this is safe.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  template <typename T> Error writeObject(const T &Obj) {
    static_assert(!std::is_pointer<T>::value,
                  "writeObject should not be used with pointers, to write "
                  "the pointed-to value dereference the pointer before calling "
                  "writeObject");
    return writeBytes(
        ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(&Obj), sizeof(T)));
  }

  /// Writes an array of objects of type T to the underlying stream, as if by
  /// using memcpy.  It is up to the caller to ensure that type of \p Obj can
  /// be safely copied in this fashion, as no checks are made to ensure that
  /// this is safe.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  template <typename T> Error writeArray(ArrayRef<T> Array) {
    if (Array.empty())
      return Error::success();
    if (Array.size() > UINT32_MAX / sizeof(T))
      return make_error<BinaryStreamError>(
          stream_error_code::invalid_array_size);

    return writeBytes(
        ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(Array.data()),
                          Array.size() * sizeof(T)));
  }

  /// Writes all data from the array \p Array to the underlying stream.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  template <typename T, typename U>
  Error writeArray(VarStreamArray<T, U> Array) {
    return writeStreamRef(Array.getUnderlyingStream());
  }

  /// Writes all elements from the array \p Array to the underlying stream.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  template <typename T> Error writeArray(FixedStreamArray<T> Array) {
    return writeStreamRef(Array.getUnderlyingStream());
  }

  /// Splits the Writer into two Writers at a given offset.
  std::pair<BinaryStreamWriter, BinaryStreamWriter> split(uint32_t Off) const;

  void setOffset(uint32_t Off) { Offset = Off; }
  uint32_t getOffset() const { return Offset; }
  uint32_t getLength() const { return Stream.getLength(); }
  uint32_t bytesRemaining() const { return getLength() - getOffset(); }
  Error padToAlignment(uint32_t Align);

protected:
  WritableBinaryStreamRef Stream;
  uint32_t Offset = 0;
};

} // end namespace llvm

#endif // LLVM_SUPPORT_BINARYSTREAMWRITER_H
