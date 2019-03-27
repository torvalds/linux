//===-- llvm/Support/FormattedStream.h - Formatted streams ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains raw_ostream implementations for streams to do
// things like pretty-print comments.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FORMATTEDSTREAM_H
#define LLVM_SUPPORT_FORMATTEDSTREAM_H

#include "llvm/Support/raw_ostream.h"
#include <utility>

namespace llvm {

/// formatted_raw_ostream - A raw_ostream that wraps another one and keeps track
/// of line and column position, allowing padding out to specific column
/// boundaries and querying the number of lines written to the stream.
///
class formatted_raw_ostream : public raw_ostream {
  /// TheStream - The real stream we output to. We set it to be
  /// unbuffered, since we're already doing our own buffering.
  ///
  raw_ostream *TheStream;

  /// Position - The current output column and line of the data that's
  /// been flushed and the portion of the buffer that's been
  /// scanned.  The line and column scheme is zero-based.
  ///
  std::pair<unsigned, unsigned> Position;

  /// Scanned - This points to one past the last character in the
  /// buffer we've scanned.
  ///
  const char *Scanned;

  void write_impl(const char *Ptr, size_t Size) override;

  /// current_pos - Return the current position within the stream,
  /// not counting the bytes currently in the buffer.
  uint64_t current_pos() const override {
    // Our current position in the stream is all the contents which have been
    // written to the underlying stream (*not* the current position of the
    // underlying stream).
    return TheStream->tell();
  }

  /// ComputePosition - Examine the given output buffer and figure out the new
  /// position after output.
  ///
  void ComputePosition(const char *Ptr, size_t size);

  void setStream(raw_ostream &Stream) {
    releaseStream();

    TheStream = &Stream;

    // This formatted_raw_ostream inherits from raw_ostream, so it'll do its
    // own buffering, and it doesn't need or want TheStream to do another
    // layer of buffering underneath. Resize the buffer to what TheStream
    // had been using, and tell TheStream not to do its own buffering.
    if (size_t BufferSize = TheStream->GetBufferSize())
      SetBufferSize(BufferSize);
    else
      SetUnbuffered();
    TheStream->SetUnbuffered();

    Scanned = nullptr;
  }

public:
  /// formatted_raw_ostream - Open the specified file for
  /// writing. If an error occurs, information about the error is
  /// put into ErrorInfo, and the stream should be immediately
  /// destroyed; the string will be empty if no error occurred.
  ///
  /// As a side effect, the given Stream is set to be Unbuffered.
  /// This is because formatted_raw_ostream does its own buffering,
  /// so it doesn't want another layer of buffering to be happening
  /// underneath it.
  ///
  formatted_raw_ostream(raw_ostream &Stream)
      : TheStream(nullptr), Position(0, 0) {
    setStream(Stream);
  }
  explicit formatted_raw_ostream() : TheStream(nullptr), Position(0, 0) {
    Scanned = nullptr;
  }

  ~formatted_raw_ostream() override {
    flush();
    releaseStream();
  }

  /// PadToColumn - Align the output to some column number.  If the current
  /// column is already equal to or more than NewCol, PadToColumn inserts one
  /// space.
  ///
  /// \param NewCol - The column to move to.
  formatted_raw_ostream &PadToColumn(unsigned NewCol);

  /// getColumn - Return the column number
  unsigned getColumn() { return Position.first; }

  /// getLine - Return the line number
  unsigned getLine() { return Position.second; }

  raw_ostream &resetColor() override {
    TheStream->resetColor();
    return *this;
  }

  raw_ostream &reverseColor() override {
    TheStream->reverseColor();
    return *this;
  }

  raw_ostream &changeColor(enum Colors Color, bool Bold, bool BG) override {
    TheStream->changeColor(Color, Bold, BG);
    return *this;
  }

  bool is_displayed() const override {
    return TheStream->is_displayed();
  }

private:
  void releaseStream() {
    // Transfer the buffer settings from this raw_ostream back to the underlying
    // stream.
    if (!TheStream)
      return;
    if (size_t BufferSize = GetBufferSize())
      TheStream->SetBufferSize(BufferSize);
    else
      TheStream->SetUnbuffered();
  }
};

/// fouts() - This returns a reference to a formatted_raw_ostream for
/// standard output.  Use it like: fouts() << "foo" << "bar";
formatted_raw_ostream &fouts();

/// ferrs() - This returns a reference to a formatted_raw_ostream for
/// standard error.  Use it like: ferrs() << "foo" << "bar";
formatted_raw_ostream &ferrs();

/// fdbgs() - This returns a reference to a formatted_raw_ostream for
/// debug output.  Use it like: fdbgs() << "foo" << "bar";
formatted_raw_ostream &fdbgs();

} // end llvm namespace


#endif
