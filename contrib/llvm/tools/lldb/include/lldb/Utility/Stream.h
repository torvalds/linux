//===-- Stream.h ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Stream_h_
#define liblldb_Stream_h_

#include "lldb/Utility/Flags.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Stream Stream.h "lldb/Utility/Stream.h"
/// A stream class that can stream formatted output to a file.
//----------------------------------------------------------------------
class Stream {
public:
  //------------------------------------------------------------------
  /// \a m_flags bit values.
  //------------------------------------------------------------------
  enum {
    eBinary = (1 << 0) ///< Get and put data as binary instead of as the default
                       /// string mode.
  };

  /// Utility class for counting the bytes that were written to a stream in a
  /// certain time span.
  /// @example
  ///   ByteDelta delta(*this);
  ///   WriteDataToStream("foo");
  ///   return *delta;
  /// @endcode
  class ByteDelta {
    Stream *m_stream;
    /// Bytes we have written so far when ByteDelta was created.
    size_t m_start;

  public:
    ByteDelta(Stream &s) : m_stream(&s), m_start(s.GetWrittenBytes()) {}
    /// Returns the number of bytes written to the given Stream since this
    /// ByteDelta object was created.
    size_t operator*() const { return m_stream->GetWrittenBytes() - m_start; }
  };

  //------------------------------------------------------------------
  /// Construct with flags and address size and byte order.
  ///
  /// Construct with dump flags \a flags and the default address size. \a
  /// flags can be any of the above enumeration logical OR'ed together.
  //------------------------------------------------------------------
  Stream(uint32_t flags, uint32_t addr_size, lldb::ByteOrder byte_order);

  //------------------------------------------------------------------
  /// Construct a default Stream, not binary, host byte order and host addr
  /// size.
  ///
  //------------------------------------------------------------------
  Stream();

  // FIXME: Streams should not be copyable.
  Stream(const Stream &other) : m_forwarder(*this) { (*this) = other; }

  Stream &operator=(const Stream &rhs) {
    m_flags = rhs.m_flags;
    m_addr_size = rhs.m_addr_size;
    m_byte_order = rhs.m_byte_order;
    m_indent_level = rhs.m_indent_level;
    return *this;
  }

  //------------------------------------------------------------------
  /// Destructor
  //------------------------------------------------------------------
  virtual ~Stream();

  //------------------------------------------------------------------
  // Subclasses must override these methods
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Flush the stream.
  ///
  /// Subclasses should flush the stream to make any output appear if the
  /// stream has any buffering.
  //------------------------------------------------------------------
  virtual void Flush() = 0;

  //------------------------------------------------------------------
  /// Output character bytes to the stream.
  ///
  /// Appends \a src_len characters from the buffer \a src to the stream.
  ///
  /// @param[in] src
  ///     A buffer containing at least \a src_len bytes of data.
  ///
  /// @param[in] src_len
  ///     A number of bytes to append to the stream.
  ///
  /// @return
  ///     The number of bytes that were appended to the stream.
  //------------------------------------------------------------------
  size_t Write(const void *src, size_t src_len) {
    size_t appended_byte_count = WriteImpl(src, src_len);
    m_bytes_written += appended_byte_count;
    return appended_byte_count;
  }

  size_t GetWrittenBytes() const { return m_bytes_written; }

  //------------------------------------------------------------------
  // Member functions
  //------------------------------------------------------------------
  size_t PutChar(char ch);

  //------------------------------------------------------------------
  /// Set the byte_order value.
  ///
  /// Sets the byte order of the data to extract. Extracted values will be
  /// swapped if necessary when decoding.
  ///
  /// @param[in] byte_order
  ///     The byte order value to use when extracting data.
  ///
  /// @return
  ///     The old byte order value.
  //------------------------------------------------------------------
  lldb::ByteOrder SetByteOrder(lldb::ByteOrder byte_order);

