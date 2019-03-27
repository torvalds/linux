//===-- ubsan_flags.inc -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// UBSan runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef UBSAN_FLAG
# error "Define UBSAN_FLAG prior to including this file!"
#endif

// UBSAN_FLAG(Type, Name, DefaultValue, Description)
// See COMMON_FLAG in sanitizer_flags.inc for more details.

UBSAN_FLAG(bool, halt_on_error, false,
           "Crash the program after printing the first error report")
UBSAN_FLAG(bool, print_stacktrace, false,
           "Include full stacktrace into an error report")
UBSAN_FLAG(const char *, suppressions, "", "Suppressions file name.")
UBSAN_FLAG(bool, report_error_type, false,
        "Print specific error type instead of 'undefined-behavior' in summary.")
UBSAN_FLAG(bool, silence_unsigned_overflow, false,
        "Do not print non-fatal error reports for unsigned integer overflow. "
        "Used to provide fuzzing signal without blowing up logs.")
