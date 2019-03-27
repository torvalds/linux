//===-- tsan_preinit.cc ---------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer.
//
// Call __tsan_init at the very early stage of process startup.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "tsan_interface.h"

#if SANITIZER_CAN_USE_PREINIT_ARRAY

// The symbol is called __local_tsan_preinit, because it's not intended to be
// exported.
// This code linked into the main executable when -fsanitize=thread is in
// the link flags. It can only use exported interface functions.
__attribute__((section(".preinit_array"), used))
void (*__local_tsan_preinit)(void) = __tsan_init;

#endif
