//===-- sanitizer/asan_interface.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer.
//
// Public interface header.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_ASAN_INTERFACE_H
#define SANITIZER_ASAN_INTERFACE_H

#include <sanitizer/common_interface_defs.h>

#ifdef __cplusplus
extern "C" {
#endif
  // Marks memory region [addr, addr+size) as unaddressable.
  // This memory must be previously allocated by the user program. Accessing
  // addresses in this region from instrumented code is forbidden until
  // this region is unpoisoned. This function is not guaranteed to poison
  // the whole region - it may poison only subregion of [addr, addr+size) due
  // to ASan alignment restrictions.
  // Method is NOT thread-safe in the sense that no two threads can
  // (un)poison memory in the same memory region simultaneously.
  void __asan_poison_memory_region(void const volatile *addr, size_t size);
  // Marks memory region [addr, addr+size) as addressable.
  // This memory must be previously allocated by the user program. Accessing
  // addresses in this region is allowed until this region is poisoned again.
  // This function may unpoison a superregion of [addr, addr+size) due to
  // ASan alignment restrictions.
  // Method is NOT thread-safe in the sense that no two threads can
  // (un)poison memory in the same memory region simultaneously.
  void __asan_unpoison_memory_region(void const volatile *addr, size_t size);

// User code should use macros instead of functions.
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#define ASAN_POISON_MEMORY_REGION(addr, size) \
  __asan_poison_memory_region((addr), (size))
#define ASAN_UNPOISON_MEMORY_REGION(addr, size) \
  __asan_unpoison_memory_region((addr), (size))
#else
#define ASAN_POISON_MEMORY_REGION(addr, size) \
  ((void)(addr), (void)(size))
#define ASAN_UNPOISON_MEMORY_REGION(addr, size) \
  ((void)(addr), (void)(size))
#endif

  // Returns 1 if addr is poisoned (i.e. 1-byte read/write access to this
  // address will result in error report from AddressSanitizer).
  // Otherwise returns 0.
  int __asan_address_is_poisoned(void const volatile *addr);

  // If at least one byte in [beg, beg+size) is poisoned, return the address
  // of the first such byte. Otherwise return 0.
  void *__asan_region_is_poisoned(void *beg, size_t size);

  // Print the description of addr (useful when debugging in gdb).
  void __asan_describe_address(void *addr);

  // Useful for calling from a debugger to get information about an ASan error.
  // Returns 1 if an error has been (or is being) reported, otherwise returns 0.
  int __asan_report_present(void);

  // Useful for calling from a debugger to get information about an ASan error.
  // If an error has been (or is being) reported, the following functions return
  // the pc, bp, sp, address, access type (0 = read, 1 = write), access size and
  // bug description (e.g. "heap-use-after-free"). Otherwise they return 0.
  void *__asan_get_report_pc(void);
  void *__asan_get_report_bp(void);
  void *__asan_get_report_sp(void);
  void *__asan_get_report_address(void);
  int __asan_get_report_access_type(void);
  size_t __asan_get_report_access_size(void);
  const char *__asan_get_report_description(void);

  // Useful for calling from the debugger to get information about a pointer.
  // Returns the category of the given pointer as a constant string.
  // Possible return values are "global", "stack", "stack-fake", "heap",
  // "heap-invalid", "shadow-low", "shadow-gap", "shadow-high", "unknown".
  // If global or stack, tries to also return the variable name, address and
  // size. If heap, tries to return the chunk address and size. 'name' should
  // point to an allocated buffer of size 'name_size'.
  const char *__asan_locate_address(void *addr, char *name, size_t name_size,
                                    void **region_address, size_t *region_size);

  // Useful for calling from the debugger to get the allocation stack trace
  // and thread ID for a heap address. Stores up to 'size' frames into 'trace',
  // returns the number of stored frames or 0 on error.
  size_t __asan_get_alloc_stack(void *addr, void **trace, size_t size,
                                int *thread_id);

  // Useful for calling from the debugger to get the free stack trace
  // and thread ID for a heap address. Stores up to 'size' frames into 'trace',
  // returns the number of stored frames or 0 on error.
  size_t __asan_get_free_stack(void *addr, void **trace, size_t size,
                               int *thread_id);

  // Useful for calling from the debugger to get the current shadow memory
  // mapping.
  void __asan_get_shadow_mapping(size_t *shadow_scale, size_t *shadow_offset);

  // This is an internal function that is called to report an error.
  // However it is still a part of the interface because users may want to
  // set a breakpoint on this function in a debugger.
  void __asan_report_error(void *pc, void *bp, void *sp,
                           void *addr, int is_write, size_t access_size);

  // Deprecated. Call __sanitizer_set_death_callback instead.
  void __asan_set_death_callback(void (*callback)(void));

  void __asan_set_error_report_callback(void (*callback)(const char*));

  // User may provide function that would be called right when ASan detects
  // an error. This can be used to notice cases when ASan detects an error, but
  // the program crashes before ASan report is printed.
  void __asan_on_error(void);

  // Prints accumulated stats to stderr. Used for debugging.
  void __asan_print_accumulated_stats(void);

  // This function may be optionally provided by user and should return
  // a string containing ASan runtime options. See asan_flags.h for details.
  const char* __asan_default_options(void);

  // The following 2 functions facilitate garbage collection in presence of
  // asan's fake stack.

  // Returns an opaque handler to be used later in __asan_addr_is_in_fake_stack.
  // Returns NULL if the current thread does not have a fake stack.
  void *__asan_get_current_fake_stack(void);

  // If fake_stack is non-NULL and addr belongs to a fake frame in
  // fake_stack, returns the address on real stack that corresponds to
  // the fake frame and sets beg/end to the boundaries of this fake frame.
  // Otherwise returns NULL and does not touch beg/end.
  // If beg/end are NULL, they are not touched.
  // This function may be called from a thread other than the owner of
  // fake_stack, but the owner thread need to be alive.
  void *__asan_addr_is_in_fake_stack(void *fake_stack, void *addr, void **beg,
                                     void **end);

  // Performs cleanup before a [[noreturn]] function.  Must be called
  // before things like _exit and execl to avoid false positives on stack.
  void __asan_handle_no_return(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SANITIZER_ASAN_INTERFACE_H
