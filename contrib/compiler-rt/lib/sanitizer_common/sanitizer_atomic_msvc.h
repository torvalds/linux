//===-- sanitizer_atomic_msvc.h ---------------------------------*- C++ -*-===//
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

#ifndef SANITIZER_ATOMIC_MSVC_H
#define SANITIZER_ATOMIC_MSVC_H

extern "C" void _ReadWriteBarrier();
#pragma intrinsic(_ReadWriteBarrier)
extern "C" void _mm_mfence();
#pragma intrinsic(_mm_mfence)
extern "C" void _mm_pause();
#pragma intrinsic(_mm_pause)
extern "C" char _InterlockedExchange8(   // NOLINT
    char volatile *Addend, char Value);  // NOLINT
#pragma intrinsic(_InterlockedExchange8)
extern "C" short _InterlockedExchange16(   // NOLINT
    short volatile *Addend, short Value);  // NOLINT
#pragma intrinsic(_InterlockedExchange16)
extern "C" long _InterlockedExchange(    // NOLINT
    long volatile *Addend, long Value);  // NOLINT
#pragma intrinsic(_InterlockedExchange)
extern "C" long _InterlockedExchangeAdd(  // NOLINT
    long volatile * Addend, long Value);  // NOLINT
#pragma intrinsic(_InterlockedExchangeAdd)
extern "C" char _InterlockedCompareExchange8(  // NOLINT
    char volatile *Destination,                // NOLINT
    char Exchange, char Comparand);            // NOLINT
#pragma intrinsic(_InterlockedCompareExchange8)
extern "C" short _InterlockedCompareExchange16(  // NOLINT
    short volatile *Destination,                 // NOLINT
    short Exchange, short Comparand);            // NOLINT
#pragma intrinsic(_InterlockedCompareExchange16)
extern "C"
long long _InterlockedCompareExchange64(  // NOLINT
    long long volatile *Destination,              // NOLINT
    long long Exchange, long long Comparand);     // NOLINT
#pragma intrinsic(_InterlockedCompareExchange64)
extern "C" void *_InterlockedCompareExchangePointer(
    void *volatile *Destination,
    void *Exchange, void *Comparand);
#pragma intrinsic(_InterlockedCompareExchangePointer)
extern "C"
long __cdecl _InterlockedCompareExchange(  // NOLINT
    long volatile *Destination,            // NOLINT
    long Exchange, long Comparand);        // NOLINT
#pragma intrinsic(_InterlockedCompareExchange)

#ifdef _WIN64
extern "C" long long _InterlockedExchangeAdd64(     // NOLINT
    long long volatile * Addend, long long Value);  // NOLINT
#pragma intrinsic(_InterlockedExchangeAdd64)
#endif

