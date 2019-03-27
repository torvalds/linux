//===-- sanitizer_stacktrace_sparc.cc -------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
//
// Implemention of fast stack unwinding for Sparc.
//===----------------------------------------------------------------------===//

// This file is ported to Sparc v8, but it should be easy to port to
// Sparc v9.
#if defined(__sparcv8__) || defined(__sparcv8) || defined(__sparc_v8__)

#include "sanitizer_common.h"
#include "sanitizer_stacktrace.h"

namespace __sanitizer {

void BufferedStackTrace::FastUnwindStack(uptr pc, uptr bp, uptr stack_top,
                                         uptr stack_bottom, u32 max_depth) {
  const uptr kPageSize = GetPageSizeCached();
  CHECK_GE(max_depth, 2);
  trace_buffer[0] = pc;
  size = 1;
  if (stack_top < 4096) return;  // Sanity check for stack top.
  // Flush register windows to memory
  asm volatile("ta 3" ::: "memory");
  uhwptr *frame = (uhwptr*)bp;
  // Lowest possible address that makes sense as the next frame pointer.
  // Goes up as we walk the stack.
  uptr bottom = stack_bottom;
  // Avoid infinite loop when frame == frame[0] by using frame > prev_frame.
  while (IsValidFrame((uptr)frame, stack_top, bottom) &&
         IsAligned((uptr)frame, sizeof(*frame)) &&
         size < max_depth) {
    uhwptr pc1 = frame[15];
    // Let's assume that any pointer in the 0th page is invalid and
    // stop unwinding here.  If we're adding support for a platform
    // where this isn't true, we need to reconsider this check.
    if (pc1 < kPageSize)
      break;
    if (pc1 != pc) {
      trace_buffer[size++] = (uptr) pc1;
    }
    bottom = (uptr)frame;
    frame = (uhwptr*)frame[14];
  }
}

}  // namespace __sanitizer

#endif  // !defined(__sparcv8__) && !defined(__sparcv8) &&
        // !defined(__sparc_v8__)