  //------------------------------------------------------------------
  /// Format a C string from a printf style format and variable arguments and
  /// encode and append the resulting C string as hex bytes.
  ///
  /// @param[in] format
  ///     A printf style format string.
  ///
  /// @param[in] ...
  ///     Any additional arguments needed for the printf format string.
  ///
  /// @return
  ///     The number of bytes that were appended to the stream.
  //------------------------------------------------------------------
  size_t PrintfAsRawHex8(const char *format, ...)
      __attribute__((__format__(__printf__, 2, 3)));

  //------------------------------------------------------------------
  /// Append an uint8_t value in the hexadecimal format to the stream.
  ///
  /// @param[in] uvalue
  ///     The value to append.
  ///
  /// @return
  ///     The number of bytes that were appended to the stream.
  //------------------------------------------------------------------
  size_t PutHex8(uint8_t uvalue);

  size_t PutNHex8(size_t n, uint8_t uvalue);

  size_t PutHex16(uint16_t uvalue,
                  lldb::ByteOrder byte_order = lldb::eByteOrderInvalid);

  size_t PutHex32(uint32_t uvalue,
                  lldb::ByteOrder byte_order = lldb::eByteOrderInvalid);

  size_t PutHex64(uint64_t uvalue,
                  lldb::ByteOrder byte_order = lldb::eByteOrderInvalid);

  size_t PutMaxHex64(uint64_t uvalue, size_t byte_size,
                     lldb::ByteOrder byte_order = lldb::eByteOrderInvalid);
  size_t PutFloat(float f,
                  lldb::ByteOrder byte_order = lldb::eByteOrderInvalid);

  size_t PutDouble(double d,
                   lldb::ByteOrder byte_order = lldb::eByteOrderInvalid);

  size_t PutLongDouble(long double ld,
                       lldb::ByteOrder byte_order = lldb::eByteOrderInvalid);

  size_t PutPointer(void *ptr);

  // Append \a src_len bytes from \a src to the stream as hex characters (two
  // ascii characters per byte of input data)
  size_t
  PutBytesAsRawHex8(const void *src, size_t src_len,
                    lldb::ByteOrder src_byte_order = lldb::eByteOrderInvalid,
                    lldb::ByteOrder dst_byte_order = lldb::eByteOrderInvalid);

  // Append \a src_len bytes from \a s to the stream as binary data.
  size_t PutRawBytes(const void *s, size_t src_len,
                     lldb::ByteOrder src_byte_order = lldb::eByteOrderInvalid,
                     lldb::ByteOrder dst_byte_order = lldb::eByteOrderInvalid);

  size_t PutCStringAsRawHex8(const char *s);

  //------------------------------------------------------------------
  /// Output a NULL terminated C string \a cstr to the stream \a s.
  ///
  /// @param[in] cstr
  ///     A NULL terminated C string.
  ///
  /// @return
  ///     A reference to this class so multiple things can be streamed
  ///     in one statement.
  //------------------------------------------------------------------
  Stream &operator<<(const char *cstr);

  Stream &operator<<(llvm::StringRef str);

  //------------------------------------------------------------------
  /// Output a pointer value \a p to the stream \a s.
  ///
  /// @param[in] p
  ///     A void pointer.
  ///
  /// @return
  ///     A reference to this class so multiple things can be streamed
  ///     in one statement.
  //------------------------------------------------------------------
  Stream &operator<<(const void *p);

  //------------------------------------------------------------------
  /// Output a character \a ch to the stream \a s.
  ///
  /// @param[in] ch
  ///     A printable character value.
  ///
  /// @return
  ///     A reference to this class so multiple things can be streamed
  ///     in one statement.
  //------------------------------------------------------------------
  Stream &operator<<(char ch);

