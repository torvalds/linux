//===--- raw_ostream.h - Raw output stream ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the raw_ostream class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RAW_OSTREAM_H
#define LLVM_SUPPORT_RAW_OSTREAM_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <system_error>

namespace llvm {

class formatv_object_base;
class format_object_base;
class FormattedString;
class FormattedNumber;
class FormattedBytes;

namespace sys {
namespace fs {
enum FileAccess : unsigned;
enum OpenFlags : unsigned;
enum CreationDisposition : unsigned;
} // end namespace fs
} // end namespace sys

/// This class implements an extremely fast bulk output stream that can *only*
/// output to a stream.  It does not support seeking, reopening, rewinding, line
/// buffered disciplines etc. It is a simple buffer that outputs
/// a chunk at a time.
class raw_ostream {
private:
  /// The buffer is handled in such a way that the buffer is
  /// uninitialized, unbuffered, or out of space when OutBufCur >=
  /// OutBufEnd. Thus a single comparison suffices to determine if we
  /// need to take the slow path to write a single character.
  ///
  /// The buffer is in one of three states:
  ///  1. Unbuffered (BufferMode == Unbuffered)
  ///  1. Uninitialized (BufferMode != Unbuffered && OutBufStart == 0).
  ///  2. Buffered (BufferMode != Unbuffered && OutBufStart != 0 &&
  ///               OutBufEnd - OutBufStart >= 1).
  ///
  /// If buffered, then the raw_ostream owns the buffer if (BufferMode ==
  /// InternalBuffer); otherwise the buffer has been set via SetBuffer and is
  /// managed by the subclass.
  ///
  /// If a subclass installs an external buffer using SetBuffer then it can wait
  /// for a \see write_impl() call to handle the data which has been put into
  /// this buffer.
  char *OutBufStart, *OutBufEnd, *OutBufCur;

  enum BufferKind {
    Unbuffered = 0,
    InternalBuffer,
    ExternalBuffer
  } BufferMode;

public:
  // color order matches ANSI escape sequence, don't change
  enum Colors {
    BLACK = 0,
    RED,
    GREEN,
    YELLOW,
    BLUE,
    MAGENTA,
    CYAN,
    WHITE,
    SAVEDCOLOR
  };

  explicit raw_ostream(bool unbuffered = false)
      : BufferMode(unbuffered ? Unbuffered : InternalBuffer) {
    // Start out ready to flush.
    OutBufStart = OutBufEnd = OutBufCur = nullptr;
  }

  raw_ostream(const raw_ostream &) = delete;
  void operator=(const raw_ostream &) = delete;

  virtual ~raw_ostream();

  /// tell - Return the current offset with the file.
  uint64_t tell() const { return current_pos() + GetNumBytesInBuffer(); }

  //===--------------------------------------------------------------------===//
  // Configuration Interface
  //===--------------------------------------------------------------------===//

  /// Set the stream to be buffered, with an automatically determined buffer
  /// size.
  void SetBuffered();

  /// Set the stream to be buffered, using the specified buffer size.
  void SetBufferSize(size_t Size) {
    flush();
    SetBufferAndMode(new char[Size], Size, InternalBuffer);
  }

  size_t GetBufferSize() const {
    // If we're supposed to be buffered but haven't actually gotten around
    // to allocating the buffer yet, return the value that would be used.
    if (BufferMode != Unbuffered && OutBufStart == nullptr)
      return preferred_buffer_size();

    // Otherwise just return the size of the allocated buffer.
    return OutBufEnd - OutBufStart;
  }

  /// Set the stream to be unbuffered. When unbuffered, the stream will flush
  /// after every write. This routine will also flush the buffer immediately
  /// when the stream is being set to unbuffered.
  void SetUnbuffered() {
    flush();
    SetBufferAndMode(nullptr, 0, Unbuffered);
  }

  size_t GetNumBytesInBuffer() const {
    return OutBufCur - OutBufStart;
  }

  //===--------------------------------------------------------------------===//
  // Data Output Interface
  //===--------------------------------------------------------------------===//

  void flush() {
    if (OutBufCur != OutBufStart)
      flush_nonempty();
  }

  raw_ostream &operator<<(char C) {
    if (OutBufCur >= OutBufEnd)
      return write(C);
    *OutBufCur++ = C;
    return *this;
  }

  raw_ostream &operator<<(unsigned char C) {
    if (OutBufCur >= OutBufEnd)
      return write(C);
    *OutBufCur++ = C;
    return *this;
  }

