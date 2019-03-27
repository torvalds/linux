//===-- sanitizer_stacktrace.cc -------------------------------------------===//
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
//===----------------------------------------------------------------------===//

#include "sanitizer_common.h"
#include "sanitizer_flags.h"
#include "sanitizer_stacktrace.h"

namespace __sanitizer {

uptr StackTrace::GetNextInstructionPc(uptr pc) {
#if defined(__mips__)
  return pc + 8;
#elif defined(__powerpc__) || defined(__sparc__) || defined(__arm__) || \
    defined(__aarch64__)
  return pc + 4;
#else
  return pc + 1;
#endif
}

uptr StackTrace::GetCurrentPc() {
  return GET_CALLER_PC();
}

void BufferedStackTrace::Init(const uptr *pcs, uptr cnt, uptr extra_top_pc) {
  size = cnt + !!extra_top_pc;
  CHECK_LE(size, kStackTraceMax);
  internal_memcpy(trace_buffer, pcs, cnt * sizeof(trace_buffer[0]));
  if (extra_top_pc)
    trace_buffer[cnt] = extra_top_pc;
  top_frame_bp = 0;
}

// Sparc implemention is in its own file.
#if !defined(__sparc__)

// In GCC on ARM bp points to saved lr, not fp, so we should check the next
// cell in stack to be a saved frame pointer. GetCanonicFrame returns the
// pointer to saved frame pointer in any case.
static inline uhwptr *GetCanonicFrame(uptr bp,
                                      uptr stack_top,
                                      uptr stack_bottom) {
#ifdef __arm__
  if (!IsValidFrame(bp, stack_top, stack_bottom)) return 0;
  uhwptr *bp_prev = (uhwptr *)bp;
  if (IsValidFrame((uptr)bp_prev[0], stack_top, stack_bottom)) return bp_prev;
  // The next frame pointer does not look right. This could be a GCC frame, step
  // back by 1 word and try again.
  if (IsValidFrame((uptr)bp_prev[-1], stack_top, stack_bottom))
    return bp_prev - 1;
  // Nope, this does not look right either. This means the frame after next does
  // not have a valid frame pointer, but we can still extract the caller PC.
  // Unfortunately, there is no way to decide between GCC and LLVM frame
  // layouts. Assume LLVM.
  return bp_prev;
#else
  return (uhwptr*)bp;
#endif
}

void BufferedStackTrace::FastUnwindStack(uptr pc, uptr bp, uptr stack_top,
                                         uptr stack_bottom, u32 max_depth) {
  const uptr kPageSize = GetPageSizeCached();
  CHECK_GE(max_depth, 2);
  trace_buffer[0] = pc;
  size = 1;
  if (stack_top < 4096) return;  // Sanity check for stack top.
  uhwptr *frame = GetCanonicFrame(bp, stack_top, stack_bottom);
  // Lowest possible address that makes sense as the next frame pointer.
  // Goes up as we walk the stack.
  uptr bottom = stack_bottom;
  // Avoid infinite loop when frame == frame[0] by using frame > prev_frame.
  while (IsValidFrame((uptr)frame, stack_top, bottom) &&
         IsAligned((uptr)frame, sizeof(*frame)) &&
         size < max_depth) {
#ifdef __powerpc__
    // PowerPC ABIs specify that the return address is saved at offset
    // 16 of the *caller's* stack frame.  Thus we must dereference the
    // back chain to find the caller frame before extracting it.
    uhwptr *caller_frame = (uhwptr*)frame[0];
    if (!IsValidFrame((uptr)caller_frame, stack_top, bottom) ||
        !IsAligned((uptr)caller_frame, sizeof(uhwptr)))
      break;
    uhwptr pc1 = caller_frame[2];
#elif defined(__s390__)
    uhwptr pc1 = frame[14];
#else
    uhwptr pc1 = frame[1];
#endif
    // Let's assume that any pointer in the 0th page (i.e. <0x1000 on i386 and
    // x86_64) is invalid and stop unwinding here.  If we're adding support for
    // a platform where this isn't true, we need to reconsider this check.
    if (pc1 < kPageSize)
      break;
    if (pc1 != pc) {
      trace_buffer[size++] = (uptr) pc1;
    }
    bottom = (uptr)frame;
    frame = GetCanonicFrame((uptr)frame[0], stack_top, bottom);
  }
}

#endif  // !defined(__sparc__)

void BufferedStackTrace::PopStackFrames(uptr count) {
  CHECK_LT(count, size);
  size -= count;
  for (uptr i = 0; i < size; ++i) {
    trace_buffer[i] = trace_buffer[i + count];
  }
}

static uptr Distance(uptr a, uptr b) { return a < b ? b - a : a - b; }

uptr BufferedStackTrace::LocatePcInTrace(uptr pc) {
  uptr best = 0;
  for (uptr i = 1; i < size; ++i) {
    if (Distance(trace[i], pc) < Distance(trace[best], pc)) best = i;
  }
  return best;
}

}  // namespace __sanitizer
