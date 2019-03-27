//===-- hwasan_allocator.cc ------------------------- ---------------------===//
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
// HWAddressSanitizer allocator.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "hwasan.h"
#include "hwasan_allocator.h"
#include "hwasan_mapping.h"
#include "hwasan_thread.h"
#include "hwasan_report.h"

#if HWASAN_WITH_INTERCEPTORS
DEFINE_REAL(void *, realloc, void *ptr, uptr size)
DEFINE_REAL(void, free, void *ptr)
#endif

namespace __hwasan {

static Allocator allocator;
static AllocatorCache fallback_allocator_cache;
static SpinMutex fallback_mutex;
static atomic_uint8_t hwasan_allocator_tagging_enabled;

static const tag_t kFallbackAllocTag = 0xBB;
static const tag_t kFallbackFreeTag = 0xBC;

enum RightAlignMode {
  kRightAlignNever,
  kRightAlignSometimes,
  kRightAlignAlways
};

// These two variables are initialized from flags()->malloc_align_right
// in HwasanAllocatorInit and are never changed afterwards.
static RightAlignMode right_align_mode = kRightAlignNever;
static bool right_align_8 = false;

// Initialized in HwasanAllocatorInit, an never changed.
static ALIGNED(16) u8 tail_magic[kShadowAlignment];

bool HwasanChunkView::IsAllocated() const {
  return metadata_ && metadata_->alloc_context_id && metadata_->requested_size;
}

// Aligns the 'addr' right to the granule boundary.
static uptr AlignRight(uptr addr, uptr requested_size) {
  uptr tail_size = requested_size % kShadowAlignment;
  if (!tail_size) return addr;
  if (right_align_8)
    return tail_size > 8 ? addr : addr + 8;
  return addr + kShadowAlignment - tail_size;
}

uptr HwasanChunkView::Beg() const {
  if (metadata_ && metadata_->right_aligned)
    return AlignRight(block_, metadata_->requested_size);
  return block_;
}
uptr HwasanChunkView::End() const {
  return Beg() + UsedSize();
}
uptr HwasanChunkView::UsedSize() const {
  return metadata_->requested_size;
}
u32 HwasanChunkView::GetAllocStackId() const {
  return metadata_->alloc_context_id;
}

uptr HwasanChunkView::ActualSize() const {
  return allocator.GetActuallyAllocatedSize(reinterpret_cast<void *>(block_));
}

bool HwasanChunkView::FromSmallHeap() const {
  return allocator.FromPrimary(reinterpret_cast<void *>(block_));
}

void GetAllocatorStats(AllocatorStatCounters s) {
  allocator.GetStats(s);
}

void HwasanAllocatorInit() {
  atomic_store_relaxed(&hwasan_allocator_tagging_enabled,
                       !flags()->disable_allocator_tagging);
  SetAllocatorMayReturnNull(common_flags()->allocator_may_return_null);
  allocator.Init(common_flags()->allocator_release_to_os_interval_ms);
  switch (flags()->malloc_align_right) {
    case 0: break;
    case 1:
      right_align_mode = kRightAlignSometimes;
      right_align_8 = false;
      break;
    case 2:
      right_align_mode = kRightAlignAlways;
      right_align_8 = false;
      break;
    case 8:
      right_align_mode = kRightAlignSometimes;
      right_align_8 = true;
      break;
    case 9:
      right_align_mode = kRightAlignAlways;
      right_align_8 = true;
      break;
    default:
      Report("ERROR: unsupported value of malloc_align_right flag: %d\n",
             flags()->malloc_align_right);
      Die();
  }
  for (uptr i = 0; i < kShadowAlignment; i++)
    tail_magic[i] = GetCurrentThread()->GenerateRandomTag();
}

void AllocatorSwallowThreadLocalCache(AllocatorCache *cache) {
  allocator.SwallowCache(cache);
}

static uptr TaggedSize(uptr size) {
  if (!size) size = 1;
  uptr new_size = RoundUpTo(size, kShadowAlignment);
  CHECK_GE(new_size, size);
  return new_size;
}

static void *HwasanAllocate(StackTrace *stack, uptr orig_size, uptr alignment,
                            bool zeroise) {
  if (orig_size > kMaxAllowedMallocSize) {
    if (AllocatorMayReturnNull()) {
      Report("WARNING: HWAddressSanitizer failed to allocate 0x%zx bytes\n",
             orig_size);
      return nullptr;
    }
    ReportAllocationSizeTooBig(orig_size, kMaxAllowedMallocSize, stack);
  }

  alignment = Max(alignment, kShadowAlignment);
  uptr size = TaggedSize(orig_size);
  Thread *t = GetCurrentThread();
  void *allocated;
  if (t) {
    allocated = allocator.Allocate(t->allocator_cache(), size, alignment);
  } else {
    SpinMutexLock l(&fallback_mutex);
    AllocatorCache *cache = &fallback_allocator_cache;
    allocated = allocator.Allocate(cache, size, alignment);
  }
  if (UNLIKELY(!allocated)) {
    SetAllocatorOutOfMemory();
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportOutOfMemory(size, stack);
  }
  Metadata *meta =
      reinterpret_cast<Metadata *>(allocator.GetMetaData(allocated));
  meta->requested_size = static_cast<u32>(orig_size);
  meta->alloc_context_id = StackDepotPut(*stack);
  meta->right_aligned = false;
  if (zeroise) {
    internal_memset(allocated, 0, size);
  } else if (flags()->max_malloc_fill_size > 0) {
    uptr fill_size = Min(size, (uptr)flags()->max_malloc_fill_size);
    internal_memset(allocated, flags()->malloc_fill_byte, fill_size);
  }
  if (!right_align_mode)
    internal_memcpy(reinterpret_cast<u8 *>(allocated) + orig_size, tail_magic,
                    size - orig_size);

  void *user_ptr = allocated;
  if (flags()->tag_in_malloc &&
      atomic_load_relaxed(&hwasan_allocator_tagging_enabled))
    user_ptr = (void *)TagMemoryAligned(
        (uptr)user_ptr, size, t ? t->GenerateRandomTag() : kFallbackAllocTag);

  if ((orig_size % kShadowAlignment) && (alignment <= kShadowAlignment) &&
      right_align_mode) {
    uptr as_uptr = reinterpret_cast<uptr>(user_ptr);
    if (right_align_mode == kRightAlignAlways ||
        GetTagFromPointer(as_uptr) & 1) {  // use a tag bit as a random bit.
      user_ptr = reinterpret_cast<void *>(AlignRight(as_uptr, orig_size));
      meta->right_aligned = 1;
    }
  }

  HWASAN_MALLOC_HOOK(user_ptr, size);
  return user_ptr;
}

static bool PointerAndMemoryTagsMatch(void *tagged_ptr) {
  CHECK(tagged_ptr);
  tag_t ptr_tag = GetTagFromPointer(reinterpret_cast<uptr>(tagged_ptr));
  tag_t mem_tag = *reinterpret_cast<tag_t *>(
      MemToShadow(reinterpret_cast<uptr>(UntagPtr(tagged_ptr))));
  return ptr_tag == mem_tag;
}

static void HwasanDeallocate(StackTrace *stack, void *tagged_ptr) {
  CHECK(tagged_ptr);
  HWASAN_FREE_HOOK(tagged_ptr);

  if (!PointerAndMemoryTagsMatch(tagged_ptr))
    ReportInvalidFree(stack, reinterpret_cast<uptr>(tagged_ptr));

  void *untagged_ptr = UntagPtr(tagged_ptr);
  void *aligned_ptr = reinterpret_cast<void *>(
      RoundDownTo(reinterpret_cast<uptr>(untagged_ptr), kShadowAlignment));
  Metadata *meta =
      reinterpret_cast<Metadata *>(allocator.GetMetaData(aligned_ptr));
  uptr orig_size = meta->requested_size;
  u32 free_context_id = StackDepotPut(*stack);
  u32 alloc_context_id = meta->alloc_context_id;

  // Check tail magic.
  uptr tagged_size = TaggedSize(orig_size);
  if (flags()->free_checks_tail_magic && !right_align_mode && orig_size) {
    uptr tail_size = tagged_size - orig_size;
    CHECK_LT(tail_size, kShadowAlignment);
    void *tail_beg = reinterpret_cast<void *>(
        reinterpret_cast<uptr>(aligned_ptr) + orig_size);
    if (tail_size && internal_memcmp(tail_beg, tail_magic, tail_size))
      ReportTailOverwritten(stack, reinterpret_cast<uptr>(tagged_ptr),
                            orig_size, tail_size, tail_magic);
  }

  meta->requested_size = 0;
  meta->alloc_context_id = 0;
  // This memory will not be reused by anyone else, so we are free to keep it
  // poisoned.
  Thread *t = GetCurrentThread();
  if (flags()->max_free_fill_size > 0) {
    uptr fill_size =
        Min(TaggedSize(orig_size), (uptr)flags()->max_free_fill_size);
    internal_memset(aligned_ptr, flags()->free_fill_byte, fill_size);
  }
  if (flags()->tag_in_free &&
      atomic_load_relaxed(&hwasan_allocator_tagging_enabled))
    TagMemoryAligned(reinterpret_cast<uptr>(aligned_ptr), TaggedSize(orig_size),
                     t ? t->GenerateRandomTag() : kFallbackFreeTag);
  if (t) {
    allocator.Deallocate(t->allocator_cache(), aligned_ptr);
    if (auto *ha = t->heap_allocations())
      ha->push({reinterpret_cast<uptr>(tagged_ptr), alloc_context_id,
                free_context_id, static_cast<u32>(orig_size)});
  } else {
    SpinMutexLock l(&fallback_mutex);
    AllocatorCache *cache = &fallback_allocator_cache;
    allocator.Deallocate(cache, aligned_ptr);
  }
}

static void *HwasanReallocate(StackTrace *stack, void *tagged_ptr_old,
                              uptr new_size, uptr alignment) {
  if (!PointerAndMemoryTagsMatch(tagged_ptr_old))
    ReportInvalidFree(stack, reinterpret_cast<uptr>(tagged_ptr_old));

  void *tagged_ptr_new =
      HwasanAllocate(stack, new_size, alignment, false /*zeroise*/);
  if (tagged_ptr_old && tagged_ptr_new) {
    void *untagged_ptr_old =  UntagPtr(tagged_ptr_old);
    Metadata *meta =
        reinterpret_cast<Metadata *>(allocator.GetMetaData(untagged_ptr_old));
    internal_memcpy(UntagPtr(tagged_ptr_new), untagged_ptr_old,
                    Min(new_size, static_cast<uptr>(meta->requested_size)));
    HwasanDeallocate(stack, tagged_ptr_old);
  }
  return tagged_ptr_new;
}

static void *HwasanCalloc(StackTrace *stack, uptr nmemb, uptr size) {
  if (UNLIKELY(CheckForCallocOverflow(size, nmemb))) {
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportCallocOverflow(nmemb, size, stack);
  }
  return HwasanAllocate(stack, nmemb * size, sizeof(u64), true);
}

HwasanChunkView FindHeapChunkByAddress(uptr address) {
  void *block = allocator.GetBlockBegin(reinterpret_cast<void*>(address));
  if (!block)
    return HwasanChunkView();
  Metadata *metadata =
      reinterpret_cast<Metadata*>(allocator.GetMetaData(block));
  return HwasanChunkView(reinterpret_cast<uptr>(block), metadata);
}

static uptr AllocationSize(const void *tagged_ptr) {
  const void *untagged_ptr = UntagPtr(tagged_ptr);
  if (!untagged_ptr) return 0;
  const void *beg = allocator.GetBlockBegin(untagged_ptr);
  Metadata *b = (Metadata *)allocator.GetMetaData(untagged_ptr);
  if (b->right_aligned) {
    if (beg != reinterpret_cast<void *>(RoundDownTo(
                   reinterpret_cast<uptr>(untagged_ptr), kShadowAlignment)))
      return 0;
  } else {
    if (beg != untagged_ptr) return 0;
  }
  return b->requested_size;
}

void *hwasan_malloc(uptr size, StackTrace *stack) {
  return SetErrnoOnNull(HwasanAllocate(stack, size, sizeof(u64), false));
}

void *hwasan_calloc(uptr nmemb, uptr size, StackTrace *stack) {
  return SetErrnoOnNull(HwasanCalloc(stack, nmemb, size));
}

void *hwasan_realloc(void *ptr, uptr size, StackTrace *stack) {
  if (!ptr)
    return SetErrnoOnNull(HwasanAllocate(stack, size, sizeof(u64), false));

#if HWASAN_WITH_INTERCEPTORS
  // A tag of 0 means that this is a system allocator allocation, so we must use
  // the system allocator to realloc it.
  if (!flags()->disable_allocator_tagging && GetTagFromPointer((uptr)ptr) == 0)
    return REAL(realloc)(ptr, size);
#endif

  if (size == 0) {
    HwasanDeallocate(stack, ptr);
    return nullptr;
  }
  return SetErrnoOnNull(HwasanReallocate(stack, ptr, size, sizeof(u64)));
}

void *hwasan_valloc(uptr size, StackTrace *stack) {
  return SetErrnoOnNull(
      HwasanAllocate(stack, size, GetPageSizeCached(), false));
}

void *hwasan_pvalloc(uptr size, StackTrace *stack) {
  uptr PageSize = GetPageSizeCached();
  if (UNLIKELY(CheckForPvallocOverflow(size, PageSize))) {
    errno = errno_ENOMEM;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportPvallocOverflow(size, stack);
  }
  // pvalloc(0) should allocate one page.
  size = size ? RoundUpTo(size, PageSize) : PageSize;
  return SetErrnoOnNull(HwasanAllocate(stack, size, PageSize, false));
}

void *hwasan_aligned_alloc(uptr alignment, uptr size, StackTrace *stack) {
  if (UNLIKELY(!CheckAlignedAllocAlignmentAndSize(alignment, size))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportInvalidAlignedAllocAlignment(size, alignment, stack);
  }
  return SetErrnoOnNull(HwasanAllocate(stack, size, alignment, false));
}

void *hwasan_memalign(uptr alignment, uptr size, StackTrace *stack) {
  if (UNLIKELY(!IsPowerOfTwo(alignment))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportInvalidAllocationAlignment(alignment, stack);
  }
  return SetErrnoOnNull(HwasanAllocate(stack, size, alignment, false));
}

int hwasan_posix_memalign(void **memptr, uptr alignment, uptr size,
                        StackTrace *stack) {
  if (UNLIKELY(!CheckPosixMemalignAlignment(alignment))) {
    if (AllocatorMayReturnNull())
      return errno_EINVAL;
    ReportInvalidPosixMemalignAlignment(alignment, stack);
  }
  void *ptr = HwasanAllocate(stack, size, alignment, false);
  if (UNLIKELY(!ptr))
    // OOM error is already taken care of by HwasanAllocate.
    return errno_ENOMEM;
  CHECK(IsAligned((uptr)ptr, alignment));
  *memptr = ptr;
  return 0;
}

void hwasan_free(void *ptr, StackTrace *stack) {
#if HWASAN_WITH_INTERCEPTORS
  // A tag of 0 means that this is a system allocator allocation, so we must use
  // the system allocator to free it.
  if (!flags()->disable_allocator_tagging && GetTagFromPointer((uptr)ptr) == 0)
    return REAL(free)(ptr);
#endif

  return HwasanDeallocate(stack, ptr);
}

}  // namespace __hwasan

