//===-- sanitizer_atomic_clang_other.h --------------------------*- C++ -*-===//
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

#ifndef SANITIZER_ATOMIC_CLANG_OTHER_H
#define SANITIZER_ATOMIC_CLANG_OTHER_H

namespace __sanitizer {


INLINE void proc_yield(int cnt) {
  __asm__ __volatile__("" ::: "memory");
}

template<typename T>
INLINE typename T::Type atomic_load(
    const volatile T *a, memory_order mo) {
  DCHECK(mo & (memory_order_relaxed | memory_order_consume
      | memory_order_acquire | memory_order_seq_cst));
  DCHECK(!((uptr)a % sizeof(*a)));
  typename T::Type v;

  if (sizeof(*a) < 8 || sizeof(void*) == 8) {
    // Assume that aligned loads are atomic.
    if (mo == memory_order_relaxed) {
      v = a->val_dont_use;
    } else if (mo == memory_order_consume) {
      // Assume that processor respects data dependencies
      // (and that compiler won't break them).
      __asm__ __volatile__("" ::: "memory");
      v = a->val_dont_use;
      __asm__ __volatile__("" ::: "memory");
    } else if (mo == memory_order_acquire) {
      __asm__ __volatile__("" ::: "memory");
      v = a->val_dont_use;
      __sync_synchronize();
    } else {  // seq_cst
      // E.g. on POWER we need a hw fence even before the store.
      __sync_synchronize();
      v = a->val_dont_use;
      __sync_synchronize();
    }
  } else {
    // 64-bit load on 32-bit platform.
    // Gross, but simple and reliable.
    // Assume that it is not in read-only memory.
    v = __sync_fetch_and_add(
        const_cast<typename T::Type volatile *>(&a->val_dont_use), 0);
  }
  return v;
}

template<typename T>
INLINE void atomic_store(volatile T *a, typename T::Type v, memory_order mo) {
  DCHECK(mo & (memory_order_relaxed | memory_order_release
      | memory_order_seq_cst));
  DCHECK(!((uptr)a % sizeof(*a)));

  if (sizeof(*a) < 8 || sizeof(void*) == 8) {
    // Assume that aligned loads are atomic.
    if (mo == memory_order_relaxed) {
      a->val_dont_use = v;
    } else if (mo == memory_order_release) {
      __sync_synchronize();
      a->val_dont_use = v;
      __asm__ __volatile__("" ::: "memory");
    } else {  // seq_cst
      __sync_synchronize();
      a->val_dont_use = v;
      __sync_synchronize();
    }
  } else {
    // 64-bit store on 32-bit platform.
    // Gross, but simple and reliable.
    typename T::Type cmp = a->val_dont_use;
    typename T::Type cur;
    for (;;) {
      cur = __sync_val_compare_and_swap(&a->val_dont_use, cmp, v);
      if (cur == cmp || cur == v)
        break;
      cmp = cur;
    }
  }
}

}  // namespace __sanitizer

#endif  // #ifndef SANITIZER_ATOMIC_CLANG_OTHER_H
