/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/crypto.h>
#include "internal/cryptlib.h"

#if defined(OPENSSL_THREADS) && !defined(CRYPTO_TDEBUG) && !defined(OPENSSL_SYS_WINDOWS)

# ifdef PTHREAD_RWLOCK_INITIALIZER
#  define USE_RWLOCK
# endif

CRYPTO_RWLOCK *CRYPTO_THREAD_lock_new(void)
{
# ifdef USE_RWLOCK
    CRYPTO_RWLOCK *lock;

    if ((lock = OPENSSL_zalloc(sizeof(pthread_rwlock_t))) == NULL) {
        /* Don't set error, to avoid recursion blowup. */
        return NULL;
    }

    if (pthread_rwlock_init(lock, NULL) != 0) {
        OPENSSL_free(lock);
        return NULL;
    }
# else
    pthread_mutexattr_t attr;
    CRYPTO_RWLOCK *lock;

    if ((lock = OPENSSL_zalloc(sizeof(pthread_mutex_t))) == NULL) {
        /* Don't set error, to avoid recursion blowup. */
        return NULL;
    }

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    if (pthread_mutex_init(lock, &attr) != 0) {
        pthread_mutexattr_destroy(&attr);
        OPENSSL_free(lock);
        return NULL;
    }

    pthread_mutexattr_destroy(&attr);
# endif

    return lock;
}

int CRYPTO_THREAD_read_lock(CRYPTO_RWLOCK *lock)
{
# ifdef USE_RWLOCK
    if (pthread_rwlock_rdlock(lock) != 0)
        return 0;
# else
    if (pthread_mutex_lock(lock) != 0)
        return 0;
# endif

    return 1;
}

int CRYPTO_THREAD_write_lock(CRYPTO_RWLOCK *lock)
{
# ifdef USE_RWLOCK
    if (pthread_rwlock_wrlock(lock) != 0)
        return 0;
# else
    if (pthread_mutex_lock(lock) != 0)
        return 0;
# endif

    return 1;
}

int CRYPTO_THREAD_unlock(CRYPTO_RWLOCK *lock)
{
# ifdef USE_RWLOCK
    if (pthread_rwlock_unlock(lock) != 0)
        return 0;
# else
    if (pthread_mutex_unlock(lock) != 0)
        return 0;
# endif

    return 1;
}

void CRYPTO_THREAD_lock_free(CRYPTO_RWLOCK *lock)
{
    if (lock == NULL)
        return;

# ifdef USE_RWLOCK
    pthread_rwlock_destroy(lock);
# else
    pthread_mutex_destroy(lock);
# endif
    OPENSSL_free(lock);

    return;
}

int CRYPTO_THREAD_run_once(CRYPTO_ONCE *once, void (*init)(void))
{
    if (pthread_once(once, init) != 0)
        return 0;

    return 1;
}

int CRYPTO_THREAD_init_local(CRYPTO_THREAD_LOCAL *key, void (*cleanup)(void *))
{
    if (pthread_key_create(key, cleanup) != 0)
        return 0;

    return 1;
}

void *CRYPTO_THREAD_get_local(CRYPTO_THREAD_LOCAL *key)
{
    return pthread_getspecific(*key);
}

int CRYPTO_THREAD_set_local(CRYPTO_THREAD_LOCAL *key, void *val)
{
    if (pthread_setspecific(*key, val) != 0)
        return 0;

    return 1;
}

int CRYPTO_THREAD_cleanup_local(CRYPTO_THREAD_LOCAL *key)
{
    if (pthread_key_delete(*key) != 0)
        return 0;

    return 1;
}

CRYPTO_THREAD_ID CRYPTO_THREAD_get_current_id(void)
{
    return pthread_self();
}

int CRYPTO_THREAD_compare_id(CRYPTO_THREAD_ID a, CRYPTO_THREAD_ID b)
{
    return pthread_equal(a, b);
}

int CRYPTO_atomic_add(int *val, int amount, int *ret, CRYPTO_RWLOCK *lock)
{
# if defined(__GNUC__) && defined(__ATOMIC_ACQ_REL) && \
     !(defined(__arm__) && __ARM_ARCH < 6)
    if (__atomic_is_lock_free(sizeof(*val), val)) {
        *ret = __atomic_add_fetch(val, amount, __ATOMIC_ACQ_REL);
        return 1;
    }
# endif
    if (!CRYPTO_THREAD_write_lock(lock))
        return 0;

    *val += amount;
    *ret  = *val;

    if (!CRYPTO_THREAD_unlock(lock))
        return 0;

    return 1;
}

# ifdef OPENSSL_SYS_UNIX
static pthread_once_t fork_once_control = PTHREAD_ONCE_INIT;

static void fork_once_func(void)
{
    pthread_atfork(OPENSSL_fork_prepare,
                   OPENSSL_fork_parent, OPENSSL_fork_child);
}
# endif

int openssl_init_fork_handlers(void)
{
# ifdef OPENSSL_SYS_UNIX
    if (pthread_once(&fork_once_control, fork_once_func) == 0)
        return 1;
# endif
    return 0;
}
#endif
