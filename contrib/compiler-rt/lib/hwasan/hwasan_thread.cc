
#include "hwasan.h"
#include "hwasan_mapping.h"
#include "hwasan_thread.h"
#include "hwasan_poisoning.h"
#include "hwasan_interface_internal.h"

#include "sanitizer_common/sanitizer_file.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"


namespace __hwasan {

static u32 RandomSeed() {
  u32 seed;
  do {
    if (UNLIKELY(!GetRandom(reinterpret_cast<void *>(&seed), sizeof(seed),
                            /*blocking=*/false))) {
      seed = static_cast<u32>(
          (NanoTime() >> 12) ^
          (reinterpret_cast<uptr>(__builtin_frame_address(0)) >> 4));
    }
  } while (!seed);
  return seed;
}

void Thread::Init(uptr stack_buffer_start, uptr stack_buffer_size) {
  static u64 unique_id;
  unique_id_ = unique_id++;
  random_state_ = flags()->random_tags ? RandomSeed() : unique_id_;
  if (auto sz = flags()->heap_history_size)
    heap_allocations_ = HeapAllocationsRingBuffer::New(sz);

  HwasanTSDThreadInit();  // Only needed with interceptors.
  uptr *ThreadLong = GetCurrentThreadLongPtr();
  // The following implicitly sets (this) as the current thread.
  stack_allocations_ = new (ThreadLong)
      StackAllocationsRingBuffer((void *)stack_buffer_start, stack_buffer_size);
  // Check that it worked.
  CHECK_EQ(GetCurrentThread(), this);

  // ScopedTaggingDisable needs GetCurrentThread to be set up.
  ScopedTaggingDisabler disabler;

  uptr tls_size;
  uptr stack_size;
  GetThreadStackAndTls(IsMainThread(), &stack_bottom_, &stack_size, &tls_begin_,
                       &tls_size);
  stack_top_ = stack_bottom_ + stack_size;
  tls_end_ = tls_begin_ + tls_size;

  if (stack_bottom_) {
    int local;
    CHECK(AddrIsInStack((uptr)&local));
    CHECK(MemIsApp(stack_bottom_));
    CHECK(MemIsApp(stack_top_ - 1));
  }

  if (flags()->verbose_threads) {
    if (IsMainThread()) {
      Printf("sizeof(Thread): %zd sizeof(HeapRB): %zd sizeof(StackRB): %zd\n",
             sizeof(Thread), heap_allocations_->SizeInBytes(),
             stack_allocations_->size() * sizeof(uptr));
    }
    Print("Creating  : ");
  }
}

void Thread::ClearShadowForThreadStackAndTLS() {
  if (stack_top_ != stack_bottom_)
    TagMemory(stack_bottom_, stack_top_ - stack_bottom_, 0);
  if (tls_begin_ != tls_end_)
    TagMemory(tls_begin_, tls_end_ - tls_begin_, 0);
}

void Thread::Destroy() {
  if (flags()->verbose_threads)
    Print("Destroying: ");
  AllocatorSwallowThreadLocalCache(allocator_cache());
  ClearShadowForThreadStackAndTLS();
  if (heap_allocations_)
    heap_allocations_->Delete();
  DTLS_Destroy();
}

void Thread::Print(const char *Prefix) {
  Printf("%sT%zd %p stack: [%p,%p) sz: %zd tls: [%p,%p)\n", Prefix,
         unique_id_, this, stack_bottom(), stack_top(),
         stack_top() - stack_bottom(),
         tls_begin(), tls_end());
}

static u32 xorshift(u32 state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// Generate a (pseudo-)random non-zero tag.
tag_t Thread::GenerateRandomTag() {
  if (tagging_disabled_) return 0;
  tag_t tag;
  do {
    if (flags()->random_tags) {
      if (!random_buffer_)
        random_buffer_ = random_state_ = xorshift(random_state_);
      CHECK(random_buffer_);
      tag = random_buffer_ & 0xFF;
      random_buffer_ >>= 8;
    } else {
      tag = random_state_ = (random_state_ + 1) & 0xFF;
    }
  } while (!tag);
  return tag;
}

} // namespace __hwasan
