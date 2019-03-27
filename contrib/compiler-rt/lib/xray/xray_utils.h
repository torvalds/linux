//===-- xray_utils.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// Some shared utilities for the XRay runtime implementation.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_UTILS_H
#define XRAY_UTILS_H

#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <utility>

#include "sanitizer_common/sanitizer_common.h"
#if SANITIZER_FUCHSIA
#include <zircon/types.h>
#endif

namespace __xray {

class LogWriter {
public:
#if SANITIZER_FUCHSIA
 LogWriter(zx_handle_t Vmo) : Vmo(Vmo) {}
#else
  explicit LogWriter(int Fd) : Fd(Fd) {}
#endif
 ~LogWriter();

 // Write a character range into a log.
 void WriteAll(const char *Begin, const char *End);

 void Flush();

 // Returns a new log instance initialized using the flag-provided values.
 static LogWriter *Open();
 // Closes and deallocates the log instance.
 static void Close(LogWriter *LogWriter);

private:
#if SANITIZER_FUCHSIA
 zx_handle_t Vmo = ZX_HANDLE_INVALID;
 uint64_t Offset = 0;
#else
 int Fd = -1;
#endif
};

constexpr size_t gcd(size_t a, size_t b) {
  return (b == 0) ? a : gcd(b, a % b);
}

constexpr size_t lcm(size_t a, size_t b) { return a * b / gcd(a, b); }

constexpr size_t nearest_boundary(size_t number, size_t multiple) {
  return multiple * ((number / multiple) + (number % multiple ? 1 : 0));
}

constexpr size_t next_pow2_helper(size_t num, size_t acc) {
  return (1u << acc) >= num ? (1u << acc) : next_pow2_helper(num, acc + 1);
}

constexpr size_t next_pow2(size_t number) {
  return next_pow2_helper(number, 1);
}

template <class T> constexpr T &max(T &A, T &B) { return A > B ? A : B; }

template <class T> constexpr T &min(T &A, T &B) { return A <= B ? A : B; }

constexpr ptrdiff_t diff(uintptr_t A, uintptr_t B) {
  return max(A, B) - min(A, B);
}

} // namespace __xray

#endif // XRAY_UTILS_H