  raw_ostream &operator<<(signed char C) {
    if (OutBufCur >= OutBufEnd)
      return write(C);
    *OutBufCur++ = C;
    return *this;
  }

  raw_ostream &operator<<(StringRef Str) {
    // Inline fast path, particularly for strings with a known length.
    size_t Size = Str.size();

    // Make sure we can use the fast path.
    if (Size > (size_t)(OutBufEnd - OutBufCur))
      return write(Str.data(), Size);

    if (Size) {
      memcpy(OutBufCur, Str.data(), Size);
      OutBufCur += Size;
    }
    return *this;
  }

  raw_ostream &operator<<(const char *Str) {
    // Inline fast path, particularly for constant strings where a sufficiently
    // smart compiler will simplify strlen.

    return this->operator<<(StringRef(Str));
  }

  raw_ostream &operator<<(const std::string &Str) {
    // Avoid the fast path, it would only increase code size for a marginal win.
    return write(Str.data(), Str.length());
  }

  raw_ostream &operator<<(const SmallVectorImpl<char> &Str) {
    return write(Str.data(), Str.size());
  }

  raw_ostream &operator<<(unsigned long N);
  raw_ostream &operator<<(long N);
  raw_ostream &operator<<(unsigned long long N);
  raw_ostream &operator<<(long long N);
  raw_ostream &operator<<(const void *P);

  raw_ostream &operator<<(unsigned int N) {
    return this->operator<<(static_cast<unsigned long>(N));
  }

  raw_ostream &operator<<(int N) {
    return this->operator<<(static_cast<long>(N));
  }

  raw_ostream &operator<<(double N);

  /// Output \p N in hexadecimal, without any prefix or padding.
  raw_ostream &write_hex(unsigned long long N);

  /// Output a formatted UUID with dash separators.
  using uuid_t = uint8_t[16];
  raw_ostream &write_uuid(const uuid_t UUID);

  /// Output \p Str, turning '\\', '\t', '\n', '"', and anything that doesn't
  /// satisfy llvm::isPrint into an escape sequence.
  raw_ostream &write_escaped(StringRef Str, bool UseHexEscapes = false);

  raw_ostream &write(unsigned char C);
  raw_ostream &write(const char *Ptr, size_t Size);

  // Formatted output, see the format() function in Support/Format.h.
  raw_ostream &operator<<(const format_object_base &Fmt);

  // Formatted output, see the leftJustify() function in Support/Format.h.
  raw_ostream &operator<<(const FormattedString &);

  // Formatted output, see the formatHex() function in Support/Format.h.
  raw_ostream &operator<<(const FormattedNumber &);

  // Formatted output, see the formatv() function in Support/FormatVariadic.h.
  raw_ostream &operator<<(const formatv_object_base &);

  // Formatted output, see the format_bytes() function in Support/Format.h.
  raw_ostream &operator<<(const FormattedBytes &);

  /// indent - Insert 'NumSpaces' spaces.
  raw_ostream &indent(unsigned NumSpaces);

  /// write_zeros - Insert 'NumZeros' nulls.
  raw_ostream &write_zeros(unsigned NumZeros);

  /// Changes the foreground color of text that will be output from this point
  /// forward.
  /// @param Color ANSI color to use, the special SAVEDCOLOR can be used to
  /// change only the bold attribute, and keep colors untouched
  /// @param Bold bold/brighter text, default false
  /// @param BG if true change the background, default: change foreground
  /// @returns itself so it can be used within << invocations
  virtual raw_ostream &changeColor(enum Colors Color,
                                   bool Bold = false,
                                   bool BG = false) {
    (void)Color;
    (void)Bold;
    (void)BG;
    return *this;
  }

  /// Resets the colors to terminal defaults. Call this when you are done
  /// outputting colored text, or before program exit.
  virtual raw_ostream &resetColor() { return *this; }

  /// Reverses the foreground and background colors.
  virtual raw_ostream &reverseColor() { return *this; }

  /// This function determines if this stream is connected to a "tty" or
  /// "console" window. That is, the output would be displayed to the user
  /// rather than being put on a pipe or stored in a file.
  virtual bool is_displayed() const { return false; }

  /// This function determines if this stream is displayed and supports colors.
  virtual bool has_colors() const { return is_displayed(); }

