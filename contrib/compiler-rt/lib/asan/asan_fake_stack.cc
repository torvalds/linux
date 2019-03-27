//===-- asan_fake_stack.cc ------------------------------------------------===//
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
// FakeStack is used to detect use-after-return bugs.
//===----------------------------------------------------------------------===//

#include "asan_allocator.h"
#include "asan_poisoning.h"
#include "asan_thread.h"

namespace __asan {

static const u64 kMagic1 = kAsanStackAfterReturnMagic;
static const u64 kMagic2 = (kMagic1 << 8) | kMagic1;
static const u64 kMagic4 = (kMagic2 << 16) | kMagic2;
static const u64 kMagic8 = (kMagic4 << 32) | kMagic4;

static const u64 kAllocaRedzoneSize = 32UL;
static const u64 kAllocaRedzoneMask = 31UL;

// For small size classes inline PoisonShadow for better performance.
ALWAYS_INLINE void SetShadow(uptr ptr, uptr size, uptr class_id, u64 magic) {
  u64 *shadow = reinterpret_cast<u64*>(MemToShadow(ptr));
  if (SHADOW_SCALE == 3 && class_id <= 6) {
    // This code expects SHADOW_SCALE=3.
    for (uptr i = 0; i < (((uptr)1) << class_id); i++) {
      shadow[i] = magic;
      // Make sure this does not become memset.
      SanitizerBreakOptimization(nullptr);
    }
  } else {
    // The size class is too big, it's cheaper to poison only size bytes.
    PoisonShadow(ptr, size, static_cast<u8>(magic));
  }
}

FakeStack *FakeStack::Create(uptr stack_size_log) {
  static uptr kMinStackSizeLog = 16;
  static uptr kMaxStackSizeLog = FIRST_32_SECOND_64(24, 28);
  if (stack_size_log < kMinStackSizeLog)
    stack_size_log = kMinStackSizeLog;
  if (stack_size_log > kMaxStackSizeLog)
    stack_size_log = kMaxStackSizeLog;
  uptr size = RequiredSize(stack_size_log);
  FakeStack *res = reinterpret_cast<FakeStack *>(
      flags()->uar_noreserve ? MmapNoReserveOrDie(size, "FakeStack")
                             : MmapOrDie(size, "FakeStack"));
  res->stack_size_log_ = stack_size_log;
  u8 *p = reinterpret_cast<u8 *>(res);
  VReport(1, "T%d: FakeStack created: %p -- %p stack_size_log: %zd; "
          "mmapped %zdK, noreserve=%d \n",
          GetCurrentTidOrInvalid(), p,
          p + FakeStack::RequiredSize(stack_size_log), stack_size_log,
          size >> 10, flags()->uar_noreserve);
  return res;
}

void FakeStack::Destroy(int tid) {
  PoisonAll(0);
  if (Verbosity() >= 2) {
    InternalScopedString str(kNumberOfSizeClasses * 50);
    for (uptr class_id = 0; class_id < kNumberOfSizeClasses; class_id++)
      str.append("%zd: %zd/%zd; ", class_id, hint_position_[class_id],
                 NumberOfFrames(stack_size_log(), class_id));
    Report("T%d: FakeStack destroyed: %s\n", tid, str.data());
  }
  uptr size = RequiredSize(stack_size_log_);
  FlushUnneededASanShadowMemory(reinterpret_cast<uptr>(this), size);
  UnmapOrDie(this, size);
}

void FakeStack::PoisonAll(u8 magic) {
  PoisonShadow(reinterpret_cast<uptr>(this), RequiredSize(stack_size_log()),
               magic);
}

#if !defined(_MSC_VER) || defined(__clang__)
ALWAYS_INLINE USED
#endif
FakeFrame *FakeStack::Allocate(uptr stack_size_log, uptr class_id,
                               uptr real_stack) {
  CHECK_LT(class_id, kNumberOfSizeClasses);
  if (needs_gc_)
    GC(real_stack);
  uptr &hint_position = hint_position_[class_id];
  const int num_iter = NumberOfFrames(stack_size_log, class_id);
  u8 *flags = GetFlags(stack_size_log, class_id);
  for (int i = 0; i < num_iter; i++) {
    uptr pos = ModuloNumberOfFrames(stack_size_log, class_id, hint_position++);
    // This part is tricky. On one hand, checking and setting flags[pos]
    // should be atomic to ensure async-signal safety. But on the other hand,
    // if the signal arrives between checking and setting flags[pos], the
    // signal handler's fake stack will start from a different hint_position
    // and so will not touch this particular byte. So, it is safe to do this
    // with regular non-atomic load and store (at least I was not able to make
    // this code crash).
    if (flags[pos]) continue;
    flags[pos] = 1;
    FakeFrame *res = reinterpret_cast<FakeFrame *>(
        GetFrame(stack_size_log, class_id, pos));
    res->real_stack = real_stack;
    *SavedFlagPtr(reinterpret_cast<uptr>(res), class_id) = &flags[pos];
    return res;
  }
  return nullptr; // We are out of fake stack.
}

uptr FakeStack::AddrIsInFakeStack(uptr ptr, uptr *frame_beg, uptr *frame_end) {
  uptr stack_size_log = this->stack_size_log();
  uptr beg = reinterpret_cast<uptr>(GetFrame(stack_size_log, 0, 0));
  uptr end = reinterpret_cast<uptr>(this) + RequiredSize(stack_size_log);
  if (ptr < beg || ptr >= end) return 0;
  uptr class_id = (ptr - beg) >> stack_size_log;
  uptr base = beg + (class_id << stack_size_log);
  CHECK_LE(base, ptr);
  CHECK_LT(ptr, base + (((uptr)1) << stack_size_log));
  uptr pos = (ptr - base) >> (kMinStackFrameSizeLog + class_id);
  uptr res = base + pos * BytesInSizeClass(class_id);
  *frame_end = res + BytesInSizeClass(class_id);
  *frame_beg = res + sizeof(FakeFrame);
  return res;
}

void FakeStack::HandleNoReturn() {
  needs_gc_ = true;
}

// When throw, longjmp or some such happens we don't call OnFree() and
// as the result may leak one or more fake frames, but the good news is that
// we are notified about all such events by HandleNoReturn().
// If we recently had such no-return event we need to collect garbage frames.
// We do it based on their 'real_stack' values -- everything that is lower
// than the current real_stack is garbage.
NOINLINE void FakeStack::GC(uptr real_stack) {
  uptr collected = 0;
  for (uptr class_id = 0; class_id < kNumberOfSizeClasses; class_id++) {
    u8 *flags = GetFlags(stack_size_log(), class_id);
    for (uptr i = 0, n = NumberOfFrames(stack_size_log(), class_id); i < n;
         i++) {
      if (flags[i] == 0) continue;  // not allocated.
      FakeFrame *ff = reinterpret_cast<FakeFrame *>(
          GetFrame(stack_size_log(), class_id, i));
      if (ff->real_stack < real_stack) {
        flags[i] = 0;
        collected++;
      }
    }
  }
  needs_gc_ = false;
}

void FakeStack::ForEachFakeFrame(RangeIteratorCallback callback, void *arg) {
  for (uptr class_id = 0; class_id < kNumberOfSizeClasses; class_id++) {
    u8 *flags = GetFlags(stack_size_log(), class_id);
    for (uptr i = 0, n = NumberOfFrames(stack_size_log(), class_id); i < n;
         i++) {
      if (flags[i] == 0) continue;  // not allocated.
      FakeFrame *ff = reinterpret_cast<FakeFrame *>(
          GetFrame(stack_size_log(), class_id, i));
      uptr begin = reinterpret_cast<uptr>(ff);
      callback(begin, begin + FakeStack::BytesInSizeClass(class_id), arg);
    }
  }
}

#if (SANITIZER_LINUX && !SANITIZER_ANDROID) || SANITIZER_FUCHSIA
static THREADLOCAL FakeStack *fake_stack_tls;

FakeStack *GetTLSFakeStack() {
  return fake_stack_tls;
}
void SetTLSFakeStack(FakeStack *fs) {
  fake_stack_tls = fs;
}
#else
FakeStack *GetTLSFakeStack() { return 0; }
void SetTLSFakeStack(FakeStack *fs) { }
#endif  // (SANITIZER_LINUX && !SANITIZER_ANDROID) || SANITIZER_FUCHSIA

static FakeStack *GetFakeStack() {
  AsanThread *t = GetCurrentThread();
  if (!t) return nullptr;
  return t->fake_stack();
}

static FakeStack *GetFakeStackFast() {
  if (FakeStack *fs = GetTLSFakeStack())
    return fs;
  if (!__asan_option_detect_stack_use_after_return)
    return nullptr;
  return GetFakeStack();
}

ALWAYS_INLINE uptr OnMalloc(uptr class_id, uptr size) {
  FakeStack *fs = GetFakeStackFast();
  if (!fs) return 0;
  uptr local_stack;
  uptr real_stack = reinterpret_cast<uptr>(&local_stack);
  FakeFrame *ff = fs->Allocate(fs->stack_size_log(), class_id, real_stack);
  if (!ff) return 0;  // Out of fake stack.
  uptr ptr = reinterpret_cast<uptr>(ff);
  SetShadow(ptr, size, class_id, 0);
  return ptr;
}

ALWAYS_INLINE void OnFree(uptr ptr, uptr class_id, uptr size) {
  FakeStack::Deallocate(ptr, class_id);
  SetShadow(ptr, size, class_id, kMagic8);
}

} // namespace __asan

