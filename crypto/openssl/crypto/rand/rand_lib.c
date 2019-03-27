/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <time.h>
#include "internal/cryptlib.h"
#include <openssl/opensslconf.h>
#include "internal/rand_int.h"
#include <openssl/engine.h>
#include "internal/thread_once.h"
#include "rand_lcl.h"
#include "e_os.h"

#ifndef OPENSSL_NO_ENGINE
/* non-NULL if default_RAND_meth is ENGINE-provided */
static ENGINE *funct_ref;
static CRYPTO_RWLOCK *rand_engine_lock;
#endif
static CRYPTO_RWLOCK *rand_meth_lock;
static const RAND_METHOD *default_RAND_meth;
static CRYPTO_ONCE rand_init = CRYPTO_ONCE_STATIC_INIT;

int rand_fork_count;

static CRYPTO_RWLOCK *rand_nonce_lock;
static int rand_nonce_count;

static int rand_inited = 0;

#ifdef OPENSSL_RAND_SEED_RDTSC
/*
 * IMPORTANT NOTE:  It is not currently possible to use this code
 * because we are not sure about the amount of randomness it provides.
 * Some SP900 tests have been run, but there is internal skepticism.
 * So for now this code is not used.
 */
# error "RDTSC enabled?  Should not be possible!"

/*
 * Acquire entropy from high-speed clock
 *
 * Since we get some randomness from the low-order bits of the
 * high-speed clock, it can help.
 *
 * Returns the total entropy count, if it exceeds the requested
 * entropy count. Otherwise, returns an entropy count of 0.
 */
size_t rand_acquire_entropy_from_tsc(RAND_POOL *pool)
{
    unsigned char c;
    int i;

    if ((OPENSSL_ia32cap_P[0] & (1 << 4)) != 0) {
        for (i = 0; i < TSC_READ_COUNT; i++) {
            c = (unsigned char)(OPENSSL_rdtsc() & 0xFF);
            rand_pool_add(pool, &c, 1, 4);
        }
    }
    return rand_pool_entropy_available(pool);
}
#endif

#ifdef OPENSSL_RAND_SEED_RDCPU
size_t OPENSSL_ia32_rdseed_bytes(unsigned char *buf, size_t len);
size_t OPENSSL_ia32_rdrand_bytes(unsigned char *buf, size_t len);

extern unsigned int OPENSSL_ia32cap_P[];

/*
 * Acquire entropy using Intel-specific cpu instructions
 *
 * Uses the RDSEED instruction if available, otherwise uses
 * RDRAND if available.
 *
 * For the differences between RDSEED and RDRAND, and why RDSEED
 * is the preferred choice, see https://goo.gl/oK3KcN
 *
 * Returns the total entropy count, if it exceeds the requested
 * entropy count. Otherwise, returns an entropy count of 0.
 */
size_t rand_acquire_entropy_from_cpu(RAND_POOL *pool)
{
    size_t bytes_needed;
    unsigned char *buffer;

    bytes_needed = rand_pool_bytes_needed(pool, 1 /*entropy_factor*/);
    if (bytes_needed > 0) {
        buffer = rand_pool_add_begin(pool, bytes_needed);

        if (buffer != NULL) {
            /* Whichever comes first, use RDSEED, RDRAND or nothing */
            if ((OPENSSL_ia32cap_P[2] & (1 << 18)) != 0) {
                if (OPENSSL_ia32_rdseed_bytes(buffer, bytes_needed)
                    == bytes_needed) {
                    rand_pool_add_end(pool, bytes_needed, 8 * bytes_needed);
                }
            } else if ((OPENSSL_ia32cap_P[1] & (1 << (62 - 32))) != 0) {
                if (OPENSSL_ia32_rdrand_bytes(buffer, bytes_needed)
                    == bytes_needed) {
                    rand_pool_add_end(pool, bytes_needed, 8 * bytes_needed);
                }
            } else {
                rand_pool_add_end(pool, 0, 0);
            }
        }
    }

    return rand_pool_entropy_available(pool);
}
#endif


/*
 * Implements the get_entropy() callback (see RAND_DRBG_set_callbacks())
 *
 * If the DRBG has a parent, then the required amount of entropy input
 * is fetched using the parent's RAND_DRBG_generate().
 *
 * Otherwise, the entropy is polled from the system entropy sources
 * using rand_pool_acquire_entropy().
 *
 * If a random pool has been added to the DRBG using RAND_add(), then
 * its entropy will be used up first.
 */