  //------------------------------------------------------------------
  /// Output a uint8_t \a uval to the stream \a s.
  ///
  /// @param[in] uval
  ///     A uint8_t value.
  ///
  /// @return
  ///     A reference to this class so multiple things can be streamed
  ///     in one statement.
  //------------------------------------------------------------------
  Stream &operator<<(uint8_t uval);

  //------------------------------------------------------------------
  /// Output a uint16_t \a uval to the stream \a s.
  ///
  /// @param[in] uval
  ///     A uint16_t value.
  ///
  /// @return
  ///     A reference to this class so multiple things can be streamed
  ///     in one statement.
  //------------------------------------------------------------------
  Stream &operator<<(uint16_t uval);

  //------------------------------------------------------------------
  /// Output a uint32_t \a uval to the stream \a s.
  ///
  /// @param[in] uval
  ///     A uint32_t value.
  ///
  /// @return
  ///     A reference to this class so multiple things can be streamed
  ///     in one statement.
  //------------------------------------------------------------------
  Stream &operator<<(uint32_t uval);

  //------------------------------------------------------------------
  /// Output a uint64_t \a uval to the stream \a s.
  ///
  /// @param[in] uval
  ///     A uint64_t value.
  ///
  /// @return
  ///     A reference to this class so multiple things can be streamed
  ///     in one statement.
  //------------------------------------------------------------------
  Stream &operator<<(uint64_t uval);

  //------------------------------------------------------------------
  /// Output a int8_t \a sval to the stream \a s.
  ///
  /// @param[in] sval
  ///     A int8_t value.
  ///
  /// @return
  ///     A reference to this class so multiple things can be streamed
  ///     in one statement.
  //------------------------------------------------------------------
  Stream &operator<<(int8_t sval);

  //------------------------------------------------------------------
  /// Output a int16_t \a sval to the stream \a s.
  ///
  /// @param[in] sval
  ///     A int16_t value.
  ///
  /// @return
  ///     A reference to this class so multiple things can be streamed
  ///     in one statement.
  //------------------------------------------------------------------
  Stream &operator<<(int16_t sval);

  //------------------------------------------------------------------
  /// Output a int32_t \a sval to the stream \a s.
  ///
  /// @param[in] sval
  ///     A int32_t value.
  ///
  /// @return
  ///     A reference to this class so multiple things can be streamed
  ///     in one statement.
  //------------------------------------------------------------------
  Stream &operator<<(int32_t sval);

  //------------------------------------------------------------------
  /// Output a int64_t \a sval to the stream \a s.
  ///
  /// @param[in] sval
  ///     A int64_t value.
  ///
  /// @return
  ///     A reference to this class so multiple things can be streamed
  ///     in one statement.
  //------------------------------------------------------------------
  Stream &operator<<(int64_t sval);

  //------------------------------------------------------------------
  /// Output an address value to this stream.
  ///
  /// Put an address \a addr out to the stream with optional \a prefix and \a
  /// suffix strings.
  ///
  /// @param[in] addr
  ///     An address value.
  ///
  /// @param[in] addr_size
  ///     Size in bytes of the address, used for formatting.
  ///
  /// @param[in] prefix
  ///     A prefix C string. If nullptr, no prefix will be output.
  ///
  /// @param[in] suffix
  ///     A suffix C string. If nullptr, no suffix will be output.
  //------------------------------------------------------------------
  void Address(uint64_t addr, uint32_t addr_size, const char *prefix = nullptr,
               const char *suffix = nullptr);

  //------------------------------------------------------------------
  /// Output an address range to this stream.
  ///
  /// Put an address range \a lo_addr - \a hi_addr out to the stream with
  /// optional \a prefix and \a suffix strings.
  ///
  /// @param[in] lo_addr
  ///     The start address of the address range.
  ///
  /// @param[in] hi_addr
  ///     The end address of the address range.
  ///
  /// @param[in] addr_size
  ///     Size in bytes of the address, used for formatting.
  ///
  /// @param[in] prefix
  ///     A prefix C string. If nullptr, no prefix will be output.
  ///
  /// @param[in] suffix
  ///     A suffix C string. If nullptr, no suffix will be output.
  //------------------------------------------------------------------
  void AddressRange(uint64_t lo_addr, uint64_t hi_addr, uint32_t addr_size,
                    const char *prefix = nullptr, const char *suffix = nullptr);