  //===--------------------------------------------------------------------===//
  // Subclass Interface
  //===--------------------------------------------------------------------===//

private:
  /// The is the piece of the class that is implemented by subclasses.  This
  /// writes the \p Size bytes starting at
  /// \p Ptr to the underlying stream.
  ///
  /// This function is guaranteed to only be called at a point at which it is
  /// safe for the subclass to install a new buffer via SetBuffer.
  ///
  /// \param Ptr The start of the data to be written. For buffered streams this
  /// is guaranteed to be the start of the buffer.
  ///
  /// \param Size The number of bytes to be written.
  ///
  /// \invariant { Size > 0 }
  virtual void write_impl(const char *Ptr, size_t Size) = 0;

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  virtual uint64_t current_pos() const = 0;

protected:
  /// Use the provided buffer as the raw_ostream buffer. This is intended for
  /// use only by subclasses which can arrange for the output to go directly
  /// into the desired output buffer, instead of being copied on each flush.
  void SetBuffer(char *BufferStart, size_t Size) {
    SetBufferAndMode(BufferStart, Size, ExternalBuffer);
  }

  /// Return an efficient buffer size for the underlying output mechanism.
  virtual size_t preferred_buffer_size() const;

  /// Return the beginning of the current stream buffer, or 0 if the stream is
  /// unbuffered.
  const char *getBufferStart() const { return OutBufStart; }

  //===--------------------------------------------------------------------===//
  // Private Interface
  //===--------------------------------------------------------------------===//
private:
  /// Install the given buffer and mode.
  void SetBufferAndMode(char *BufferStart, size_t Size, BufferKind Mode);

  /// Flush the current buffer, which is known to be non-empty. This outputs the
  /// currently buffered data and resets the buffer to empty.
  void flush_nonempty();

  /// Copy data into the buffer. Size must not be greater than the number of
  /// unused bytes in the buffer.
  void copy_to_buffer(const char *Ptr, size_t Size);

  virtual void anchor();
};

/// An abstract base class for streams implementations that also support a
/// pwrite operation. This is useful for code that can mostly stream out data,
/// but needs to patch in a header that needs to know the output size.
class raw_pwrite_stream : public raw_ostream {
  virtual void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) = 0;
  void anchor() override;

public:
  explicit raw_pwrite_stream(bool Unbuffered = false)
      : raw_ostream(Unbuffered) {}
  void pwrite(const char *Ptr, size_t Size, uint64_t Offset) {
#ifndef NDBEBUG
    uint64_t Pos = tell();
    // /dev/null always reports a pos of 0, so we cannot perform this check
    // in that case.
    if (Pos)
      assert(Size + Offset <= Pos && "We don't support extending the stream");
#endif
    pwrite_impl(Ptr, Size, Offset);
  }
};

//===----------------------------------------------------------------------===//
// File Output Streams
//===----------------------------------------------------------------------===//

/// A raw_ostream that writes to a file descriptor.
///
class raw_fd_ostream : public raw_pwrite_stream {
  int FD;
  bool ShouldClose;

  bool SupportsSeeking;

#ifdef _WIN32
  /// True if this fd refers to a Windows console device. Mintty and other
  /// terminal emulators are TTYs, but they are not consoles.
  bool IsWindowsConsole = false;
#endif

  std::error_code EC;

  uint64_t pos;

  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override;

  void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) override;

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  uint64_t current_pos() const override { return pos; }

  /// Determine an efficient buffer size.
  size_t preferred_buffer_size() const override;

  /// Set the flag indicating that an output error has been encountered.
  void error_detected(std::error_code EC) { this->EC = EC; }

  void anchor() override;