// ---------------------- Interface ---------------- {{{1
using namespace __asan;
#define DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(class_id)                       \
  extern "C" SANITIZER_INTERFACE_ATTRIBUTE uptr                                \
      __asan_stack_malloc_##class_id(uptr size) {                              \
    return OnMalloc(class_id, size);                                           \
  }                                                                            \
  extern "C" SANITIZER_INTERFACE_ATTRIBUTE void __asan_stack_free_##class_id(  \
      uptr ptr, uptr size) {                                                   \
    OnFree(ptr, class_id, size);                                               \
  }

DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(0)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(1)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(2)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(3)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(4)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(5)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(6)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(7)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(8)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(9)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(10)
extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void *__asan_get_current_fake_stack() { return GetFakeStackFast(); }

SANITIZER_INTERFACE_ATTRIBUTE
void *__asan_addr_is_in_fake_stack(void *fake_stack, void *addr, void **beg,
                                   void **end) {
  FakeStack *fs = reinterpret_cast<FakeStack*>(fake_stack);
  if (!fs) return nullptr;
  uptr frame_beg, frame_end;
  FakeFrame *frame = reinterpret_cast<FakeFrame *>(fs->AddrIsInFakeStack(
      reinterpret_cast<uptr>(addr), &frame_beg, &frame_end));
  if (!frame) return nullptr;
  if (frame->magic != kCurrentStackFrameMagic)
    return nullptr;
  if (beg) *beg = reinterpret_cast<void*>(frame_beg);
  if (end) *end = reinterpret_cast<void*>(frame_end);
  return reinterpret_cast<void*>(frame->real_stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __asan_alloca_poison(uptr addr, uptr size) {
  uptr LeftRedzoneAddr = addr - kAllocaRedzoneSize;
  uptr PartialRzAddr = addr + size;
  uptr RightRzAddr = (PartialRzAddr + kAllocaRedzoneMask) & ~kAllocaRedzoneMask;
  uptr PartialRzAligned = PartialRzAddr & ~(SHADOW_GRANULARITY - 1);
  FastPoisonShadow(LeftRedzoneAddr, kAllocaRedzoneSize, kAsanAllocaLeftMagic);
  FastPoisonShadowPartialRightRedzone(
      PartialRzAligned, PartialRzAddr % SHADOW_GRANULARITY,
      RightRzAddr - PartialRzAligned, kAsanAllocaRightMagic);
  FastPoisonShadow(RightRzAddr, kAllocaRedzoneSize, kAsanAllocaRightMagic);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __asan_allocas_unpoison(uptr top, uptr bottom) {
  if ((!top) || (top > bottom)) return;
  REAL(memset)(reinterpret_cast<void*>(MemToShadow(top)), 0,
               (bottom - top) / SHADOW_GRANULARITY);
}
} // extern "C"
