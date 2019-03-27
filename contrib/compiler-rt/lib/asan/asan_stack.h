//===-- asan_stack.h --------------------------------------------*- C++ -*-===//
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
// ASan-private header for asan_stack.cc.
//===----------------------------------------------------------------------===//

#ifndef ASAN_STACK_H
#define ASAN_STACK_H

#include "asan_flags.h"
#include "asan_thread.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_stacktrace.h"

namespace __asan {

static const u32 kDefaultMallocContextSize = 30;

void SetMallocContextSize(u32 size);
u32 GetMallocContextSize();

// Get the stack trace with the given pc and bp.
// The pc will be in the position 0 of the resulting stack trace.
// The bp may refer to the current frame or to the caller's frame.
ALWAYS_INLINE
void GetStackTrace(BufferedStackTrace *stack, uptr max_depth, uptr pc, uptr bp,
                   void *context, bool fast) {
#if SANITIZER_WINDOWS
  stack->Unwind(max_depth, pc, bp, context, 0, 0, fast);
#else
  AsanThread *t;
  stack->size = 0;
  if (LIKELY(asan_inited)) {
    if ((t = GetCurrentThread()) && !t->isUnwinding()) {
      uptr stack_top = t->stack_top();
      uptr stack_bottom = t->stack_bottom();
      ScopedUnwinding unwind_scope(t);
      if (!SANITIZER_MIPS || IsValidFrame(bp, stack_top, stack_bottom)) {
        stack->Unwind(max_depth, pc, bp, context, stack_top, stack_bottom,
                      fast);
      }
    } else if (!t && !fast) {
      /* If GetCurrentThread() has failed, try to do slow unwind anyways. */
      stack->Unwind(max_depth, pc, bp, context, 0, 0, false);
    }
  }
#endif // SANITIZER_WINDOWS
}

} // namespace __asan

// NOTE: A Rule of thumb is to retrieve stack trace in the interceptors
// as early as possible (in functions exposed to the user), as we generally
// don't want stack trace to contain functions from ASan internals.

#define GET_STACK_TRACE(max_size, fast)                          \
  BufferedStackTrace stack;                                      \
  if (max_size <= 2) {                                           \
    stack.size = max_size;                                       \
    if (max_size > 0) {                                          \
      stack.top_frame_bp = GET_CURRENT_FRAME();                  \
      stack.trace_buffer[0] = StackTrace::GetCurrentPc();        \
      if (max_size > 1) stack.trace_buffer[1] = GET_CALLER_PC(); \
    }                                                            \
  } else {                                                       \
    GetStackTrace(&stack, max_size, StackTrace::GetCurrentPc(),  \
                  GET_CURRENT_FRAME(), 0, fast);                 \
  }

#define GET_STACK_TRACE_FATAL(pc, bp)              \
  BufferedStackTrace stack;                        \
  GetStackTrace(&stack, kStackTraceMax, pc, bp, 0, \
                common_flags()->fast_unwind_on_fatal)

#define GET_STACK_TRACE_SIGNAL(sig)                                        \
  BufferedStackTrace stack;                                                \
  GetStackTrace(&stack, kStackTraceMax, (sig).pc, (sig).bp, (sig).context, \
                common_flags()->fast_unwind_on_fatal)

#define GET_STACK_TRACE_FATAL_HERE                                \
  GET_STACK_TRACE(kStackTraceMax, common_flags()->fast_unwind_on_fatal)

#define GET_STACK_TRACE_CHECK_HERE                                \
  GET_STACK_TRACE(kStackTraceMax, common_flags()->fast_unwind_on_check)

#define GET_STACK_TRACE_THREAD                                    \
  GET_STACK_TRACE(kStackTraceMax, true)

#define GET_STACK_TRACE_MALLOC                                                 \
  GET_STACK_TRACE(GetMallocContextSize(), common_flags()->fast_unwind_on_malloc)

#define GET_STACK_TRACE_FREE GET_STACK_TRACE_MALLOC

#define PRINT_CURRENT_STACK()   \
  {                             \
    GET_STACK_TRACE_FATAL_HERE; \
    stack.Print();              \
  }

#define PRINT_CURRENT_STACK_CHECK() \
  {                                 \
    GET_STACK_TRACE_CHECK_HERE;     \
    stack.Print();                  \
  }

#endif // ASAN_STACK_H
