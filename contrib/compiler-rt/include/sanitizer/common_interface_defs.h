//===-- sanitizer/common_interface_defs.h -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Common part of the public sanitizer interface.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_COMMON_INTERFACE_DEFS_H
#define SANITIZER_COMMON_INTERFACE_DEFS_H

#include <stddef.h>
#include <stdint.h>

// GCC does not understand __has_feature.
#if !defined(__has_feature)
# define __has_feature(x) 0
#endif

#ifdef __cplusplus
extern "C" {
#endif
  // Arguments for __sanitizer_sandbox_on_notify() below.
  typedef struct {
    // Enable sandbox support in sanitizer coverage.
    int coverage_sandboxed;
    // File descriptor to write coverage data to. If -1 is passed, a file will
    // be pre-opened by __sanitizer_sandobx_on_notify(). This field has no
    // effect if coverage_sandboxed == 0.
    intptr_t coverage_fd;
    // If non-zero, split the coverage data into well-formed blocks. This is
    // useful when coverage_fd is a socket descriptor. Each block will contain
    // a header, allowing data from multiple processes to be sent over the same
    // socket.
    unsigned int coverage_max_block_size;
  } __sanitizer_sandbox_arguments;

  // Tell the tools to write their reports to "path.<pid>" instead of stderr.
  void __sanitizer_set_report_path(const char *path);
  // Tell the tools to write their reports to the provided file descriptor
  // (casted to void *).
  void __sanitizer_set_report_fd(void *fd);

  // Notify the tools that the sandbox is going to be turned on. The reserved
  // parameter will be used in the future to hold a structure with functions
  // that the tools may call to bypass the sandbox.
  void __sanitizer_sandbox_on_notify(__sanitizer_sandbox_arguments *args);

  // This function is called by the tool when it has just finished reporting
  // an error. 'error_summary' is a one-line string that summarizes
  // the error message. This function can be overridden by the client.
  void __sanitizer_report_error_summary(const char *error_summary);

  // Some of the sanitizers (e.g. asan/tsan) may miss bugs that happen
  // in unaligned loads/stores. In order to find such bugs reliably one needs
  // to replace plain unaligned loads/stores with these calls.
  uint16_t __sanitizer_unaligned_load16(const void *p);
  uint32_t __sanitizer_unaligned_load32(const void *p);
  uint64_t __sanitizer_unaligned_load64(const void *p);
  void __sanitizer_unaligned_store16(void *p, uint16_t x);
  void __sanitizer_unaligned_store32(void *p, uint32_t x);
  void __sanitizer_unaligned_store64(void *p, uint64_t x);

  // Returns 1 on the first call, then returns 0 thereafter.  Called by the tool
  // to ensure only one report is printed when multiple errors occur
  // simultaneously.
  int __sanitizer_acquire_crash_state();

  // Annotate the current state of a contiguous container, such as
  // std::vector, std::string or similar.
  // A contiguous container is a container that keeps all of its elements
  // in a contiguous region of memory. The container owns the region of memory
  // [beg, end); the memory [beg, mid) is used to store the current elements
  // and the memory [mid, end) is reserved for future elements;
  // beg <= mid <= end. For example, in "std::vector<> v"
  //   beg = &v[0];
  //   end = beg + v.capacity() * sizeof(v[0]);
  //   mid = beg + v.size()     * sizeof(v[0]);
  //
  // This annotation tells the Sanitizer tool about the current state of the
  // container so that the tool can report errors when memory from [mid, end)
  // is accessed. Insert this annotation into methods like push_back/pop_back.
  // Supply the old and the new values of mid (old_mid/new_mid).
  // In the initial state mid == end and so should be the final
  // state when the container is destroyed or when it reallocates the storage.
  //
  // Use with caution and don't use for anything other than vector-like classes.
  //
  // For AddressSanitizer, 'beg' should be 8-aligned and 'end' should
  // be either 8-aligned or it should point to the end of a separate heap-,
  // stack-, or global- allocated buffer. I.e. the following will not work:
  //   int64_t x[2];  // 16 bytes, 8-aligned.
  //   char *beg = (char *)&x[0];
  //   char *end = beg + 12;  // Not 8 aligned, not the end of the buffer.
  // This however will work fine:
  //   int32_t x[3];  // 12 bytes, but 8-aligned under AddressSanitizer.
  //   char *beg = (char*)&x[0];
  //   char *end = beg + 12;  // Not 8-aligned, but is the end of the buffer.
  void __sanitizer_annotate_contiguous_container(const void *beg,
                                                 const void *end,
                                                 const void *old_mid,
                                                 const void *new_mid);
  // Returns true if the contiguous container [beg, end) is properly poisoned
  // (e.g. with __sanitizer_annotate_contiguous_container), i.e. if
  //  - [beg, mid) is addressable,
  //  - [mid, end) is unaddressable.
  // Full verification requires O(end-beg) time; this function tries to avoid
  // such complexity by touching only parts of the container around beg/mid/end.
  int __sanitizer_verify_contiguous_container(const void *beg, const void *mid,
                                              const void *end);

  // Similar to __sanitizer_verify_contiguous_container but returns the address
  // of the first improperly poisoned byte otherwise. Returns null if the area
  // is poisoned properly.
  const void *__sanitizer_contiguous_container_find_bad_address(
      const void *beg, const void *mid, const void *end);

  // Print the stack trace leading to this call. Useful for debugging user code.
  void __sanitizer_print_stack_trace(void);

  // Symbolizes the supplied 'pc' using the format string 'fmt'.
  // Outputs at most 'out_buf_size' bytes into 'out_buf'.
  // If 'out_buf' is not empty then output is zero or more non empty C strings
  // followed by single empty C string. Multiple strings can be returned if PC
  // corresponds to inlined function. Inlined frames are printed in the order
  // from "most-inlined" to the "least-inlined", so the last frame should be the
  // not inlined function.
  // Inlined frames can be removed with 'symbolize_inline_frames=0'.
  // The format syntax is described in
  // lib/sanitizer_common/sanitizer_stacktrace_printer.h.
  void __sanitizer_symbolize_pc(void *pc, const char *fmt, char *out_buf,
                                size_t out_buf_size);
  // Same as __sanitizer_symbolize_pc, but for data section (i.e. globals).
  void __sanitizer_symbolize_global(void *data_ptr, const char *fmt,
                                    char *out_buf, size_t out_buf_size);

  // Sets the callback to be called right before death on error.
  // Passing 0 will unset the callback.
  void __sanitizer_set_death_callback(void (*callback)(void));

  // Interceptor hooks.
  // Whenever a libc function interceptor is called it checks if the
  // corresponding weak hook is defined, and it so -- calls it.
  // The primary use case is data-flow-guided fuzzing, where the fuzzer needs
  // to know what is being passed to libc functions, e.g. memcmp.
  // FIXME: implement more hooks.
  void __sanitizer_weak_hook_memcmp(void *called_pc, const void *s1,
                                    const void *s2, size_t n, int result);
  void __sanitizer_weak_hook_strncmp(void *called_pc, const char *s1,
                                    const char *s2, size_t n, int result);
  void __sanitizer_weak_hook_strncasecmp(void *called_pc, const char *s1,
                                         const char *s2, size_t n, int result);
  void __sanitizer_weak_hook_strcmp(void *called_pc, const char *s1,
                                    const char *s2, int result);
  void __sanitizer_weak_hook_strcasecmp(void *called_pc, const char *s1,
                                        const char *s2, int result);
  void __sanitizer_weak_hook_strstr(void *called_pc, const char *s1,
                                    const char *s2, char *result);
  void __sanitizer_weak_hook_strcasestr(void *called_pc, const char *s1,
                                        const char *s2, char *result);
  void __sanitizer_weak_hook_memmem(void *called_pc,
                                    const void *s1, size_t len1,
                                    const void *s2, size_t len2, void *result);

  // Prints stack traces for all live heap allocations ordered by total
  // allocation size until `top_percent` of total live heap is shown.
  // `top_percent` should be between 1 and 100.
  // At most `max_number_of_contexts` contexts (stack traces) is printed.
  // Experimental feature currently available only with asan on Linux/x86_64.
  void __sanitizer_print_memory_profile(size_t top_percent,
                                        size_t max_number_of_contexts);

  // Fiber annotation interface.
  // Before switching to a different stack, one must call
  // __sanitizer_start_switch_fiber with a pointer to the bottom of the
  // destination stack and its size. When code starts running on the new stack,
  // it must call __sanitizer_finish_switch_fiber to finalize the switch.
  // The start_switch function takes a void** to store the current fake stack if
  // there is one (it is needed when detect_stack_use_after_return is enabled).
  // When restoring a stack, this pointer must be given to the finish_switch
  // function. In most cases, this void* can be stored on the stack just before
  // switching.  When leaving a fiber definitely, null must be passed as first
  // argument to the start_switch function so that the fake stack is destroyed.
  // If you do not want support for stack use-after-return detection, you can
  // always pass null to these two functions.
  // Note that the fake stack mechanism is disabled during fiber switch, so if a
  // signal callback runs during the switch, it will not benefit from the stack
  // use-after-return detection.
  void __sanitizer_start_switch_fiber(void **fake_stack_save,
                                      const void *bottom, size_t size);
  void __sanitizer_finish_switch_fiber(void *fake_stack_save,
                                       const void **bottom_old,
                                       size_t *size_old);

  // Get full module name and calculate pc offset within it.
  // Returns 1 if pc belongs to some module, 0 if module was not found.
  int __sanitizer_get_module_and_offset_for_pc(void *pc, char *module_path,
                                               size_t module_path_len,
                                               void **pc_offset);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SANITIZER_COMMON_INTERFACE_DEFS_H
