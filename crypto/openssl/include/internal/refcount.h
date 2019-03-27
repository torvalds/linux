/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */
#ifndef HEADER_INTERNAL_REFCOUNT_H
# define HEADER_INTERNAL_REFCOUNT_H

/* Used to checking reference counts, most while doing perl5 stuff :-) */
# if defined(OPENSSL_NO_STDIO)
#  if defined(REF_PRINT)
#   error "REF_PRINT requires stdio"
#  endif
# endif

# if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L \
     && !defined(__STDC_NO_ATOMICS__)
#  include <stdatomic.h>
#  define HAVE_C11_ATOMICS
# endif

# if defined(HAVE_C11_ATOMICS) && defined(ATOMIC_INT_LOCK_FREE) \
     && ATOMIC_INT_LOCK_FREE > 0

#  define HAVE_ATOMICS 1

typedef _Atomic int CRYPTO_REF_COUNT;

static inline int CRYPTO_UP_REF(_Atomic int *val, int *ret, void *lock)
{
    *ret = atomic_fetch_add_explicit(val, 1, memory_order_relaxed) + 1;
    return 1;
}

/*
 * Changes to shared structure other than reference counter have to be
 * serialized. And any kind of serialization implies a release fence. This
 * means that by the time reference counter is decremented all other
 * changes are visible on all processors. Hence decrement itself can be
 * relaxed. In case it hits zero, object will be destructed. Since it's
 * last use of the object, destructor programmer might reason that access
 * to mutable members doesn't have to be serialized anymore, which would
 * otherwise imply an acquire fence. Hence conditional acquire fence...
 */
static inline int CRYPTO_DOWN_REF(_Atomic int *val, int *ret, void *lock)
{
    *ret = atomic_fetch_sub_explicit(val, 1, memory_order_relaxed) - 1;
    if (*ret == 0)
        atomic_thread_fence(memory_order_acquire);
    return 1;
}

# elif defined(__GNUC__) && defined(__ATOMIC_RELAXED) && __GCC_ATOMIC_INT_LOCK_FREE > 0

#  define HAVE_ATOMICS 1

typedef int CRYPTO_REF_COUNT;

static __inline__ int CRYPTO_UP_REF(int *val, int *ret, void *lock)
{
    *ret = __atomic_fetch_add(val, 1, __ATOMIC_RELAXED) + 1;
    return 1;
}

static __inline__ int CRYPTO_DOWN_REF(int *val, int *ret, void *lock)
{
    *ret = __atomic_fetch_sub(val, 1, __ATOMIC_RELAXED) - 1;
    if (*ret == 0)
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return 1;
}

# elif defined(_MSC_VER) && _MSC_VER>=1200

#  define HAVE_ATOMICS 1

typedef volatile int CRYPTO_REF_COUNT;

#  if (defined(_M_ARM) && _M_ARM>=7) || defined(_M_ARM64)
#   include <intrin.h>
#   if defined(_M_ARM64) && !defined(_ARM_BARRIER_ISH)
#    define _ARM_BARRIER_ISH _ARM64_BARRIER_ISH
#   endif

static __inline int CRYPTO_UP_REF(volatile int *val, int *ret, void *lock)
{
    *ret = _InterlockedExchangeAdd_nf(val, 1) + 1;
    return 1;
}

static __inline int CRYPTO_DOWN_REF(volatile int *val, int *ret, void *lock)
{
    *ret = _InterlockedExchangeAdd_nf(val, -1) - 1;
    if (*ret == 0)
        __dmb(_ARM_BARRIER_ISH);
    return 1;
}
#  else
#   pragma intrinsic(_InterlockedExchangeAdd)

static __inline int CRYPTO_UP_REF(volatile int *val, int *ret, void *lock)
{
    *ret = _InterlockedExchangeAdd(val, 1) + 1;
    return 1;
}

static __inline int CRYPTO_DOWN_REF(volatile int *val, int *ret, void *lock)
{
    *ret = _InterlockedExchangeAdd(val, -1) - 1;
    return 1;
}
#  endif

# else

typedef int CRYPTO_REF_COUNT;

# define CRYPTO_UP_REF(val, ret, lock) CRYPTO_atomic_add(val, 1, ret, lock)
# define CRYPTO_DOWN_REF(val, ret, lock) CRYPTO_atomic_add(val, -1, ret, lock)

# endif

# if !defined(NDEBUG) && !defined(OPENSSL_NO_STDIO)
#  define REF_ASSERT_ISNT(test) \
    (void)((test) ? (OPENSSL_die("refcount error", __FILE__, __LINE__), 1) : 0)
# else
#  define REF_ASSERT_ISNT(i)
# endif

# ifdef REF_PRINT
#  define REF_PRINT_COUNT(a, b) \
        fprintf(stderr, "%p:%4d:%s\n", b, b->references, a)
# else
#  define REF_PRINT_COUNT(a, b)
# endif

#endif
