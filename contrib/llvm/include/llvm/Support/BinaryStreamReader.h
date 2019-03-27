//===- BinaryStreamReader.h - Reads objects from a binary stream *- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BINARYSTREAMREADER_H
#define LLVM_SUPPORT_BINARYSTREAMREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/type_traits.h"

#include <string>
#include <type_traits>

namespace llvm {

/// Provides read only access to a subclass of `BinaryStream`.  Provides
/// bounds checking and helpers for writing certain common data types such as
/// null-terminated strings, integers in various flavors of endianness, etc.
/// Can be subclassed to provide reading of custom datatypes, although no
/// are overridable.
class BinaryStreamReader {
public:
  BinaryStreamReader() = default;
  explicit BinaryStreamReader(BinaryStreamRef Ref);
  explicit BinaryStreamReader(BinaryStream &Stream);
  explicit BinaryStreamReader(ArrayRef<uint8_t> Data,
                              llvm::support::endianness Endian);
  explicit BinaryStreamReader(StringRef Data, llvm::support::endianness Endian);

  BinaryStreamReader(const BinaryStreamReader &Other)
      : Stream(Other.Stream), Offset(Other.Offset) {}

  BinaryStreamReader &operator=(const BinaryStreamReader &Other) {
    Stream = Other.Stream;
    Offset = Other.Offset;
    return *this;
  }

  virtual ~BinaryStreamReader() {}

  /// Read as much as possible from the underlying string at the current offset
  /// without invoking a copy, and set \p Buffer to the resulting data slice.
  /// Updates the stream's offset to point after the newly read data.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readLongestContiguousChunk(ArrayRef<uint8_t> &Buffer);

  /// Read \p Size bytes from the underlying stream at the current offset and
  /// and set \p Buffer to the resulting data slice.  Whether a copy occurs
  /// depends on the implementation of the underlying stream.  Updates the
  /// stream's offset to point after the newly read data.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readBytes(ArrayRef<uint8_t> &Buffer, uint32_t Size);

  /// Read an integer of the specified endianness into \p Dest and update the
  /// stream's offset.  The data is always copied from the stream's underlying
  /// buffer into \p Dest. Updates the stream's offset to point after the newly
  /// read data.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T> Error readInteger(T &Dest) {
    static_assert(std::is_integral<T>::value,
                  "Cannot call readInteger with non-integral value!");

    ArrayRef<uint8_t> Bytes;
    if (auto EC = readBytes(Bytes, sizeof(T)))
      return EC;

    Dest = llvm::support::endian::read<T, llvm::support::unaligned>(
        Bytes.data(), Stream.getEndian());
    return Error::success();
  }

  /// Similar to readInteger.
  template <typename T> Error readEnum(T &Dest) {
    static_assert(std::is_enum<T>::value,
                  "Cannot call readEnum with non-enum value!");
    typename std::underlying_type<T>::type N;
    if (auto EC = readInteger(N))
      return EC;
    Dest = static_cast<T>(N);
    return Error::success();
  }

  /// Read a null terminated string from \p Dest.  Whether a copy occurs depends
  /// on the implementation of the underlying stream.  Updates the stream's
  /// offset to point after the newly read data.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readCString(StringRef &Dest);

  /// Similar to readCString, however read a null-terminated UTF16 string
  /// instead.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readWideString(ArrayRef<UTF16> &Dest);

  /// Read a \p Length byte string into \p Dest.  Whether a copy occurs depends
  /// on the implementation of the underlying stream.  Updates the stream's
  /// offset to point after the newly read data.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readFixedString(StringRef &Dest, uint32_t Length);

  /// Read the entire remainder of the underlying stream into \p Ref.  This is
  /// equivalent to calling getUnderlyingStream().slice(Offset).  Updates the
  /// stream's offset to point to the end of the stream.  Never causes a copy.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readStreamRef(BinaryStreamRef &Ref);

  /// Read \p Length bytes from the underlying stream into \p Ref.  This is
  /// equivalent to calling getUnderlyingStream().slice(Offset, Length).
  /// Updates the stream's offset to point after the newly read object.  Never
  /// causes a copy.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readStreamRef(BinaryStreamRef &Ref, uint32_t Length);

  /// Read \p Length bytes from the underlying stream into \p Stream.  This is
  /// equivalent to calling getUnderlyingStream().slice(Offset, Length).
  /// Updates the stream's offset to point after the newly read object.  Never
  /// causes a copy.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readSubstream(BinarySubstreamRef &Stream, uint32_t Size);

