//===-- msan_flags.inc ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// MSan runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef MSAN_FLAG
# error "Define MSAN_FLAG prior to including this file!"
#endif

// MSAN_FLAG(Type, Name, DefaultValue, Description)
// See COMMON_FLAG in sanitizer_flags.inc for more details.

MSAN_FLAG(int, exit_code, -1,
          "DEPRECATED. Use exitcode from common flags instead.")
MSAN_FLAG(int, origin_history_size, Origin::kMaxDepth, "")
MSAN_FLAG(int, origin_history_per_stack_limit, 20000, "")
MSAN_FLAG(bool, poison_heap_with_zeroes, false, "")
MSAN_FLAG(bool, poison_stack_with_zeroes, false, "")
MSAN_FLAG(bool, poison_in_malloc, true, "")
MSAN_FLAG(bool, poison_in_free, true, "")
MSAN_FLAG(bool, poison_in_dtor, false, "")
MSAN_FLAG(bool, report_umrs, true, "")
MSAN_FLAG(bool, wrap_signals, true, "")
MSAN_FLAG(bool, print_stats, false, "")
MSAN_FLAG(bool, halt_on_error, !&__msan_keep_going, "")
MSAN_FLAG(bool, atexit, false, "")
MSAN_FLAG(int, store_context_size, 20,
          "Like malloc_context_size, but for uninit stores.")
