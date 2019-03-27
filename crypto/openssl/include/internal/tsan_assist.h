/*
 * Copyright 2018-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * Contemporary compilers implement lock-free atomic memory access
 * primitives that facilitate writing "thread-opportunistic" or even real
 * multi-threading low-overhead code. "Thread-opportunistic" is when
 * exact result is not required, e.g. some statistics, or execution flow
 * doesn't have to be unambiguous. Simplest example is lazy "constant"
 * initialization when one can synchronize on variable itself, e.g.
 *
 * if (var == NOT_YET_INITIALIZED)
 *     var = function_returning_same_value();
 *
 * This does work provided that loads and stores are single-instuction
 * operations (and integer ones are on *all* supported platforms), but
 * it upsets Thread Sanitizer. Suggested solution is
 *
 * if (tsan_load(&var) == NOT_YET_INITIALIZED)
 *     tsan_store(&var, function_returning_same_value());
 *
 * Production machine code would be the same, so one can wonder why
 * bother. Having Thread Sanitizer accept "thread-opportunistic" code
 * allows to move on trouble-shooting real bugs.
 *
 * Resolving Thread Sanitizer nits was the initial purpose for this module,
 * but it was later extended with more nuanced primitives that are useful
 * even in "non-opportunistic" scenarios. Most notably verifying if a shared
 * structure is fully initialized and bypassing the initialization lock.
 * It's suggested to view macros defined in this module as "annotations" for
 * thread-safe lock-free code, "Thread-Safe ANnotations"...
 *
 * It's assumed that ATOMIC_{LONG|INT}_LOCK_FREE are assigned same value as
 * ATOMIC_POINTER_LOCK_FREE. And check for >= 2 ensures that corresponding
 * code is inlined. It should be noted that statistics counters become
 * accurate in such case.
 *
 * Special note about TSAN_QUALIFIER. It might be undesired to use it in
 * a shared header. Because whether operation on specific variable or member
 * is atomic or not might be irrelevant in other modules. In such case one
 * can use TSAN_QUALIFIER in cast specifically when it has to count.
 */

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L \
    && !defined(__STDC_NO_ATOMICS__)
# include <stdatomic.h>

# if defined(ATOMIC_POINTER_LOCK_FREE) \
          && ATOMIC_POINTER_LOCK_FREE >= 2
#  define TSAN_QUALIFIER _Atomic
#  define tsan_load(ptr) atomic_load_explicit((ptr), memory_order_relaxed)
#  define tsan_store(ptr, val) atomic_store_explicit((ptr), (val), memory_order_relaxed)
#  define tsan_counter(ptr) atomic_fetch_add_explicit((ptr), 1, memory_order_relaxed)
#  define tsan_decr(ptr) atomic_fetch_add_explicit((ptr), -1, memory_order_relaxed)
#  define tsan_ld_acq(ptr) atomic_load_explicit((ptr), memory_order_acquire)
#  define tsan_st_rel(ptr, val) atomic_store_explicit((ptr), (val), memory_order_release)
# endif

#elif defined(__GNUC__) && defined(__ATOMIC_RELAXED)

# if defined(__GCC_ATOMIC_POINTER_LOCK_FREE) \
          && __GCC_ATOMIC_POINTER_LOCK_FREE >= 2
#  define TSAN_QUALIFIER volatile
#  define tsan_load(ptr) __atomic_load_n((ptr), __ATOMIC_RELAXED)
#  define tsan_store(ptr, val) __atomic_store_n((ptr), (val), __ATOMIC_RELAXED)
#  define tsan_counter(ptr) __atomic_fetch_add((ptr), 1, __ATOMIC_RELAXED)
#  define tsan_decr(ptr) __atomic_fetch_add((ptr), -1, __ATOMIC_RELAXED)
#  define tsan_ld_acq(ptr) __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
#  define tsan_st_rel(ptr, val) __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
# endif

#elif defined(_MSC_VER) && _MSC_VER>=1200 \
      && (defined(_M_IX86) || defined(_M_AMD64) || defined(_M_X64) || \
          defined(_M_ARM64) || (defined(_M_ARM) && _M_ARM >= 7))
/*
 * There is subtle dependency on /volatile:<iso|ms> command-line option.
 * "ms" implies same semantic as memory_order_acquire for loads and
 * memory_order_release for stores, while "iso" - memory_order_relaxed for
 * either. Real complication is that defaults are different on x86 and ARM.
 * There is explanation for that, "ms" is backward compatible with earlier
 * compiler versions, while multi-processor ARM can be viewed as brand new
 * platform to MSC and its users, and with non-relaxed semantic taking toll
 * with additional instructions and penalties, it kind of makes sense to
 * default to "iso"...
 */
# define TSAN_QUALIFIER volatile
# if defined(_M_ARM) || defined(_M_ARM64)
#  define _InterlockedExchangeAdd _InterlockedExchangeAdd_nf
#  pragma intrinsic(_InterlockedExchangeAdd_nf)
#  pragma intrinsic(__iso_volatile_load32, __iso_volatile_store32)
#  ifdef _WIN64
#   define _InterlockedExchangeAdd64 _InterlockedExchangeAdd64_nf
#   pragma intrinsic(_InterlockedExchangeAdd64_nf)
#   pragma intrinsic(__iso_volatile_load64, __iso_volatile_store64)
#   define tsan_load(ptr) (sizeof(*(ptr)) == 8 ? __iso_volatile_load64(ptr) \
                                               : __iso_volatile_load32(ptr))
#   define tsan_store(ptr, val) (sizeof(*(ptr)) == 8 ? __iso_volatile_store64((ptr), (val)) \
                                                     : __iso_volatile_store32((ptr), (val)))
#  else
#   define tsan_load(ptr) __iso_volatile_load32(ptr)
#   define tsan_store(ptr, val) __iso_volatile_store32((ptr), (val))
#  endif
# else
#  define tsan_load(ptr) (*(ptr))
#  define tsan_store(ptr, val) (*(ptr) = (val))
# endif
# pragma intrinsic(_InterlockedExchangeAdd)
# ifdef _WIN64
#  pragma intrinsic(_InterlockedExchangeAdd64)
#  define tsan_counter(ptr) (sizeof(*(ptr)) == 8 ? _InterlockedExchangeAdd64((ptr), 1) \
                                                 : _InterlockedExchangeAdd((ptr), 1))
#  define tsan_decr(ptr) (sizeof(*(ptr)) == 8 ? _InterlockedExchangeAdd64((ptr), -1) \
                                                 : _InterlockedExchangeAdd((ptr), -1))
# else
#  define tsan_counter(ptr) _InterlockedExchangeAdd((ptr), 1)
#  define tsan_decr(ptr) _InterlockedExchangeAdd((ptr), -1)
# endif
# if !defined(_ISO_VOLATILE)
#  define tsan_ld_acq(ptr) (*(ptr))
#  define tsan_st_rel(ptr, val) (*(ptr) = (val))
# endif

#endif

#ifndef TSAN_QUALIFIER

# define TSAN_QUALIFIER volatile
# define tsan_load(ptr) (*(ptr))
# define tsan_store(ptr, val) (*(ptr) = (val))
# define tsan_counter(ptr) ((*(ptr))++)
# define tsan_decr(ptr) ((*(ptr))--)
/*
 * Lack of tsan_ld_acq and tsan_ld_rel means that compiler support is not
 * sophisticated enough to support them. Code that relies on them should be
 * protected with #ifdef tsan_ld_acq with locked fallback.
 */

#endif
