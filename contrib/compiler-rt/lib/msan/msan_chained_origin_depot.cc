//===-- msan_chained_origin_depot.cc -----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A storage for chained origins.
//===----------------------------------------------------------------------===//

#include "msan_chained_origin_depot.h"

#include "sanitizer_common/sanitizer_stackdepotbase.h"

namespace __msan {

struct ChainedOriginDepotDesc {
  u32 here_id;
  u32 prev_id;
};

struct ChainedOriginDepotNode {
  ChainedOriginDepotNode *link;
  u32 id;
  u32 here_id;
  u32 prev_id;

  typedef ChainedOriginDepotDesc args_type;

  bool eq(u32 hash, const args_type &args) const {
    return here_id == args.here_id && prev_id == args.prev_id;
  }

  static uptr storage_size(const args_type &args) {
    return sizeof(ChainedOriginDepotNode);
  }

  /* This is murmur2 hash for the 64->32 bit case.
     It does not behave all that well because the keys have a very biased
     distribution (I've seen 7-element buckets with the table only 14% full).

     here_id is built of
     * (1 bits) Reserved, zero.
     * (8 bits) Part id = bits 13..20 of the hash value of here_id's key.
     * (23 bits) Sequential number (each part has each own sequence).

     prev_id has either the same distribution as here_id (but with 3:8:21)
     split, or one of two reserved values (-1) or (-2). Either case can
     dominate depending on the workload.
  */
  static u32 hash(const args_type &args) {
    const u32 m = 0x5bd1e995;
    const u32 seed = 0x9747b28c;
    const u32 r = 24;
    u32 h = seed;
    u32 k = args.here_id;
    k *= m;
    k ^= k >> r;
    k *= m;
    h *= m;
    h ^= k;

    k = args.prev_id;
    k *= m;
    k ^= k >> r;
    k *= m;
    h *= m;
    h ^= k;

    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;
    return h;
  }
  static bool is_valid(const args_type &args) { return true; }
  void store(const args_type &args, u32 other_hash) {
    here_id = args.here_id;
    prev_id = args.prev_id;
  }

  args_type load() const {
    args_type ret = {here_id, prev_id};
    return ret;
  }

  struct Handle {
    ChainedOriginDepotNode *node_;
    Handle() : node_(nullptr) {}
    explicit Handle(ChainedOriginDepotNode *node) : node_(node) {}
    bool valid() { return node_; }
    u32 id() { return node_->id; }
    int here_id() { return node_->here_id; }
    int prev_id() { return node_->prev_id; }
  };

  Handle get_handle() { return Handle(this); }

  typedef Handle handle_type;
};

static StackDepotBase<ChainedOriginDepotNode, 4, 20> chainedOriginDepot;

StackDepotStats *ChainedOriginDepotGetStats() {
  return chainedOriginDepot.GetStats();
}

bool ChainedOriginDepotPut(u32 here_id, u32 prev_id, u32 *new_id) {
  ChainedOriginDepotDesc desc = {here_id, prev_id};
  bool inserted;
  ChainedOriginDepotNode::Handle h = chainedOriginDepot.Put(desc, &inserted);
  *new_id = h.valid() ? h.id() : 0;
  return inserted;
}

// Retrieves a stored stack trace by the id.
u32 ChainedOriginDepotGet(u32 id, u32 *other) {
  ChainedOriginDepotDesc desc = chainedOriginDepot.Get(id);
  *other = desc.prev_id;
  return desc.here_id;
}

void ChainedOriginDepotLockAll() {
  chainedOriginDepot.LockAll();
}

void ChainedOriginDepotUnlockAll() {
  chainedOriginDepot.UnlockAll();
}

} // namespace __msan
