//===-- sanitizer_atomic_clang_mips.h ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
// Not intended for direct inclusion. Include sanitizer_atomic.h.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_ATOMIC_CLANG_MIPS_H
#define SANITIZER_ATOMIC_CLANG_MIPS_H

namespace __sanitizer {

// MIPS32 does not support atomics > 4 bytes. To address this lack of
// functionality, the sanitizer library provides helper methods which use an
// internal spin lock mechanism to emulate atomic oprations when the size is
// 8 bytes.
static void __spin_lock(volatile int *lock) {
  while (__sync_lock_test_and_set(lock, 1))
    while (*lock) {
    }
}

static void __spin_unlock(volatile int *lock) { __sync_lock_release(lock); }

// Make sure the lock is on its own cache line to prevent false sharing.
// Put it inside a struct that is aligned and padded to the typical MIPS
// cacheline which is 32 bytes.
static struct {
  int lock;
  char pad[32 - sizeof(int)];
} __attribute__((aligned(32))) lock = {0, {0}};

template <>
INLINE atomic_uint64_t::Type atomic_fetch_add(volatile atomic_uint64_t *ptr,
                                              atomic_uint64_t::Type val,
                                              memory_order mo) {
  DCHECK(mo &
         (memory_order_relaxed | memory_order_releasae | memory_order_seq_cst));
  DCHECK(!((uptr)ptr % sizeof(*ptr)));

  atomic_uint64_t::Type ret;

  __spin_lock(&lock.lock);
  ret = *(const_cast<atomic_uint64_t::Type volatile *>(&ptr->val_dont_use));
  ptr->val_dont_use = ret + val;
  __spin_unlock(&lock.lock);

  return ret;
}

template <>
INLINE atomic_uint64_t::Type atomic_fetch_sub(volatile atomic_uint64_t *ptr,
                                              atomic_uint64_t::Type val,
                                              memory_order mo) {
  return atomic_fetch_add(ptr, -val, mo);
}

template <>
INLINE bool atomic_compare_exchange_strong(volatile atomic_uint64_t *ptr,
                                           atomic_uint64_t::Type *cmp,
                                           atomic_uint64_t::Type xchg,
                                           memory_order mo) {
  DCHECK(mo &
         (memory_order_relaxed | memory_order_releasae | memory_order_seq_cst));
  DCHECK(!((uptr)ptr % sizeof(*ptr)));

  typedef atomic_uint64_t::Type Type;
  Type cmpv = *cmp;
  Type prev;
  bool ret = false;

  __spin_lock(&lock.lock);
  prev = *(const_cast<Type volatile *>(&ptr->val_dont_use));
  if (prev == cmpv) {
    ret = true;
    ptr->val_dont_use = xchg;
  }
  __spin_unlock(&lock.lock);

  return ret;
}

template <>
INLINE atomic_uint64_t::Type atomic_load(const volatile atomic_uint64_t *ptr,
                                         memory_order mo) {
  DCHECK(mo &
         (memory_order_relaxed | memory_order_releasae | memory_order_seq_cst));
  DCHECK(!((uptr)ptr % sizeof(*ptr)));

  atomic_uint64_t::Type zero = 0;
  volatile atomic_uint64_t *Newptr =
      const_cast<volatile atomic_uint64_t *>(ptr);
  return atomic_fetch_add(Newptr, zero, mo);
}

template <>
INLINE void atomic_store(volatile atomic_uint64_t *ptr, atomic_uint64_t::Type v,
                         memory_order mo) {
  DCHECK(mo &
         (memory_order_relaxed | memory_order_releasae | memory_order_seq_cst));
  DCHECK(!((uptr)ptr % sizeof(*ptr)));

  __spin_lock(&lock.lock);
  ptr->val_dont_use = v;
  __spin_unlock(&lock.lock);
}

}  // namespace __sanitizer

#endif  // SANITIZER_ATOMIC_CLANG_MIPS_H

