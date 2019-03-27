//===-- sanitizer_stackdepot.h ----------------------------------*- C++ -*-===//
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

#ifndef SANITIZER_STACKDEPOT_H
#define SANITIZER_STACKDEPOT_H

#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_stacktrace.h"

namespace __sanitizer {

// StackDepot efficiently stores huge amounts of stack traces.
struct StackDepotNode;
struct StackDepotHandle {
  StackDepotNode *node_;
  StackDepotHandle() : node_(nullptr) {}
  explicit StackDepotHandle(StackDepotNode *node) : node_(node) {}
  bool valid() { return node_; }
  u32 id();
  int use_count();
  void inc_use_count_unsafe();
};

const int kStackDepotMaxUseCount = 1U << (SANITIZER_ANDROID ? 16 : 20);

StackDepotStats *StackDepotGetStats();
u32 StackDepotPut(StackTrace stack);
StackDepotHandle StackDepotPut_WithHandle(StackTrace stack);
// Retrieves a stored stack trace by the id.
StackTrace StackDepotGet(u32 id);

void StackDepotLockAll();
void StackDepotUnlockAll();

// Instantiating this class creates a snapshot of StackDepot which can be
// efficiently queried with StackDepotGet(). You can use it concurrently with
// StackDepot, but the snapshot is only guaranteed to contain those stack traces
// which were stored before it was instantiated.
class StackDepotReverseMap {
 public:
  StackDepotReverseMap();
  StackTrace Get(u32 id);

 private:
  struct IdDescPair {
    u32 id;
    StackDepotNode *desc;

    static bool IdComparator(const IdDescPair &a, const IdDescPair &b);
  };

  InternalMmapVector<IdDescPair> map_;

  // Disallow evil constructors.
  StackDepotReverseMap(const StackDepotReverseMap&);
  void operator=(const StackDepotReverseMap&);
};

} // namespace __sanitizer

#endif // SANITIZER_STACKDEPOT_H
