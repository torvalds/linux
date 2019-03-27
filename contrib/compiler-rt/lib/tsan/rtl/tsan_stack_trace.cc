//===-- tsan_stack_trace.cc -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#include "tsan_stack_trace.h"
#include "tsan_rtl.h"
#include "tsan_mman.h"

namespace __tsan {

VarSizeStackTrace::VarSizeStackTrace()
    : StackTrace(nullptr, 0), trace_buffer(nullptr) {}

VarSizeStackTrace::~VarSizeStackTrace() {
  ResizeBuffer(0);
}

void VarSizeStackTrace::ResizeBuffer(uptr new_size) {
  if (trace_buffer) {
    internal_free(trace_buffer);
  }
  trace_buffer =
      (new_size > 0)
          ? (uptr *)internal_alloc(MBlockStackTrace,
                                   new_size * sizeof(trace_buffer[0]))
          : nullptr;
  trace = trace_buffer;
  size = new_size;
}

void VarSizeStackTrace::Init(const uptr *pcs, uptr cnt, uptr extra_top_pc) {
  ResizeBuffer(cnt + !!extra_top_pc);
  internal_memcpy(trace_buffer, pcs, cnt * sizeof(trace_buffer[0]));
  if (extra_top_pc)
    trace_buffer[cnt] = extra_top_pc;
}

void VarSizeStackTrace::ReverseOrder() {
  for (u32 i = 0; i < (size >> 1); i++)
    Swap(trace_buffer[i], trace_buffer[size - 1 - i]);
}

}  // namespace __tsan
