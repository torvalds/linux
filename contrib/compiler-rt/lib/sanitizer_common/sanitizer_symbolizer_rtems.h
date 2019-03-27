//===-- sanitizer_symbolizer_rtems.h -----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries.
//
// Define RTEMS's string formats and limits for the markup symbolizer.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_SYMBOLIZER_RTEMS_H
#define SANITIZER_SYMBOLIZER_RTEMS_H

#include "sanitizer_internal_defs.h"

namespace __sanitizer {

// The Myriad RTEMS symbolizer currently only parses backtrace lines,
// so use a format that the symbolizer understands.  For other
// markups, keep them the same as the Fuchsia's.

// This is used by UBSan for type names, and by ASan for global variable names.
constexpr const char *kFormatDemangle = "{{{symbol:%s}}}";
constexpr uptr kFormatDemangleMax = 1024;  // Arbitrary.

// Function name or equivalent from PC location.
constexpr const char *kFormatFunction = "{{{pc:%p}}}";
constexpr uptr kFormatFunctionMax = 64;  // More than big enough for 64-bit hex.

// Global variable name or equivalent from data memory address.
constexpr const char *kFormatData = "{{{data:%p}}}";

// One frame in a backtrace (printed on a line by itself).
constexpr const char *kFormatFrame = "    [%u] IP: %p";

}  // namespace __sanitizer

#endif  // SANITIZER_SYMBOLIZER_RTEMS_H
