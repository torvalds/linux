//===--- Utility.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//
// This file contains several utility classes used by the demangle library.
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEMANGLE_UTILITY_H
#define LLVM_DEMANGLE_UTILITY_H

#include "StringView.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>

// Stream that AST nodes write their string representation into after the AST
// has been parsed.
class OutputStream {
  char *Buffer;
  size_t CurrentPosition;
  size_t BufferCapacity;

  // Ensure there is at least n more positions in buffer.
  void grow(size_t N) {
    if (N + CurrentPosition >= BufferCapacity) {
      BufferCapacity *= 2;
      if (BufferCapacity < N + CurrentPosition)
        BufferCapacity = N + CurrentPosition;
      Buffer = static_cast<char *>(std::realloc(Buffer, BufferCapacity));
      if (Buffer == nullptr)
        std::terminate();
    }
  }

  void writeUnsigned(uint64_t N, bool isNeg = false) {
    // Handle special case...
    if (N == 0) {
      *this << '0';
      return;
    }

    char Temp[21];
    char *TempPtr = std::end(Temp);

    while (N) {
      *--TempPtr = '0' + char(N % 10);
      N /= 10;
    }

    // Add negative sign...
    if (isNeg)
      *--TempPtr = '-';
    this->operator<<(StringView(TempPtr, std::end(Temp)));
  }

public:
  OutputStream(char *StartBuf, size_t Size)
      : Buffer(StartBuf), CurrentPosition(0), BufferCapacity(Size) {}
  OutputStream() = default;
  void reset(char *Buffer_, size_t BufferCapacity_) {
    CurrentPosition = 0;
    Buffer = Buffer_;
    BufferCapacity = BufferCapacity_;
  }

  /// If a ParameterPackExpansion (or similar type) is encountered, the offset
  /// into the pack that we're currently printing.
  unsigned CurrentPackIndex = std::numeric_limits<unsigned>::max();
  unsigned CurrentPackMax = std::numeric_limits<unsigned>::max();

  OutputStream &operator+=(StringView R) {
    size_t Size = R.size();
    if (Size == 0)
      return *this;
    grow(Size);
    std::memmove(Buffer + CurrentPosition, R.begin(), Size);
    CurrentPosition += Size;
    return *this;
  }

  OutputStream &operator+=(char C) {
    grow(1);
    Buffer[CurrentPosition++] = C;
    return *this;
  }

  OutputStream &operator<<(StringView R) { return (*this += R); }

  OutputStream &operator<<(char C) { return (*this += C); }

  OutputStream &operator<<(long long N) {
    if (N < 0)
      writeUnsigned(static_cast<unsigned long long>(-N), true);
    else
      writeUnsigned(static_cast<unsigned long long>(N));
    return *this;
  }

  OutputStream &operator<<(unsigned long long N) {
    writeUnsigned(N, false);
    return *this;
  }

  OutputStream &operator<<(long N) {
    return this->operator<<(static_cast<long long>(N));
  }

  OutputStream &operator<<(unsigned long N) {
    return this->operator<<(static_cast<unsigned long long>(N));
  }

  OutputStream &operator<<(int N) {
    return this->operator<<(static_cast<long long>(N));
  }

  OutputStream &operator<<(unsigned int N) {
    return this->operator<<(static_cast<unsigned long long>(N));
  }

  size_t getCurrentPosition() const { return CurrentPosition; }
  void setCurrentPosition(size_t NewPos) { CurrentPosition = NewPos; }

  char back() const {
    return CurrentPosition ? Buffer[CurrentPosition - 1] : '\0';
  }

  bool empty() const { return CurrentPosition == 0; }

  char *getBuffer() { return Buffer; }
  char *getBufferEnd() { return Buffer + CurrentPosition - 1; }
  size_t getBufferCapacity() { return BufferCapacity; }
};

template <class T> class SwapAndRestore {
  T &Restore;
  T OriginalValue;
  bool ShouldRestore = true;

public:
  SwapAndRestore(T &Restore_) : SwapAndRestore(Restore_, Restore_) {}

  SwapAndRestore(T &Restore_, T NewVal)
      : Restore(Restore_), OriginalValue(Restore) {
    Restore = std::move(NewVal);
  }
  ~SwapAndRestore() {
    if (ShouldRestore)
      Restore = std::move(OriginalValue);
  }

  void shouldRestore(bool ShouldRestore_) { ShouldRestore = ShouldRestore_; }

  void restoreNow(bool Force) {
    if (!Force && !ShouldRestore)
      return;

    Restore = std::move(OriginalValue);
    ShouldRestore = false;
  }

  SwapAndRestore(const SwapAndRestore &) = delete;
  SwapAndRestore &operator=(const SwapAndRestore &) = delete;
};

inline bool initializeOutputStream(char *Buf, size_t *N, OutputStream &S,
                                   size_t InitSize) {
  size_t BufferSize;
  if (Buf == nullptr) {
    Buf = static_cast<char *>(std::malloc(InitSize));
    if (Buf == nullptr)
      return false;
    BufferSize = InitSize;
  } else
    BufferSize = *N;

  S.reset(Buf, BufferSize);
  return true;
}

#endif