size_t rand_drbg_get_entropy(RAND_DRBG *drbg,
                             unsigned char **pout,
                             int entropy, size_t min_len, size_t max_len,
                             int prediction_resistance)
{
    size_t ret = 0;
    size_t entropy_available = 0;
    RAND_POOL *pool;

    if (drbg->parent && drbg->strength > drbg->parent->strength) {
        /*
         * We currently don't support the algorithm from NIST SP 800-90C
         * 10.1.2 to use a weaker DRBG as source
         */
        RANDerr(RAND_F_RAND_DRBG_GET_ENTROPY, RAND_R_PARENT_STRENGTH_TOO_WEAK);
        return 0;
    }

    if (drbg->seed_pool != NULL) {
        pool = drbg->seed_pool;
        pool->entropy_requested = entropy;
    } else {
        pool = rand_pool_new(entropy, min_len, max_len);
        if (pool == NULL)
            return 0;
    }

    if (drbg->parent) {
        size_t bytes_needed = rand_pool_bytes_needed(pool, 1 /*entropy_factor*/);
        unsigned char *buffer = rand_pool_add_begin(pool, bytes_needed);

        if (buffer != NULL) {
            size_t bytes = 0;

            /*
             * Get random from parent, include our state as additional input.
             * Our lock is already held, but we need to lock our parent before
             * generating bits from it. (Note: taking the lock will be a no-op
             * if locking if drbg->parent->lock == NULL.)
             */
            rand_drbg_lock(drbg->parent);
            if (RAND_DRBG_generate(drbg->parent,
                                   buffer, bytes_needed,
                                   prediction_resistance,
                                   NULL, 0) != 0)
                bytes = bytes_needed;
            drbg->reseed_next_counter
                = tsan_load(&drbg->parent->reseed_prop_counter);
            rand_drbg_unlock(drbg->parent);

            rand_pool_add_end(pool, bytes, 8 * bytes);
            entropy_available = rand_pool_entropy_available(pool);
        }

    } else {
        if (prediction_resistance) {
            /*
             * We don't have any entropy sources that comply with the NIST
             * standard to provide prediction resistance (see NIST SP 800-90C,
             * Section 5.4).
             */
            RANDerr(RAND_F_RAND_DRBG_GET_ENTROPY,
                    RAND_R_PREDICTION_RESISTANCE_NOT_SUPPORTED);
            goto err;
        }

        /* Get entropy by polling system entropy sources. */
        entropy_available = rand_pool_acquire_entropy(pool);
    }

    if (entropy_available > 0) {
        ret   = rand_pool_length(pool);
        *pout = rand_pool_detach(pool);
    }

 err:
    if (drbg->seed_pool == NULL)
        rand_pool_free(pool);
    return ret;
}

/*
 * Implements the cleanup_entropy() callback (see RAND_DRBG_set_callbacks())
 *
 */
void rand_drbg_cleanup_entropy(RAND_DRBG *drbg,
                               unsigned char *out, size_t outlen)
{
    if (drbg->seed_pool == NULL)
        OPENSSL_secure_clear_free(out, outlen);
}


/*
 * Implements the get_nonce() callback (see RAND_DRBG_set_callbacks())
 *
 */
size_t rand_drbg_get_nonce(RAND_DRBG *drbg,
                           unsigned char **pout,
                           int entropy, size_t min_len, size_t max_len)
{
    size_t ret = 0;
    RAND_POOL *pool;

    struct {
        void * instance;
        int count;
    } data = { 0 };

    pool = rand_pool_new(0, min_len, max_len);
    if (pool == NULL)
        return 0;

    if (rand_pool_add_nonce_data(pool) == 0)
        goto err;

    data.instance = drbg;
    CRYPTO_atomic_add(&rand_nonce_count, 1, &data.count, rand_nonce_lock);

    if (rand_pool_add(pool, (unsigned char *)&data, sizeof(data), 0) == 0)
        goto err;

    ret   = rand_pool_length(pool);
    *pout = rand_pool_detach(pool);

 err:
    rand_pool_free(pool);

    return ret;
}

/*
 * Implements the cleanup_nonce() callback (see RAND_DRBG_set_callbacks())
 *
 */