  //------------------------------------------------------------------
  /// Output a C string to the stream.
  ///
  /// Print a C string \a cstr to the stream.
  ///
  /// @param[in] cstr
  ///     The string to be output to the stream.
  //------------------------------------------------------------------
  size_t PutCString(llvm::StringRef cstr);

  //------------------------------------------------------------------
  /// Output and End of Line character to the stream.
  //------------------------------------------------------------------
  size_t EOL();

  //------------------------------------------------------------------
  /// Get the address size in bytes.
  ///
  /// @return
  ///     The size of an address in bytes that is used when outputting
  ///     address and pointer values to the stream.
  //------------------------------------------------------------------
  uint32_t GetAddressByteSize() const;

  //------------------------------------------------------------------
  /// The flags accessor.
  ///
  /// @return
  ///     A reference to the Flags member variable.
  //------------------------------------------------------------------
  Flags &GetFlags();

  //------------------------------------------------------------------
  /// The flags const accessor.
  ///
  /// @return
  ///     A const reference to the Flags member variable.
  //------------------------------------------------------------------
  const Flags &GetFlags() const;

  //------------------------------------------------------------------
  //// The byte order accessor.
  ////
  //// @return
  ////     The byte order.
  //------------------------------------------------------------------
  lldb::ByteOrder GetByteOrder() const;

  //------------------------------------------------------------------
  /// Get the current indentation level.
  ///
  /// @return
  ///     The current indentation level as an integer.
  //------------------------------------------------------------------
  int GetIndentLevel() const;

  //------------------------------------------------------------------
  /// Indent the current line in the stream.
  ///
  /// Indent the current line using the current indentation level and print an
  /// optional string following the indentation spaces.
  ///
  /// @param[in] s
  ///     A C string to print following the indentation. If nullptr, just
  ///     output the indentation characters.
  //------------------------------------------------------------------
  size_t Indent(const char *s = nullptr);
  size_t Indent(llvm::StringRef s);

  //------------------------------------------------------------------
  /// Decrement the current indentation level.
  //------------------------------------------------------------------
  void IndentLess(int amount = 2);

  //------------------------------------------------------------------
  /// Increment the current indentation level.
  //------------------------------------------------------------------
  void IndentMore(int amount = 2);

  //------------------------------------------------------------------
  /// Output an offset value.
  ///
  /// Put an offset \a uval out to the stream using the printf format in \a
  /// format.
  ///
  /// @param[in] offset
  ///     The offset value.
  ///
  /// @param[in] format
  ///     The printf style format to use when outputting the offset.
  //------------------------------------------------------------------
  void Offset(uint32_t offset, const char *format = "0x%8.8x: ");

  //------------------------------------------------------------------
  /// Output printf formatted output to the stream.
  ///
  /// Print some formatted output to the stream.
  ///
  /// @param[in] format
  ///     A printf style format string.
  ///
  /// @param[in] ...
  ///     Variable arguments that are needed for the printf style
  ///     format string \a format.
  //------------------------------------------------------------------
  size_t Printf(const char *format, ...) __attribute__((format(printf, 2, 3)));

  size_t PrintfVarArg(const char *format, va_list args);

  template <typename... Args> void Format(const char *format, Args &&... args) {
    PutCString(llvm::formatv(format, std::forward<Args>(args)...).str());
  }

  //------------------------------------------------------------------
  /// Output a quoted C string value to the stream.
  ///
  /// Print a double quoted NULL terminated C string to the stream using the
  /// printf format in \a format.
  ///
  /// @param[in] cstr
  ///     A NULL terminated C string value.
  ///
  /// @param[in] format
  ///     The optional C string format that can be overridden.
  //------------------------------------------------------------------
  void QuotedCString(const char *cstr, const char *format = "\"%s\"");