namespace __sanitizer {

INLINE void atomic_signal_fence(memory_order) {
  _ReadWriteBarrier();
}

INLINE void atomic_thread_fence(memory_order) {
  _mm_mfence();
}

INLINE void proc_yield(int cnt) {
  for (int i = 0; i < cnt; i++)
    _mm_pause();
}

template<typename T>
INLINE typename T::Type atomic_load(
    const volatile T *a, memory_order mo) {
  DCHECK(mo & (memory_order_relaxed | memory_order_consume
      | memory_order_acquire | memory_order_seq_cst));
  DCHECK(!((uptr)a % sizeof(*a)));
  typename T::Type v;
  // FIXME(dvyukov): 64-bit load is not atomic on 32-bits.
  if (mo == memory_order_relaxed) {
    v = a->val_dont_use;
  } else {
    atomic_signal_fence(memory_order_seq_cst);
    v = a->val_dont_use;
    atomic_signal_fence(memory_order_seq_cst);
  }
  return v;
}

template<typename T>
INLINE void atomic_store(volatile T *a, typename T::Type v, memory_order mo) {
  DCHECK(mo & (memory_order_relaxed | memory_order_release
      | memory_order_seq_cst));
  DCHECK(!((uptr)a % sizeof(*a)));
  // FIXME(dvyukov): 64-bit store is not atomic on 32-bits.
  if (mo == memory_order_relaxed) {
    a->val_dont_use = v;
  } else {
    atomic_signal_fence(memory_order_seq_cst);
    a->val_dont_use = v;
    atomic_signal_fence(memory_order_seq_cst);
  }
  if (mo == memory_order_seq_cst)
    atomic_thread_fence(memory_order_seq_cst);
}

INLINE u32 atomic_fetch_add(volatile atomic_uint32_t *a,
    u32 v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return (u32)_InterlockedExchangeAdd(
      (volatile long*)&a->val_dont_use, (long)v);  // NOLINT
}

INLINE uptr atomic_fetch_add(volatile atomic_uintptr_t *a,
    uptr v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
#ifdef _WIN64
  return (uptr)_InterlockedExchangeAdd64(
      (volatile long long*)&a->val_dont_use, (long long)v);  // NOLINT
#else
  return (uptr)_InterlockedExchangeAdd(
      (volatile long*)&a->val_dont_use, (long)v);  // NOLINT
#endif
}

INLINE u32 atomic_fetch_sub(volatile atomic_uint32_t *a,
    u32 v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return (u32)_InterlockedExchangeAdd(
      (volatile long*)&a->val_dont_use, -(long)v);  // NOLINT
}

INLINE uptr atomic_fetch_sub(volatile atomic_uintptr_t *a,
    uptr v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
#ifdef _WIN64
  return (uptr)_InterlockedExchangeAdd64(
      (volatile long long*)&a->val_dont_use, -(long long)v);  // NOLINT
#else
  return (uptr)_InterlockedExchangeAdd(
      (volatile long*)&a->val_dont_use, -(long)v);  // NOLINT
#endif
}

INLINE u8 atomic_exchange(volatile atomic_uint8_t *a,
    u8 v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return (u8)_InterlockedExchange8((volatile char*)&a->val_dont_use, v);
}

INLINE u16 atomic_exchange(volatile atomic_uint16_t *a,
    u16 v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return (u16)_InterlockedExchange16((volatile short*)&a->val_dont_use, v);
}

INLINE u32 atomic_exchange(volatile atomic_uint32_t *a,
    u32 v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return (u32)_InterlockedExchange((volatile long*)&a->val_dont_use, v);
}

INLINE bool atomic_compare_exchange_strong(volatile atomic_uint8_t *a,
                                           u8 *cmp,
                                           u8 xchgv,
                                           memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  u8 cmpv = *cmp;
#ifdef _WIN64
  u8 prev = (u8)_InterlockedCompareExchange8(
      (volatile char*)&a->val_dont_use, (char)xchgv, (char)cmpv);
#else
  u8 prev;
  __asm {
    mov al, cmpv
    mov ecx, a
    mov dl, xchgv
    lock cmpxchg [ecx], dl
    mov prev, al
  }
#endif
  if (prev == cmpv)
    return true;
  *cmp = prev;
  return false;
}

INLINE bool atomic_compare_exchange_strong(volatile atomic_uintptr_t *a,
                                           uptr *cmp,
                                           uptr xchg,
                                           memory_order mo) {
  uptr cmpv = *cmp;
  uptr prev = (uptr)_InterlockedCompareExchangePointer(
      (void*volatile*)&a->val_dont_use, (void*)xchg, (void*)cmpv);
  if (prev == cmpv)
    return true;
  *cmp = prev;
  return false;
}

INLINE bool atomic_compare_exchange_strong(volatile atomic_uint16_t *a,
                                           u16 *cmp,
                                           u16 xchg,
                                           memory_order mo) {
  u16 cmpv = *cmp;
  u16 prev = (u16)_InterlockedCompareExchange16(
      (volatile short*)&a->val_dont_use, (short)xchg, (short)cmpv);
  if (prev == cmpv)
    return true;
  *cmp = prev;
  return false;
}

INLINE bool atomic_compare_exchange_strong(volatile atomic_uint32_t *a,
                                           u32 *cmp,
                                           u32 xchg,
                                           memory_order mo) {
  u32 cmpv = *cmp;
  u32 prev = (u32)_InterlockedCompareExchange(
      (volatile long*)&a->val_dont_use, (long)xchg, (long)cmpv);
  if (prev == cmpv)
    return true;
  *cmp = prev;
  return false;
}

INLINE bool atomic_compare_exchange_strong(volatile atomic_uint64_t *a,
                                           u64 *cmp,
                                           u64 xchg,
                                           memory_order mo) {
  u64 cmpv = *cmp;
  u64 prev = (u64)_InterlockedCompareExchange64(
      (volatile long long*)&a->val_dont_use, (long long)xchg, (long long)cmpv);
  if (prev == cmpv)
    return true;
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

#endif  // SANITIZER_ATOMIC_CLANG_H