using namespace __hwasan;

void __hwasan_enable_allocator_tagging() {
  atomic_store_relaxed(&hwasan_allocator_tagging_enabled, 1);
}

void __hwasan_disable_allocator_tagging() {
#if HWASAN_WITH_INTERCEPTORS
  // Allocator tagging must be enabled for the system allocator fallback to work
  // correctly. This means that we can't disable it at runtime if it was enabled
  // at startup since that might result in our deallocations going to the system
  // allocator. If tagging was disabled at startup we avoid this problem by
  // disabling the fallback altogether.
  CHECK(flags()->disable_allocator_tagging);
#endif

  atomic_store_relaxed(&hwasan_allocator_tagging_enabled, 0);
}

uptr __sanitizer_get_current_allocated_bytes() {
  uptr stats[AllocatorStatCount];
  allocator.GetStats(stats);
  return stats[AllocatorStatAllocated];
}

uptr __sanitizer_get_heap_size() {
  uptr stats[AllocatorStatCount];
  allocator.GetStats(stats);
  return stats[AllocatorStatMapped];
}

uptr __sanitizer_get_free_bytes() { return 1; }

uptr __sanitizer_get_unmapped_bytes() { return 1; }

uptr __sanitizer_get_estimated_allocated_size(uptr size) { return size; }

int __sanitizer_get_ownership(const void *p) { return AllocationSize(p) != 0; }

uptr __sanitizer_get_allocated_size(const void *p) { return AllocationSize(p); }