  //------------------------------------------------------------------
  /// Set the address size in bytes.
  ///
  /// @param[in] addr_size
  ///     The new size in bytes of an address to use when outputting
  ///     address and pointer values.
  //------------------------------------------------------------------
  void SetAddressByteSize(uint32_t addr_size);

  //------------------------------------------------------------------
  /// Set the current indentation level.
  ///
  /// @param[in] level
  ///     The new indentation level.
  //------------------------------------------------------------------
  void SetIndentLevel(int level);

  //------------------------------------------------------------------
  /// Output a SLEB128 number to the stream.
  ///
  /// Put an SLEB128 \a uval out to the stream using the printf format in \a
  /// format.
  ///
  /// @param[in] uval
  ///     A uint64_t value that was extracted as a SLEB128 value.
  //------------------------------------------------------------------
  size_t PutSLEB128(int64_t uval);

  //------------------------------------------------------------------
  /// Output a ULEB128 number to the stream.
  ///
  /// Put an ULEB128 \a uval out to the stream using the printf format in \a
  /// format.
  ///
  /// @param[in] uval
  ///     A uint64_t value that was extracted as a ULEB128 value.
  //------------------------------------------------------------------
  size_t PutULEB128(uint64_t uval);

  //------------------------------------------------------------------
  /// Returns a raw_ostream that forwards the data to this Stream object.
  //------------------------------------------------------------------
  llvm::raw_ostream &AsRawOstream() {
    return m_forwarder;
  }

protected:
  //------------------------------------------------------------------
  // Member variables
  //------------------------------------------------------------------
  Flags m_flags;        ///< Dump flags.
  uint32_t m_addr_size; ///< Size of an address in bytes.
  lldb::ByteOrder
      m_byte_order;   ///< Byte order to use when encoding scalar types.
  int m_indent_level; ///< Indention level.
  std::size_t m_bytes_written = 0; ///< Number of bytes written so far.

  void _PutHex8(uint8_t uvalue, bool add_prefix);

  //------------------------------------------------------------------
  /// Output character bytes to the stream.
  ///
  /// Appends \a src_len characters from the buffer \a src to the stream.
  ///
  /// @param[in] src
  ///     A buffer containing at least \a src_len bytes of data.
  ///
  /// @param[in] src_len
  ///     A number of bytes to append to the stream.
  ///
  /// @return
  ///     The number of bytes that were appended to the stream.
  //------------------------------------------------------------------
  virtual size_t WriteImpl(const void *src, size_t src_len) = 0;

  //----------------------------------------------------------------------
  /// @class RawOstreamForward Stream.h "lldb/Utility/Stream.h"
  /// This is a wrapper class that exposes a raw_ostream interface that just
  /// forwards to an LLDB stream, allowing to reuse LLVM algorithms that take
  /// a raw_ostream within the LLDB code base.
  //----------------------------------------------------------------------
  class RawOstreamForward : public llvm::raw_ostream {
    // Note: This stream must *not* maintain its own buffer, but instead
    // directly write everything to the internal Stream class. Without this,
    // we would run into the problem that the Stream written byte count would
    // differ from the actually written bytes by the size of the internal
    // raw_ostream buffer.

    Stream &m_target;
    void write_impl(const char *Ptr, size_t Size) override {
      m_target.Write(Ptr, Size);
    }

    uint64_t current_pos() const override {
      return m_target.GetWrittenBytes();
    }

  public:
    RawOstreamForward(Stream &target)
        : llvm::raw_ostream(/*unbuffered*/ true), m_target(target) {}
  };
  RawOstreamForward m_forwarder;
};

} // namespace lldb_private

#endif // liblldb_Stream_h_
