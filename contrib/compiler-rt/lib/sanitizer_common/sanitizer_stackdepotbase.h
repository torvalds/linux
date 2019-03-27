//===-- sanitizer_stackdepotbase.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of a mapping from arbitrary values to unique 32-bit
// identifiers.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_STACKDEPOTBASE_H
#define SANITIZER_STACKDEPOTBASE_H

#include "sanitizer_internal_defs.h"
#include "sanitizer_mutex.h"
#include "sanitizer_atomic.h"
#include "sanitizer_persistent_allocator.h"

namespace __sanitizer {

template <class Node, int kReservedBits, int kTabSizeLog>
class StackDepotBase {
 public:
  typedef typename Node::args_type args_type;
  typedef typename Node::handle_type handle_type;
  // Maps stack trace to an unique id.
  handle_type Put(args_type args, bool *inserted = nullptr);
  // Retrieves a stored stack trace by the id.
  args_type Get(u32 id);

  StackDepotStats *GetStats() { return &stats; }

  void LockAll();
  void UnlockAll();

 private:
  static Node *find(Node *s, args_type args, u32 hash);
  static Node *lock(atomic_uintptr_t *p);
  static void unlock(atomic_uintptr_t *p, Node *s);

  static const int kTabSize = 1 << kTabSizeLog;  // Hash table size.
  static const int kPartBits = 8;
  static const int kPartShift = sizeof(u32) * 8 - kPartBits - kReservedBits;
  static const int kPartCount =
      1 << kPartBits;  // Number of subparts in the table.
  static const int kPartSize = kTabSize / kPartCount;
  static const int kMaxId = 1 << kPartShift;

  atomic_uintptr_t tab[kTabSize];   // Hash table of Node's.
  atomic_uint32_t seq[kPartCount];  // Unique id generators.

  StackDepotStats stats;

  friend class StackDepotReverseMap;
};

template <class Node, int kReservedBits, int kTabSizeLog>
Node *StackDepotBase<Node, kReservedBits, kTabSizeLog>::find(Node *s,
                                                             args_type args,
                                                             u32 hash) {
  // Searches linked list s for the stack, returns its id.
  for (; s; s = s->link) {
    if (s->eq(hash, args)) {
      return s;
    }
  }
  return nullptr;
}

template <class Node, int kReservedBits, int kTabSizeLog>
Node *StackDepotBase<Node, kReservedBits, kTabSizeLog>::lock(
    atomic_uintptr_t *p) {
  // Uses the pointer lsb as mutex.
  for (int i = 0;; i++) {
    uptr cmp = atomic_load(p, memory_order_relaxed);
    if ((cmp & 1) == 0 &&
        atomic_compare_exchange_weak(p, &cmp, cmp | 1, memory_order_acquire))
      return (Node *)cmp;
    if (i < 10)
      proc_yield(10);
    else
      internal_sched_yield();
  }
}

template <class Node, int kReservedBits, int kTabSizeLog>
void StackDepotBase<Node, kReservedBits, kTabSizeLog>::unlock(
    atomic_uintptr_t *p, Node *s) {
  DCHECK_EQ((uptr)s & 1, 0);
  atomic_store(p, (uptr)s, memory_order_release);
}

template <class Node, int kReservedBits, int kTabSizeLog>
typename StackDepotBase<Node, kReservedBits, kTabSizeLog>::handle_type
StackDepotBase<Node, kReservedBits, kTabSizeLog>::Put(args_type args,
                                                      bool *inserted) {
  if (inserted) *inserted = false;
  if (!Node::is_valid(args)) return handle_type();
  uptr h = Node::hash(args);
  atomic_uintptr_t *p = &tab[h % kTabSize];
  uptr v = atomic_load(p, memory_order_consume);
  Node *s = (Node *)(v & ~1);
  // First, try to find the existing stack.
  Node *node = find(s, args, h);
  if (node) return node->get_handle();
  // If failed, lock, retry and insert new.
  Node *s2 = lock(p);
  if (s2 != s) {
    node = find(s2, args, h);
    if (node) {
      unlock(p, s2);
      return node->get_handle();
    }
  }
  uptr part = (h % kTabSize) / kPartSize;
  u32 id = atomic_fetch_add(&seq[part], 1, memory_order_relaxed) + 1;
  stats.n_uniq_ids++;
  CHECK_LT(id, kMaxId);
  id |= part << kPartShift;
  CHECK_NE(id, 0);
  CHECK_EQ(id & (((u32)-1) >> kReservedBits), id);
  uptr memsz = Node::storage_size(args);
  s = (Node *)PersistentAlloc(memsz);
  stats.allocated += memsz;
  s->id = id;
  s->store(args, h);
  s->link = s2;
  unlock(p, s);
  if (inserted) *inserted = true;
  return s->get_handle();
}

template <class Node, int kReservedBits, int kTabSizeLog>
typename StackDepotBase<Node, kReservedBits, kTabSizeLog>::args_type
StackDepotBase<Node, kReservedBits, kTabSizeLog>::Get(u32 id) {
  if (id == 0) {
    return args_type();
  }
  CHECK_EQ(id & (((u32)-1) >> kReservedBits), id);
  // High kPartBits contain part id, so we need to scan at most kPartSize lists.
  uptr part = id >> kPartShift;
  for (int i = 0; i != kPartSize; i++) {
    uptr idx = part * kPartSize + i;
    CHECK_LT(idx, kTabSize);
    atomic_uintptr_t *p = &tab[idx];
    uptr v = atomic_load(p, memory_order_consume);
    Node *s = (Node *)(v & ~1);
    for (; s; s = s->link) {
      if (s->id == id) {
        return s->load();
      }
    }
  }
  return args_type();
}

template <class Node, int kReservedBits, int kTabSizeLog>
void StackDepotBase<Node, kReservedBits, kTabSizeLog>::LockAll() {
  for (int i = 0; i < kTabSize; ++i) {
    lock(&tab[i]);
  }
}

template <class Node, int kReservedBits, int kTabSizeLog>
void StackDepotBase<Node, kReservedBits, kTabSizeLog>::UnlockAll() {
  for (int i = 0; i < kTabSize; ++i) {
    atomic_uintptr_t *p = &tab[i];
    uptr s = atomic_load(p, memory_order_relaxed);
    unlock(p, (Node *)(s & ~1UL));
  }
}

} // namespace __sanitizer

#endif // SANITIZER_STACKDEPOTBASE_H
