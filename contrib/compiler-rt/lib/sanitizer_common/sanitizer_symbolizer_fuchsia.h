//===-- sanitizer_symbolizer_fuchsia.h -----------------------------------===//
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
// Define Fuchsia's string formats and limits for the markup symbolizer.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_SYMBOLIZER_FUCHSIA_H
#define SANITIZER_SYMBOLIZER_FUCHSIA_H

#include "sanitizer_internal_defs.h"

namespace __sanitizer {

// See the spec at:
// https://fuchsia.googlesource.com/zircon/+/master/docs/symbolizer_markup.md

// This is used by UBSan for type names, and by ASan for global variable names.
constexpr const char *kFormatDemangle = "{{{symbol:%s}}}";
constexpr uptr kFormatDemangleMax = 1024;  // Arbitrary.

// Function name or equivalent from PC location.
constexpr const char *kFormatFunction = "{{{pc:%p}}}";
constexpr uptr kFormatFunctionMax = 64;  // More than big enough for 64-bit hex.

// Global variable name or equivalent from data memory address.
constexpr const char *kFormatData = "{{{data:%p}}}";

// One frame in a backtrace (printed on a line by itself).
constexpr const char *kFormatFrame = "{{{bt:%u:%p}}}";

// Dump trigger element.
#define FORMAT_DUMPFILE "{{{dumpfile:%s:%s}}}"

}  // namespace __sanitizer

#endif  // SANITIZER_SYMBOLIZER_FUCHSIA_H
