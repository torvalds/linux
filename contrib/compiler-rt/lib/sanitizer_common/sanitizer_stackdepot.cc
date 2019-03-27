//===-- sanitizer_stackdepot.cc -------------------------------------------===//
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

#include "sanitizer_stackdepot.h"

#include "sanitizer_common.h"
#include "sanitizer_stackdepotbase.h"

namespace __sanitizer {

struct StackDepotNode {
  StackDepotNode *link;
  u32 id;
  atomic_uint32_t hash_and_use_count; // hash_bits : 12; use_count : 20;
  u32 size;
  u32 tag;
  uptr stack[1];  // [size]

  static const u32 kTabSizeLog = SANITIZER_ANDROID ? 16 : 20;
  // Lower kTabSizeLog bits are equal for all items in one bucket.
  // We use these bits to store the per-stack use counter.
  static const u32 kUseCountBits = kTabSizeLog;
  static const u32 kMaxUseCount = 1 << kUseCountBits;
  static const u32 kUseCountMask = (1 << kUseCountBits) - 1;
  static const u32 kHashMask = ~kUseCountMask;

  typedef StackTrace args_type;
  bool eq(u32 hash, const args_type &args) const {
    u32 hash_bits =
        atomic_load(&hash_and_use_count, memory_order_relaxed) & kHashMask;
    if ((hash & kHashMask) != hash_bits || args.size != size || args.tag != tag)
      return false;
    uptr i = 0;
    for (; i < size; i++) {
      if (stack[i] != args.trace[i]) return false;
    }
    return true;
  }
  static uptr storage_size(const args_type &args) {
    return sizeof(StackDepotNode) + (args.size - 1) * sizeof(uptr);
  }
  static u32 hash(const args_type &args) {
    // murmur2
    const u32 m = 0x5bd1e995;
    const u32 seed = 0x9747b28c;
    const u32 r = 24;
    u32 h = seed ^ (args.size * sizeof(uptr));
    for (uptr i = 0; i < args.size; i++) {
      u32 k = args.trace[i];
      k *= m;
      k ^= k >> r;
      k *= m;
      h *= m;
      h ^= k;
    }
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;
    return h;
  }
  static bool is_valid(const args_type &args) {
    return args.size > 0 && args.trace;
  }
  void store(const args_type &args, u32 hash) {
    atomic_store(&hash_and_use_count, hash & kHashMask, memory_order_relaxed);
    size = args.size;
    tag = args.tag;
    internal_memcpy(stack, args.trace, size * sizeof(uptr));
  }
  args_type load() const {
    return args_type(&stack[0], size, tag);
  }
  StackDepotHandle get_handle() { return StackDepotHandle(this); }

  typedef StackDepotHandle handle_type;
};

COMPILER_CHECK(StackDepotNode::kMaxUseCount == (u32)kStackDepotMaxUseCount);

u32 StackDepotHandle::id() { return node_->id; }
int StackDepotHandle::use_count() {
  return atomic_load(&node_->hash_and_use_count, memory_order_relaxed) &
         StackDepotNode::kUseCountMask;
}
void StackDepotHandle::inc_use_count_unsafe() {
  u32 prev =
      atomic_fetch_add(&node_->hash_and_use_count, 1, memory_order_relaxed) &
      StackDepotNode::kUseCountMask;
  CHECK_LT(prev + 1, StackDepotNode::kMaxUseCount);
}

// FIXME(dvyukov): this single reserved bit is used in TSan.
typedef StackDepotBase<StackDepotNode, 1, StackDepotNode::kTabSizeLog>
    StackDepot;
static StackDepot theDepot;

StackDepotStats *StackDepotGetStats() {
  return theDepot.GetStats();
}

u32 StackDepotPut(StackTrace stack) {
  StackDepotHandle h = theDepot.Put(stack);
  return h.valid() ? h.id() : 0;
}

StackDepotHandle StackDepotPut_WithHandle(StackTrace stack) {
  return theDepot.Put(stack);
}

StackTrace StackDepotGet(u32 id) {
  return theDepot.Get(id);
}

void StackDepotLockAll() {
  theDepot.LockAll();
}

void StackDepotUnlockAll() {
  theDepot.UnlockAll();
}

bool StackDepotReverseMap::IdDescPair::IdComparator(
    const StackDepotReverseMap::IdDescPair &a,
    const StackDepotReverseMap::IdDescPair &b) {
  return a.id < b.id;
}

StackDepotReverseMap::StackDepotReverseMap() {
  map_.reserve(StackDepotGetStats()->n_uniq_ids + 100);
  for (int idx = 0; idx < StackDepot::kTabSize; idx++) {
    atomic_uintptr_t *p = &theDepot.tab[idx];
    uptr v = atomic_load(p, memory_order_consume);
    StackDepotNode *s = (StackDepotNode*)(v & ~1);
    for (; s; s = s->link) {
      IdDescPair pair = {s->id, s};
      map_.push_back(pair);
    }
  }
  Sort(map_.data(), map_.size(), &IdDescPair::IdComparator);
}

StackTrace StackDepotReverseMap::Get(u32 id) {
  if (!map_.size())
    return StackTrace();
  IdDescPair pair = {id, nullptr};
  uptr idx =
      InternalLowerBound(map_, 0, map_.size(), pair, IdDescPair::IdComparator);
  if (idx > map_.size() || map_[idx].id != id)
    return StackTrace();
  return map_[idx].desc->load();
}

} // namespace __sanitizer