void rand_drbg_cleanup_nonce(RAND_DRBG *drbg,
                             unsigned char *out, size_t outlen)
{
    OPENSSL_secure_clear_free(out, outlen);
}

/*
 * Generate additional data that can be used for the drbg. The data does
 * not need to contain entropy, but it's useful if it contains at least
 * some bits that are unpredictable.
 *
 * Returns 0 on failure.
 *
 * On success it allocates a buffer at |*pout| and returns the length of
 * the data. The buffer should get freed using OPENSSL_secure_clear_free().
 */
size_t rand_drbg_get_additional_data(RAND_POOL *pool, unsigned char **pout)
{
    size_t ret = 0;

    if (rand_pool_add_additional_data(pool) == 0)
        goto err;

    ret = rand_pool_length(pool);
    *pout = rand_pool_detach(pool);

 err:
    return ret;
}

void rand_drbg_cleanup_additional_data(RAND_POOL *pool, unsigned char *out)
{
    rand_pool_reattach(pool, out);
}

void rand_fork(void)
{
    rand_fork_count++;
}

DEFINE_RUN_ONCE_STATIC(do_rand_init)
{
#ifndef OPENSSL_NO_ENGINE
    rand_engine_lock = CRYPTO_THREAD_lock_new();
    if (rand_engine_lock == NULL)
        return 0;
#endif

    rand_meth_lock = CRYPTO_THREAD_lock_new();
    if (rand_meth_lock == NULL)
        goto err1;

    rand_nonce_lock = CRYPTO_THREAD_lock_new();
    if (rand_nonce_lock == NULL)
        goto err2;

    if (!rand_pool_init())
        goto err3;

    rand_inited = 1;
    return 1;

err3:
    CRYPTO_THREAD_lock_free(rand_nonce_lock);
    rand_nonce_lock = NULL;
err2:
    CRYPTO_THREAD_lock_free(rand_meth_lock);
    rand_meth_lock = NULL;
err1:
#ifndef OPENSSL_NO_ENGINE
    CRYPTO_THREAD_lock_free(rand_engine_lock);
    rand_engine_lock = NULL;
#endif
    return 0;
}

void rand_cleanup_int(void)
{
    const RAND_METHOD *meth = default_RAND_meth;

    if (!rand_inited)
        return;

    if (meth != NULL && meth->cleanup != NULL)
        meth->cleanup();
    RAND_set_rand_method(NULL);
    rand_pool_cleanup();
#ifndef OPENSSL_NO_ENGINE
    CRYPTO_THREAD_lock_free(rand_engine_lock);
    rand_engine_lock = NULL;
#endif
    CRYPTO_THREAD_lock_free(rand_meth_lock);
    rand_meth_lock = NULL;
    CRYPTO_THREAD_lock_free(rand_nonce_lock);
    rand_nonce_lock = NULL;
    rand_inited = 0;
}

/*
 * RAND_close_seed_files() ensures that any seed file decriptors are
 * closed after use.
 */
void RAND_keep_random_devices_open(int keep)
{
    if (RUN_ONCE(&rand_init, do_rand_init))
        rand_pool_keep_random_devices_open(keep);
}

/*
 * RAND_poll() reseeds the default RNG using random input
 *
 * The random input is obtained from polling various entropy
 * sources which depend on the operating system and are
 * configurable via the --with-rand-seed configure option.
 */
int RAND_poll(void)
{
    int ret = 0;

    RAND_POOL *pool = NULL;

    const RAND_METHOD *meth = RAND_get_rand_method();

    if (meth == RAND_OpenSSL()) {
        /* fill random pool and seed the master DRBG */
        RAND_DRBG *drbg = RAND_DRBG_get0_master();

        if (drbg == NULL)
            return 0;

        rand_drbg_lock(drbg);
        ret = rand_drbg_restart(drbg, NULL, 0, 0);
        rand_drbg_unlock(drbg);

        return ret;

    } else {
        /* fill random pool and seed the current legacy RNG */
        pool = rand_pool_new(RAND_DRBG_STRENGTH,
                             RAND_DRBG_STRENGTH / 8,
                             RAND_POOL_MAX_LENGTH);
        if (pool == NULL)
            return 0;

        if (rand_pool_acquire_entropy(pool) == 0)
            goto err;

        if (meth->add == NULL
            || meth->add(rand_pool_buffer(pool),
                         rand_pool_length(pool),
                         (rand_pool_entropy(pool) / 8.0)) == 0)
            goto err;

        ret = 1;
    }

err:
    rand_pool_free(pool);
    return ret;
}