  /// Get a pointer to an object of type T from the underlying stream, as if by
  /// memcpy, and store the result into \p Dest.  It is up to the caller to
  /// ensure that objects of type T can be safely treated in this manner.
  /// Updates the stream's offset to point after the newly read object.  Whether
  /// a copy occurs depends upon the implementation of the underlying
  /// stream.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T> Error readObject(const T *&Dest) {
    ArrayRef<uint8_t> Buffer;
    if (auto EC = readBytes(Buffer, sizeof(T)))
      return EC;
    Dest = reinterpret_cast<const T *>(Buffer.data());
    return Error::success();
  }

  /// Get a reference to a \p NumElements element array of objects of type T
  /// from the underlying stream as if by memcpy, and store the resulting array
  /// slice into \p array.  It is up to the caller to ensure that objects of
  /// type T can be safely treated in this manner.  Updates the stream's offset
  /// to point after the newly read object.  Whether a copy occurs depends upon
  /// the implementation of the underlying stream.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T>
  Error readArray(ArrayRef<T> &Array, uint32_t NumElements) {
    ArrayRef<uint8_t> Bytes;
    if (NumElements == 0) {
      Array = ArrayRef<T>();
      return Error::success();
    }

    if (NumElements > UINT32_MAX / sizeof(T))
      return make_error<BinaryStreamError>(
          stream_error_code::invalid_array_size);

    if (auto EC = readBytes(Bytes, NumElements * sizeof(T)))
      return EC;

    assert(alignmentAdjustment(Bytes.data(), alignof(T)) == 0 &&
           "Reading at invalid alignment!");

    Array = ArrayRef<T>(reinterpret_cast<const T *>(Bytes.data()), NumElements);
    return Error::success();
  }

  /// Read a VarStreamArray of size \p Size bytes and store the result into
  /// \p Array.  Updates the stream's offset to point after the newly read
  /// array.  Never causes a copy (although iterating the elements of the
  /// VarStreamArray may, depending upon the implementation of the underlying
  /// stream).
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T, typename U>
  Error readArray(VarStreamArray<T, U> &Array, uint32_t Size,
                  uint32_t Skew = 0) {
    BinaryStreamRef S;
    if (auto EC = readStreamRef(S, Size))
      return EC;
    Array.setUnderlyingStream(S, Skew);
    return Error::success();
  }

  /// Read a FixedStreamArray of \p NumItems elements and store the result into
  /// \p Array.  Updates the stream's offset to point after the newly read
  /// array.  Never causes a copy (although iterating the elements of the
  /// FixedStreamArray may, depending upon the implementation of the underlying
  /// stream).
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T>
  Error readArray(FixedStreamArray<T> &Array, uint32_t NumItems) {
    if (NumItems == 0) {
      Array = FixedStreamArray<T>();
      return Error::success();
    }

    if (NumItems > UINT32_MAX / sizeof(T))
      return make_error<BinaryStreamError>(
          stream_error_code::invalid_array_size);

    BinaryStreamRef View;
    if (auto EC = readStreamRef(View, NumItems * sizeof(T)))
      return EC;

    Array = FixedStreamArray<T>(View);
    return Error::success();
  }

  bool empty() const { return bytesRemaining() == 0; }
  void setOffset(uint32_t Off) { Offset = Off; }
  uint32_t getOffset() const { return Offset; }
  uint32_t getLength() const { return Stream.getLength(); }
  uint32_t bytesRemaining() const { return getLength() - getOffset(); }

  /// Advance the stream's offset by \p Amount bytes.
  ///
  /// \returns a success error code if at least \p Amount bytes remain in the
  /// stream, otherwise returns an appropriate error code.
  Error skip(uint32_t Amount);

  /// Examine the next byte of the underlying stream without advancing the
  /// stream's offset.  If the stream is empty the behavior is undefined.
  ///
  /// \returns the next byte in the stream.
  uint8_t peek() const;

  Error padToAlignment(uint32_t Align);

  std::pair<BinaryStreamReader, BinaryStreamReader>
  split(uint32_t Offset) const;

private:
  BinaryStreamRef Stream;
  uint32_t Offset = 0;
};
} // namespace llvm

#endif // LLVM_SUPPORT_BINARYSTREAMREADER_H
