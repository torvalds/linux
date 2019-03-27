//===-- asan_stack.cc -----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Code for ASan stack trace.
//===----------------------------------------------------------------------===//
#include "asan_internal.h"
#include "asan_stack.h"
#include "sanitizer_common/sanitizer_atomic.h"

namespace __asan {

static atomic_uint32_t malloc_context_size;

void SetMallocContextSize(u32 size) {
  atomic_store(&malloc_context_size, size, memory_order_release);
}

u32 GetMallocContextSize() {
  return atomic_load(&malloc_context_size, memory_order_acquire);
}

}  // namespace __asan

// ------------------ Interface -------------- {{{1

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_print_stack_trace() {
  using namespace __asan;
  PRINT_CURRENT_STACK();
}
}  // extern "C"
