//===-- esan_interface_internal.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of EfficiencySanitizer, a family of performance tuners.
//
// Calls to the functions declared in this header will be inserted by
// the instrumentation module.
//===----------------------------------------------------------------------===//

#ifndef ESAN_INTERFACE_INTERNAL_H
#define ESAN_INTERFACE_INTERNAL_H

#include <sanitizer_common/sanitizer_internal_defs.h>

// This header should NOT include any other headers.
// All functions in this header are extern "C" and start with __esan_.

using __sanitizer::uptr;
using __sanitizer::u32;

extern "C" {

// This should be kept consistent with LLVM's EfficiencySanitizerOptions.
// The value is passed as a 32-bit integer by the compiler.
typedef enum Type : u32 {
  ESAN_None = 0,
  ESAN_CacheFrag,
  ESAN_WorkingSet,
  ESAN_Max,
} ToolType;

// To handle interceptors that invoke instrumented code prior to
// __esan_init() being called, the instrumentation module creates this
// global variable specifying the tool.
extern ToolType __esan_which_tool;

// This function should be called at the very beginning of the process,
// before any instrumented code is executed and before any call to malloc.
SANITIZER_INTERFACE_ATTRIBUTE void __esan_init(ToolType Tool, void *Ptr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_exit(void *Ptr);

// The instrumentation module will insert a call to one of these routines prior
// to each load and store instruction for which we do not have "fastpath"
// inlined instrumentation.  These calls constitute the "slowpath" for our
// tools.  We have separate routines for each type of memory access to enable
// targeted optimization.
SANITIZER_INTERFACE_ATTRIBUTE void __esan_aligned_load1(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_aligned_load2(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_aligned_load4(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_aligned_load8(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_aligned_load16(void *Addr);

SANITIZER_INTERFACE_ATTRIBUTE void __esan_aligned_store1(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_aligned_store2(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_aligned_store4(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_aligned_store8(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_aligned_store16(void *Addr);

SANITIZER_INTERFACE_ATTRIBUTE void __esan_unaligned_load2(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_unaligned_load4(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_unaligned_load8(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_unaligned_load16(void *Addr);

SANITIZER_INTERFACE_ATTRIBUTE void __esan_unaligned_store2(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_unaligned_store4(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_unaligned_store8(void *Addr);
SANITIZER_INTERFACE_ATTRIBUTE void __esan_unaligned_store16(void *Addr);

// These cover unusually-sized accesses.
SANITIZER_INTERFACE_ATTRIBUTE
void __esan_unaligned_loadN(void *Addr, uptr Size);
SANITIZER_INTERFACE_ATTRIBUTE
void __esan_unaligned_storeN(void *Addr, uptr Size);

} // extern "C"

#endif // ESAN_INTERFACE_INTERNAL_H