/*
 * Allocate memory and initialize a new random pool
 */

RAND_POOL *rand_pool_new(int entropy_requested, size_t min_len, size_t max_len)
{
    RAND_POOL *pool = OPENSSL_zalloc(sizeof(*pool));

    if (pool == NULL) {
        RANDerr(RAND_F_RAND_POOL_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    pool->min_len = min_len;
    pool->max_len = (max_len > RAND_POOL_MAX_LENGTH) ?
        RAND_POOL_MAX_LENGTH : max_len;

    pool->buffer = OPENSSL_secure_zalloc(pool->max_len);
    if (pool->buffer == NULL) {
        RANDerr(RAND_F_RAND_POOL_NEW, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    pool->entropy_requested = entropy_requested;

    return pool;

err:
    OPENSSL_free(pool);
    return NULL;
}

/*
 * Attach new random pool to the given buffer
 *
 * This function is intended to be used only for feeding random data
 * provided by RAND_add() and RAND_seed() into the <master> DRBG.
 */
RAND_POOL *rand_pool_attach(const unsigned char *buffer, size_t len,
                            size_t entropy)
{
    RAND_POOL *pool = OPENSSL_zalloc(sizeof(*pool));

    if (pool == NULL) {
        RANDerr(RAND_F_RAND_POOL_ATTACH, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    /*
     * The const needs to be cast away, but attached buffers will not be
     * modified (in contrary to allocated buffers which are zeroed and
     * freed in the end).
     */
    pool->buffer = (unsigned char *) buffer;
    pool->len = len;

    pool->attached = 1;

    pool->min_len = pool->max_len = pool->len;
    pool->entropy = entropy;

    return pool;
}

/*
 * Free |pool|, securely erasing its buffer.
 */
void rand_pool_free(RAND_POOL *pool)
{
    if (pool == NULL)
        return;

    /*
     * Although it would be advisable from a cryptographical viewpoint,
     * we are not allowed to clear attached buffers, since they are passed
     * to rand_pool_attach() as `const unsigned char*`.
     * (see corresponding comment in rand_pool_attach()).
     */
    if (!pool->attached)
        OPENSSL_secure_clear_free(pool->buffer, pool->max_len);
    OPENSSL_free(pool);
}

/*
 * Return the |pool|'s buffer to the caller (readonly).
 */
const unsigned char *rand_pool_buffer(RAND_POOL *pool)
{
    return pool->buffer;
}

/*
 * Return the |pool|'s entropy to the caller.
 */
size_t rand_pool_entropy(RAND_POOL *pool)
{
    return pool->entropy;
}

/*
 * Return the |pool|'s buffer length to the caller.
 */
size_t rand_pool_length(RAND_POOL *pool)
{
    return pool->len;
}

/*
 * Detach the |pool| buffer and return it to the caller.
 * It's the responsibility of the caller to free the buffer
 * using OPENSSL_secure_clear_free() or to re-attach it
 * again to the pool using rand_pool_reattach().
 */
unsigned char *rand_pool_detach(RAND_POOL *pool)
{
    unsigned char *ret = pool->buffer;
    pool->buffer = NULL;
    pool->entropy = 0;
    return ret;
}

/*
 * Re-attach the |pool| buffer. It is only allowed to pass
 * the |buffer| which was previously detached from the same pool.
 */
void rand_pool_reattach(RAND_POOL *pool, unsigned char *buffer)
{
    pool->buffer = buffer;
    OPENSSL_cleanse(pool->buffer, pool->len);
    pool->len = 0;
}

/*
 * If |entropy_factor| bits contain 1 bit of entropy, how many bytes does one
 * need to obtain at least |bits| bits of entropy?
 */
#define ENTROPY_TO_BYTES(bits, entropy_factor) \
    (((bits) * (entropy_factor) + 7) / 8)


/*
 * Checks whether the |pool|'s entropy is available to the caller.
 * This is the case when entropy count and buffer length are high enough.
 * Returns
 *
 *  |entropy|  if the entropy count and buffer size is large enough
 *      0      otherwise
 */
size_t rand_pool_entropy_available(RAND_POOL *pool)
{
    if (pool->entropy < pool->entropy_requested)
        return 0;

    if (pool->len < pool->min_len)
        return 0;

    return pool->entropy;
}

/*
 * Returns the (remaining) amount of entropy needed to fill
 * the random pool.
 */

size_t rand_pool_entropy_needed(RAND_POOL *pool)
{
    if (pool->entropy < pool->entropy_requested)
        return pool->entropy_requested - pool->entropy;

    return 0;
}

/*
 * Returns the number of bytes needed to fill the pool, assuming
 * the input has 1 / |entropy_factor| entropy bits per data bit.
 * In case of an error, 0 is returned.
 */

size_t rand_pool_bytes_needed(RAND_POOL *pool, unsigned int entropy_factor)
{
    size_t bytes_needed;
    size_t entropy_needed = rand_pool_entropy_needed(pool);

    if (entropy_factor < 1) {
        RANDerr(RAND_F_RAND_POOL_BYTES_NEEDED, RAND_R_ARGUMENT_OUT_OF_RANGE);
        return 0;
    }

    bytes_needed = ENTROPY_TO_BYTES(entropy_needed, entropy_factor);

    if (bytes_needed > pool->max_len - pool->len) {
        /* not enough space left */
        RANDerr(RAND_F_RAND_POOL_BYTES_NEEDED, RAND_R_RANDOM_POOL_OVERFLOW);
        return 0;
    }

    if (pool->len < pool->min_len &&
        bytes_needed < pool->min_len - pool->len)
        /* to meet the min_len requirement */
        bytes_needed = pool->min_len - pool->len;

    return bytes_needed;
}

/* Returns the remaining number of bytes available */
size_t rand_pool_bytes_remaining(RAND_POOL *pool)
{
    return pool->max_len - pool->len;
}

/*
 * Add random bytes to the random pool.
 *
 * It is expected that the |buffer| contains |len| bytes of
 * random input which contains at least |entropy| bits of
 * randomness.
 *
 * Returns 1 if the added amount is adequate, otherwise 0
 */
int rand_pool_add(RAND_POOL *pool,
                  const unsigned char *buffer, size_t len, size_t entropy)
{
    if (len > pool->max_len - pool->len) {
        RANDerr(RAND_F_RAND_POOL_ADD, RAND_R_ENTROPY_INPUT_TOO_LONG);
        return 0;
    }

    if (pool->buffer == NULL) {
        RANDerr(RAND_F_RAND_POOL_ADD, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    if (len > 0) {
        memcpy(pool->buffer + pool->len, buffer, len);
        pool->len += len;
        pool->entropy += entropy;
    }

    return 1;
}

/*
 * Start to add random bytes to the random pool in-place.
 *
 * Reserves the next |len| bytes for adding random bytes in-place
 * and returns a pointer to the buffer.
 * The caller is allowed to copy up to |len| bytes into the buffer.
 * If |len| == 0 this is considered a no-op and a NULL pointer
 * is returned without producing an error message.
 *
 * After updating the buffer, rand_pool_add_end() needs to be called
 * to finish the udpate operation (see next comment).
 */
unsigned char *rand_pool_add_begin(RAND_POOL *pool, size_t len)
{
    if (len == 0)
        return NULL;

    if (len > pool->max_len - pool->len) {
        RANDerr(RAND_F_RAND_POOL_ADD_BEGIN, RAND_R_RANDOM_POOL_OVERFLOW);
        return NULL;
    }

    if (pool->buffer == NULL) {
        RANDerr(RAND_F_RAND_POOL_ADD_BEGIN, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    return pool->buffer + pool->len;
}

/*
 * Finish to add random bytes to the random pool in-place.
 *
 * Finishes an in-place update of the random pool started by
 * rand_pool_add_begin() (see previous comment).
 * It is expected that |len| bytes of random input have been added
 * to the buffer which contain at least |entropy| bits of randomness.
 * It is allowed to add less bytes than originally reserved.
 */
int rand_pool_add_end(RAND_POOL *pool, size_t len, size_t entropy)
{
    if (len > pool->max_len - pool->len) {
        RANDerr(RAND_F_RAND_POOL_ADD_END, RAND_R_RANDOM_POOL_OVERFLOW);
        return 0;
    }

    if (len > 0) {
        pool->len += len;
        pool->entropy += entropy;
    }

    return 1;
}

int RAND_set_rand_method(const RAND_METHOD *meth)
{
    if (!RUN_ONCE(&rand_init, do_rand_init))
        return 0;

    CRYPTO_THREAD_write_lock(rand_meth_lock);
#ifndef OPENSSL_NO_ENGINE
    ENGINE_finish(funct_ref);
    funct_ref = NULL;
#endif
    default_RAND_meth = meth;
    CRYPTO_THREAD_unlock(rand_meth_lock);
    return 1;
}

const RAND_METHOD *RAND_get_rand_method(void)
{
    const RAND_METHOD *tmp_meth = NULL;

    if (!RUN_ONCE(&rand_init, do_rand_init))
        return NULL;

    CRYPTO_THREAD_write_lock(rand_meth_lock);
    if (default_RAND_meth == NULL) {
#ifndef OPENSSL_NO_ENGINE
        ENGINE *e;

        /* If we have an engine that can do RAND, use it. */
        if ((e = ENGINE_get_default_RAND()) != NULL
                && (tmp_meth = ENGINE_get_RAND(e)) != NULL) {
            funct_ref = e;
            default_RAND_meth = tmp_meth;
        } else {
            ENGINE_finish(e);
            default_RAND_meth = &rand_meth;
        }
#else
        default_RAND_meth = &rand_meth;
#endif
    }
    tmp_meth = default_RAND_meth;
    CRYPTO_THREAD_unlock(rand_meth_lock);
    return tmp_meth;
}

#ifndef OPENSSL_NO_ENGINE
int RAND_set_rand_engine(ENGINE *engine)
{
    const RAND_METHOD *tmp_meth = NULL;

    if (!RUN_ONCE(&rand_init, do_rand_init))
        return 0;

    if (engine != NULL) {
        if (!ENGINE_init(engine))
            return 0;
        tmp_meth = ENGINE_get_RAND(engine);
        if (tmp_meth == NULL) {
            ENGINE_finish(engine);
            return 0;
        }
    }
    CRYPTO_THREAD_write_lock(rand_engine_lock);
    /* This function releases any prior ENGINE so call it first */
    RAND_set_rand_method(tmp_meth);
    funct_ref = engine;
    CRYPTO_THREAD_unlock(rand_engine_lock);
    return 1;
}
#endif

void RAND_seed(const void *buf, int num)
{
    const RAND_METHOD *meth = RAND_get_rand_method();

    if (meth->seed != NULL)
        meth->seed(buf, num);
}

void RAND_add(const void *buf, int num, double randomness)
{
    const RAND_METHOD *meth = RAND_get_rand_method();

    if (meth->add != NULL)
        meth->add(buf, num, randomness);
}

/*
 * This function is not part of RAND_METHOD, so if we're not using
 * the default method, then just call RAND_bytes().  Otherwise make
 * sure we're instantiated and use the private DRBG.
 */
int RAND_priv_bytes(unsigned char *buf, int num)
{
    const RAND_METHOD *meth = RAND_get_rand_method();
    RAND_DRBG *drbg;
    int ret;

    if (meth != RAND_OpenSSL())
        return RAND_bytes(buf, num);

    drbg = RAND_DRBG_get0_private();
    if (drbg == NULL)
        return 0;

    ret = RAND_DRBG_bytes(drbg, buf, num);
    return ret;
}

int RAND_bytes(unsigned char *buf, int num)
{
    const RAND_METHOD *meth = RAND_get_rand_method();

    if (meth->bytes != NULL)
        return meth->bytes(buf, num);
    RANDerr(RAND_F_RAND_BYTES, RAND_R_FUNC_NOT_IMPLEMENTED);
    return -1;
}

#if OPENSSL_API_COMPAT < 0x10100000L
int RAND_pseudo_bytes(unsigned char *buf, int num)
{
    const RAND_METHOD *meth = RAND_get_rand_method();

    if (meth->pseudorand != NULL)
        return meth->pseudorand(buf, num);
    return -1;
}
#endif

int RAND_status(void)
{
    const RAND_METHOD *meth = RAND_get_rand_method();

    if (meth->status != NULL)
        return meth->status();
    return 0;
}
