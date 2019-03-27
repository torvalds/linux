//===-- allocator_interface.h ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Public interface header for allocator used in sanitizers (ASan/TSan/MSan).
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_ALLOCATOR_INTERFACE_H
#define SANITIZER_ALLOCATOR_INTERFACE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
  /* Returns the estimated number of bytes that will be reserved by allocator
     for request of "size" bytes. If allocator can't allocate that much
     memory, returns the maximal possible allocation size, otherwise returns
     "size". */
  size_t __sanitizer_get_estimated_allocated_size(size_t size);

  /* Returns true if p was returned by the allocator and
     is not yet freed. */
  int __sanitizer_get_ownership(const volatile void *p);

  /* Returns the number of bytes reserved for the pointer p.
     Requires (get_ownership(p) == true) or (p == 0). */
  size_t __sanitizer_get_allocated_size(const volatile void *p);

  /* Number of bytes, allocated and not yet freed by the application. */
  size_t __sanitizer_get_current_allocated_bytes(void);

  /* Number of bytes, mmaped by the allocator to fulfill allocation requests.
     Generally, for request of X bytes, allocator can reserve and add to free
     lists a large number of chunks of size X to use them for future requests.
     All these chunks count toward the heap size. Currently, allocator never
     releases memory to OS (instead, it just puts freed chunks to free
     lists). */
  size_t __sanitizer_get_heap_size(void);

  /* Number of bytes, mmaped by the allocator, which can be used to fulfill
     allocation requests. When a user program frees memory chunk, it can first
     fall into quarantine and will count toward __sanitizer_get_free_bytes()
     later. */
  size_t __sanitizer_get_free_bytes(void);

  /* Number of bytes in unmapped pages, that are released to OS. Currently,
     always returns 0. */
  size_t __sanitizer_get_unmapped_bytes(void);

  /* Malloc hooks that may be optionally provided by user.
     __sanitizer_malloc_hook(ptr, size) is called immediately after
       allocation of "size" bytes, which returned "ptr".
     __sanitizer_free_hook(ptr) is called immediately before
       deallocation of "ptr". */
  void __sanitizer_malloc_hook(const volatile void *ptr, size_t size);
  void __sanitizer_free_hook(const volatile void *ptr);

  /* Installs a pair of hooks for malloc/free.
     Several (currently, 5) hook pairs may be installed, they are executed
     in the order they were installed and after calling
     __sanitizer_malloc_hook/__sanitizer_free_hook.
     Unlike __sanitizer_malloc_hook/__sanitizer_free_hook these hooks can be
     chained and do not rely on weak symbols working on the platform, but
     require __sanitizer_install_malloc_and_free_hooks to be called at startup
     and thus will not be called on malloc/free very early in the process.
     Returns the number of hooks currently installed or 0 on failure.
     Not thread-safe, should be called in the main thread before starting
     other threads.
  */
  int __sanitizer_install_malloc_and_free_hooks(
      void (*malloc_hook)(const volatile void *, size_t),
      void (*free_hook)(const volatile void *));

  /* Drains allocator quarantines (calling thread's and global ones), returns
     freed memory back to OS and releases other non-essential internal allocator
     resources in attempt to reduce process RSS.
     Currently available with ASan only.
  */
  void __sanitizer_purge_allocator(void);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif
