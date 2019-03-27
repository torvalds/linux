//===-- VASPrintf.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/VASPrintf.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

bool lldb_private::VASprintf(llvm::SmallVectorImpl<char> &buf, const char *fmt,
                             va_list args) {
  llvm::SmallString<16> error("<Encoding error>");
  bool result = true;

  // Copy in case our first call to vsnprintf doesn't fit into our buffer
  va_list copy_args;
  va_copy(copy_args, args);

  buf.resize(buf.capacity());
  // Write up to `capacity` bytes, ignoring the current size.
  int length = ::vsnprintf(buf.data(), buf.size(), fmt, args);
  if (length < 0) {
    buf = error;
    result = false;
    goto finish;
  }

  if (size_t(length) >= buf.size()) {
    // The error formatted string didn't fit into our buffer, resize it to the
    // exact needed size, and retry
    buf.resize(length + 1);
    length = ::vsnprintf(buf.data(), buf.size(), fmt, copy_args);
    if (length < 0) {
      buf = error;
      result = false;
      goto finish;
    }
    assert(size_t(length) < buf.size());
  }
  buf.resize(length);

finish:
  va_end(args);
  va_end(copy_args);
  return result;
}
