//===-- sanitizer_atomic_clang.h --------------------------------*- C++ -*-===//
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

#ifndef SANITIZER_ATOMIC_CLANG_H
#define SANITIZER_ATOMIC_CLANG_H

#if defined(__i386__) || defined(__x86_64__)
# include "sanitizer_atomic_clang_x86.h"
#else
# include "sanitizer_atomic_clang_other.h"
#endif

namespace __sanitizer {

// We would like to just use compiler builtin atomic operations
// for loads and stores, but they are mostly broken in clang:
// - they lead to vastly inefficient code generation
// (http://llvm.org/bugs/show_bug.cgi?id=17281)
// - 64-bit atomic operations are not implemented on x86_32
// (http://llvm.org/bugs/show_bug.cgi?id=15034)
// - they are not implemented on ARM
// error: undefined reference to '__atomic_load_4'

// See http://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
// for mappings of the memory model to different processors.

INLINE void atomic_signal_fence(memory_order) {
  __asm__ __volatile__("" ::: "memory");
}

INLINE void atomic_thread_fence(memory_order) {
  __sync_synchronize();
}

template<typename T>
INLINE typename T::Type atomic_fetch_add(volatile T *a,
    typename T::Type v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return __sync_fetch_and_add(&a->val_dont_use, v);
}

template<typename T>
INLINE typename T::Type atomic_fetch_sub(volatile T *a,
    typename T::Type v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return __sync_fetch_and_add(&a->val_dont_use, -v);
}

template<typename T>
INLINE typename T::Type atomic_exchange(volatile T *a,
    typename T::Type v, memory_order mo) {
  DCHECK(!((uptr)a % sizeof(*a)));
  if (mo & (memory_order_release | memory_order_acq_rel | memory_order_seq_cst))
    __sync_synchronize();
  v = __sync_lock_test_and_set(&a->val_dont_use, v);
  if (mo == memory_order_seq_cst)
    __sync_synchronize();
  return v;
}

template <typename T>
INLINE bool atomic_compare_exchange_strong(volatile T *a, typename T::Type *cmp,
                                           typename T::Type xchg,
                                           memory_order mo) {
  typedef typename T::Type Type;
  Type cmpv = *cmp;
  Type prev;
  prev = __sync_val_compare_and_swap(&a->val_dont_use, cmpv, xchg);
  if (prev == cmpv) return true;
  *cmp = prev;
  return false;
}

template<typename T>
INLINE bool atomic_compare_exchange_weak(volatile T *a,
                                         typename T::Type *cmp,
                                         typename T::Type xchg,
                                         memory_order mo) {
  return atomic_compare_exchange_strong(a, cmp, xchg, mo);
}

}  // namespace __sanitizer

// This include provides explicit template instantiations for atomic_uint64_t
// on MIPS32, which does not directly support 8 byte atomics. It has to
// proceed the template definitions above.
#if defined(_MIPS_SIM) && defined(_ABIO32)
  #include "sanitizer_atomic_clang_mips.h"
#endif

#undef ATOMIC_ORDER

#endif  // SANITIZER_ATOMIC_CLANG_H
