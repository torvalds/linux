//===- LineIterator.h - Iterator to read a text buffer's lines --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_LINEITERATOR_H
#define LLVM_SUPPORT_LINEITERATOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"
#include <iterator>

namespace llvm {

class MemoryBuffer;

/// A forward iterator which reads text lines from a buffer.
///
/// This class provides a forward iterator interface for reading one line at
/// a time from a buffer. When default constructed the iterator will be the
/// "end" iterator.
///
/// The iterator is aware of what line number it is currently processing. It
/// strips blank lines by default, and comment lines given a comment-starting
/// character.
///
/// Note that this iterator requires the buffer to be nul terminated.
class line_iterator
    : public std::iterator<std::forward_iterator_tag, StringRef> {
  const MemoryBuffer *Buffer;
  char CommentMarker;
  bool SkipBlanks;

  unsigned LineNumber;
  StringRef CurrentLine;

public:
  /// Default construct an "end" iterator.
  line_iterator() : Buffer(nullptr) {}

  /// Construct a new iterator around some memory buffer.
  explicit line_iterator(const MemoryBuffer &Buffer, bool SkipBlanks = true,
                         char CommentMarker = '\0');

  /// Return true if we've reached EOF or are an "end" iterator.
  bool is_at_eof() const { return !Buffer; }

  /// Return true if we're an "end" iterator or have reached EOF.
  bool is_at_end() const { return is_at_eof(); }

  /// Return the current line number. May return any number at EOF.
  int64_t line_number() const { return LineNumber; }

  /// Advance to the next (non-empty, non-comment) line.
  line_iterator &operator++() {
    advance();
    return *this;
  }
  line_iterator operator++(int) {
    line_iterator tmp(*this);
    advance();
    return tmp;
  }

  /// Get the current line as a \c StringRef.
  StringRef operator*() const { return CurrentLine; }
  const StringRef *operator->() const { return &CurrentLine; }

  friend bool operator==(const line_iterator &LHS, const line_iterator &RHS) {
    return LHS.Buffer == RHS.Buffer &&
           LHS.CurrentLine.begin() == RHS.CurrentLine.begin();
  }

  friend bool operator!=(const line_iterator &LHS, const line_iterator &RHS) {
    return !(LHS == RHS);
  }

private:
  /// Advance the iterator to the next line.
  void advance();
};
}

#endif
