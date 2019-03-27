//===-- hwasan_flags.inc ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Hwasan runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef HWASAN_FLAG
# error "Define HWASAN_FLAG prior to including this file!"
#endif

// HWASAN_FLAG(Type, Name, DefaultValue, Description)
// See COMMON_FLAG in sanitizer_flags.inc for more details.

HWASAN_FLAG(bool, verbose_threads, false,
            "inform on thread creation/destruction")
HWASAN_FLAG(bool, tag_in_malloc, true, "")
HWASAN_FLAG(bool, tag_in_free, true, "")
HWASAN_FLAG(bool, print_stats, false, "")
HWASAN_FLAG(bool, halt_on_error, true, "")
HWASAN_FLAG(bool, atexit, false, "")

// Test only flag to disable malloc/realloc/free memory tagging on startup.
// Tagging can be reenabled with __hwasan_enable_allocator_tagging().
HWASAN_FLAG(bool, disable_allocator_tagging, false, "")

// If false, use simple increment of a thread local counter to generate new
// tags.
HWASAN_FLAG(bool, random_tags, true, "")

HWASAN_FLAG(
    int, max_malloc_fill_size, 0x1000,  // By default, fill only the first 4K.
    "HWASan allocator flag. max_malloc_fill_size is the maximal amount of "
    "bytes that will be filled with malloc_fill_byte on malloc.")

// Rules for malloc alignment on aarch64:
//   * If the size is 16-aligned, then malloc should return 16-aligned memory.
//   * Otherwise, malloc should return 8-alignment memory.
// So,
//   * If the size is 16-aligned, we don't need to do anything.
//   * Otherwise we don't have to obey 16-alignment, just the 8-alignment.
//   * We may want to break the 8-alignment rule to catch more buffer overflows
//     but this will break valid code in some rare cases, like this:
//     struct Foo {
//       // accessed via atomic instructions that require 8-alignment.
//       std::atomic<int64_t> atomic_stuff;
//       ...
//       char vla[1];  // the actual size of vla could be anything.
//     }
// Which means that the safe values for malloc_align_right are 0, 8, 9,
// and the values 1 and 2 may require changes in otherwise valid code.

HWASAN_FLAG(
    int, malloc_align_right, 0,  // off by default
    "HWASan allocator flag. "
    "0 (default): allocations are always aligned left to 16-byte boundary; "
    "1: allocations are sometimes aligned right to 1-byte boundary (risky); "
    "2: allocations are always aligned right to 1-byte boundary (risky); "
    "8: allocations are sometimes aligned right to 8-byte boundary; "
    "9: allocations are always aligned right to 8-byte boundary."
  )
HWASAN_FLAG(bool, free_checks_tail_magic, 1,
    "If set, free() will check the magic values "
    "to the right of the allocated object "
    "if the allocation size is not a divident of the granule size")
HWASAN_FLAG(
    int, max_free_fill_size, 0,
    "HWASan allocator flag. max_free_fill_size is the maximal amount of "
    "bytes that will be filled with free_fill_byte during free.")
HWASAN_FLAG(int, malloc_fill_byte, 0xbe,
          "Value used to fill the newly allocated memory.")
HWASAN_FLAG(int, free_fill_byte, 0x55,
          "Value used to fill deallocated memory.")
HWASAN_FLAG(int, heap_history_size, 1023,
          "The number of heap (de)allocations remembered per thread. "
          "Affects the quality of heap-related reports, but not the ability "
          "to find bugs.")
HWASAN_FLAG(bool, export_memory_stats, true,
            "Export up-to-date memory stats through /proc")
HWASAN_FLAG(int, stack_history_size, 1024,
            "The number of stack frames remembered per thread. "
            "Affects the quality of stack-related reports, but not the ability "
            "to find bugs.")
