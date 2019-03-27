//===-- hwasan_thread.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
//===----------------------------------------------------------------------===//

#ifndef HWASAN_THREAD_H
#define HWASAN_THREAD_H

#include "hwasan_allocator.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_ring_buffer.h"

namespace __hwasan {

typedef __sanitizer::CompactRingBuffer<uptr> StackAllocationsRingBuffer;

class Thread {
 public:
  void Init(uptr stack_buffer_start, uptr stack_buffer_size);  // Must be called from the thread itself.
  void Destroy();

  uptr stack_top() { return stack_top_; }
  uptr stack_bottom() { return stack_bottom_; }
  uptr stack_size() { return stack_top() - stack_bottom(); }
  uptr tls_begin() { return tls_begin_; }
  uptr tls_end() { return tls_end_; }
  bool IsMainThread() { return unique_id_ == 0; }

  bool AddrIsInStack(uptr addr) {
    return addr >= stack_bottom_ && addr < stack_top_;
  }

  bool InSignalHandler() { return in_signal_handler_; }
  void EnterSignalHandler() { in_signal_handler_++; }
  void LeaveSignalHandler() { in_signal_handler_--; }

  bool InSymbolizer() { return in_symbolizer_; }
  void EnterSymbolizer() { in_symbolizer_++; }
  void LeaveSymbolizer() { in_symbolizer_--; }

  bool InInterceptorScope() { return in_interceptor_scope_; }
  void EnterInterceptorScope() { in_interceptor_scope_++; }
  void LeaveInterceptorScope() { in_interceptor_scope_--; }

  AllocatorCache *allocator_cache() { return &allocator_cache_; }
  HeapAllocationsRingBuffer *heap_allocations() { return heap_allocations_; }
  StackAllocationsRingBuffer *stack_allocations() { return stack_allocations_; }

  tag_t GenerateRandomTag();

  void DisableTagging() { tagging_disabled_++; }
  void EnableTagging() { tagging_disabled_--; }
  bool TaggingIsDisabled() const { return tagging_disabled_; }

  u64 unique_id() const { return unique_id_; }
  void Announce() {
    if (announced_) return;
    announced_ = true;
    Print("Thread: ");
  }

 private:
  // NOTE: There is no Thread constructor. It is allocated
  // via mmap() and *must* be valid in zero-initialized state.
  void ClearShadowForThreadStackAndTLS();
  void Print(const char *prefix);
  uptr stack_top_;
  uptr stack_bottom_;
  uptr tls_begin_;
  uptr tls_end_;

  unsigned in_signal_handler_;
  unsigned in_symbolizer_;
  unsigned in_interceptor_scope_;

  u32 random_state_;
  u32 random_buffer_;

  AllocatorCache allocator_cache_;
  HeapAllocationsRingBuffer *heap_allocations_;
  StackAllocationsRingBuffer *stack_allocations_;

  static void InsertIntoThreadList(Thread *t);
  static void RemoveFromThreadList(Thread *t);
  Thread *next_;  // All live threads form a linked list.

  u64 unique_id_;  // counting from zero.

  u32 tagging_disabled_;  // if non-zero, malloc uses zero tag in this thread.

  bool announced_;

  friend struct ThreadListHead;
};

Thread *GetCurrentThread();
uptr *GetCurrentThreadLongPtr();

struct ScopedTaggingDisabler {
  ScopedTaggingDisabler() { GetCurrentThread()->DisableTagging(); }
  ~ScopedTaggingDisabler() { GetCurrentThread()->EnableTagging(); }
};

} // namespace __hwasan

#endif // HWASAN_THREAD_H