public:
  /// Open the specified file for writing. If an error occurs, information
  /// about the error is put into EC, and the stream should be immediately
  /// destroyed;
  /// \p Flags allows optional flags to control how the file will be opened.
  ///
  /// As a special case, if Filename is "-", then the stream will use
  /// STDOUT_FILENO instead of opening a file. This will not close the stdout
  /// descriptor.
  raw_fd_ostream(StringRef Filename, std::error_code &EC);
  raw_fd_ostream(StringRef Filename, std::error_code &EC,
                 sys::fs::CreationDisposition Disp);
  raw_fd_ostream(StringRef Filename, std::error_code &EC,
                 sys::fs::FileAccess Access);
  raw_fd_ostream(StringRef Filename, std::error_code &EC,
                 sys::fs::OpenFlags Flags);
  raw_fd_ostream(StringRef Filename, std::error_code &EC,
                 sys::fs::CreationDisposition Disp, sys::fs::FileAccess Access,
                 sys::fs::OpenFlags Flags);

  /// FD is the file descriptor that this writes to.  If ShouldClose is true,
  /// this closes the file when the stream is destroyed. If FD is for stdout or
  /// stderr, it will not be closed.
  raw_fd_ostream(int fd, bool shouldClose, bool unbuffered=false);

  ~raw_fd_ostream() override;

  /// Manually flush the stream and close the file. Note that this does not call
  /// fsync.
  void close();

  bool supportsSeeking() { return SupportsSeeking; }

  /// Flushes the stream and repositions the underlying file descriptor position
  /// to the offset specified from the beginning of the file.
  uint64_t seek(uint64_t off);

  raw_ostream &changeColor(enum Colors colors, bool bold=false,
                           bool bg=false) override;
  raw_ostream &resetColor() override;

  raw_ostream &reverseColor() override;

  bool is_displayed() const override;

  bool has_colors() const override;

  std::error_code error() const { return EC; }

  /// Return the value of the flag in this raw_fd_ostream indicating whether an
  /// output error has been encountered.
  /// This doesn't implicitly flush any pending output.  Also, it doesn't
  /// guarantee to detect all errors unless the stream has been closed.
  bool has_error() const { return bool(EC); }

  /// Set the flag read by has_error() to false. If the error flag is set at the
  /// time when this raw_ostream's destructor is called, report_fatal_error is
  /// called to report the error. Use clear_error() after handling the error to
  /// avoid this behavior.
  ///
  ///   "Errors should never pass silently.
  ///    Unless explicitly silenced."
  ///      - from The Zen of Python, by Tim Peters
  ///
  void clear_error() { EC = std::error_code(); }
};

/// This returns a reference to a raw_ostream for standard output. Use it like:
/// outs() << "foo" << "bar";
raw_ostream &outs();

/// This returns a reference to a raw_ostream for standard error. Use it like:
/// errs() << "foo" << "bar";
raw_ostream &errs();

/// This returns a reference to a raw_ostream which simply discards output.
raw_ostream &nulls();

//===----------------------------------------------------------------------===//
// Output Stream Adaptors
//===----------------------------------------------------------------------===//

/// A raw_ostream that writes to an std::string.  This is a simple adaptor
/// class. This class does not encounter output errors.
class raw_string_ostream : public raw_ostream {
  std::string &OS;

  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override;

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  uint64_t current_pos() const override { return OS.size(); }

public:
  explicit raw_string_ostream(std::string &O) : OS(O) {}
  ~raw_string_ostream() override;

  /// Flushes the stream contents to the target string and returns  the string's
  /// reference.
  std::string& str() {
    flush();
    return OS;
  }
};

/// A raw_ostream that writes to an SmallVector or SmallString.  This is a
/// simple adaptor class. This class does not encounter output errors.
/// raw_svector_ostream operates without a buffer, delegating all memory
/// management to the SmallString. Thus the SmallString is always up-to-date,
/// may be used directly and there is no need to call flush().
class raw_svector_ostream : public raw_pwrite_stream {
  SmallVectorImpl<char> &OS;

  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override;

  void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) override;

  /// Return the current position within the stream.
  uint64_t current_pos() const override;

public:
  /// Construct a new raw_svector_ostream.
  ///
  /// \param O The vector to write to; this should generally have at least 128
  /// bytes free to avoid any extraneous memory overhead.
  explicit raw_svector_ostream(SmallVectorImpl<char> &O) : OS(O) {
    SetUnbuffered();
  }

  ~raw_svector_ostream() override = default;

  void flush() = delete;

  /// Return a StringRef for the vector contents.
  StringRef str() { return StringRef(OS.data(), OS.size()); }
};

/// A raw_ostream that discards all output.
class raw_null_ostream : public raw_pwrite_stream {
  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t size) override;
  void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) override;

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  uint64_t current_pos() const override;

public:
  explicit raw_null_ostream() = default;
  ~raw_null_ostream() override;
};

class buffer_ostream : public raw_svector_ostream {
  raw_ostream &OS;
  SmallVector<char, 0> Buffer;

  virtual void anchor() override;

public:
  buffer_ostream(raw_ostream &OS) : raw_svector_ostream(Buffer), OS(OS) {}
  ~buffer_ostream() override { OS << str(); }
};

} // end namespace llvm

#endif // LLVM_SUPPORT_RAW_OSTREAM_H
