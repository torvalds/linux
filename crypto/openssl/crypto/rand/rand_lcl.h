/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_RAND_LCL_H
# define HEADER_RAND_LCL_H

# include <openssl/aes.h>
# include <openssl/evp.h>
# include <openssl/sha.h>
# include <openssl/hmac.h>
# include <openssl/ec.h>
# include <openssl/rand_drbg.h>
# include "internal/tsan_assist.h"

# include "internal/numbers.h"

/* How many times to read the TSC as a randomness source. */
# define TSC_READ_COUNT                 4

/* Maximum reseed intervals */
# define MAX_RESEED_INTERVAL                     (1 << 24)
# define MAX_RESEED_TIME_INTERVAL                (1 << 20) /* approx. 12 days */

/* Default reseed intervals */
# define MASTER_RESEED_INTERVAL                  (1 << 8)
# define SLAVE_RESEED_INTERVAL                   (1 << 16)
# define MASTER_RESEED_TIME_INTERVAL             (60*60)   /* 1 hour */
# define SLAVE_RESEED_TIME_INTERVAL              (7*60)    /* 7 minutes */



/*
 * Maximum input size for the DRBG (entropy, nonce, personalization string)
 *
 * NIST SP800 90Ar1 allows a maximum of (1 << 35) bits i.e., (1 << 32) bytes.
 *
 * We lower it to 'only' INT32_MAX bytes, which is equivalent to 2 gigabytes.
 */
# define DRBG_MAX_LENGTH                         INT32_MAX



/*
 * Maximum allocation size for RANDOM_POOL buffers
 *
 * The max_len value for the buffer provided to the rand_drbg_get_entropy()
 * callback is currently 2^31 bytes (2 gigabytes), if a derivation function
 * is used. Since this is much too large to be allocated, the rand_pool_new()
 * function chooses more modest values as default pool length, bounded
 * by RAND_POOL_MIN_LENGTH and RAND_POOL_MAX_LENGTH
 *
 * The choice of the RAND_POOL_FACTOR is large enough such that the
 * RAND_POOL can store a random input which has a lousy entropy rate of
 * 8/256 (= 0.03125) bits per byte. This input will be sent through the
 * derivation function which 'compresses' the low quality input into a
 * high quality output.
 *
 * The factor 1.5 below is the pessimistic estimate for the extra amount
 * of entropy required when no get_nonce() callback is defined.
 */
# define RAND_POOL_FACTOR        256
# define RAND_POOL_MAX_LENGTH    (RAND_POOL_FACTOR * \
                                  3 * (RAND_DRBG_STRENGTH / 16))
/*
 *                             = (RAND_POOL_FACTOR * \
 *                                1.5 * (RAND_DRBG_STRENGTH / 8))
 */


/* DRBG status values */
typedef enum drbg_status_e {
    DRBG_UNINITIALISED,
    DRBG_READY,
    DRBG_ERROR
} DRBG_STATUS;


/* instantiate */
typedef int (*RAND_DRBG_instantiate_fn)(RAND_DRBG *ctx,
                                        const unsigned char *ent,
                                        size_t entlen,
                                        const unsigned char *nonce,
                                        size_t noncelen,
                                        const unsigned char *pers,
                                        size_t perslen);
/* reseed */
typedef int (*RAND_DRBG_reseed_fn)(RAND_DRBG *ctx,
                                   const unsigned char *ent,
                                   size_t entlen,
                                   const unsigned char *adin,
                                   size_t adinlen);
/* generate output */
typedef int (*RAND_DRBG_generate_fn)(RAND_DRBG *ctx,
                                     unsigned char *out,
                                     size_t outlen,
                                     const unsigned char *adin,
                                     size_t adinlen);
/* uninstantiate */
typedef int (*RAND_DRBG_uninstantiate_fn)(RAND_DRBG *ctx);


/*
 * The DRBG methods
 */

typedef struct rand_drbg_method_st {
    RAND_DRBG_instantiate_fn instantiate;
    RAND_DRBG_reseed_fn reseed;
    RAND_DRBG_generate_fn generate;
    RAND_DRBG_uninstantiate_fn uninstantiate;
} RAND_DRBG_METHOD;


/*
 * The state of a DRBG AES-CTR.
 */
typedef struct rand_drbg_ctr_st {
    EVP_CIPHER_CTX *ctx;
    EVP_CIPHER_CTX *ctx_df;
    const EVP_CIPHER *cipher;
    size_t keylen;
    unsigned char K[32];
    unsigned char V[16];
    /* Temporary block storage used by ctr_df */
    unsigned char bltmp[16];
    size_t bltmp_pos;
    unsigned char KX[48];
} RAND_DRBG_CTR;


/*
 * The 'random pool' acts as a dumb container for collecting random
 * input from various entropy sources. The pool has no knowledge about
 * whether its randomness is fed into a legacy RAND_METHOD via RAND_add()
 * or into a new style RAND_DRBG. It is the callers duty to 1) initialize the
 * random pool, 2) pass it to the polling callbacks, 3) seed the RNG, and
 * 4) cleanup the random pool again.
 *
 * The random pool contains no locking mechanism because its scope and
 * lifetime is intended to be restricted to a single stack frame.
 */
struct rand_pool_st {
    unsigned char *buffer;  /* points to the beginning of the random pool */
    size_t len; /* current number of random bytes contained in the pool */

    int attached;  /* true pool was attached to existing buffer */

    size_t min_len; /* minimum number of random bytes requested */
    size_t max_len; /* maximum number of random bytes (allocated buffer size) */
    size_t entropy; /* current entropy count in bits */
    size_t entropy_requested; /* requested entropy count in bits */
};

/*
 * The state of all types of DRBGs, even though we only have CTR mode
 * right now.
 */
struct rand_drbg_st {
    CRYPTO_RWLOCK *lock;
    RAND_DRBG *parent;
    int secure; /* 1: allocated on the secure heap, 0: otherwise */
    int type; /* the nid of the underlying algorithm */
    /*
     * Stores the value of the rand_fork_count global as of when we last
     * reseeded.  The DRBG reseeds automatically whenever drbg->fork_count !=
     * rand_fork_count.  Used to provide fork-safety and reseed this DRBG in
     * the child process.
     */
    int fork_count;
    unsigned short flags; /* various external flags */

    /*
     * The random_data is used by RAND_add()/drbg_add() to attach random
     * data to the global drbg, such that the rand_drbg_get_entropy() callback
     * can pull it during instantiation and reseeding. This is necessary to
     * reconcile the different philosophies of the RAND and the RAND_DRBG
     * with respect to how randomness is added to the RNG during reseeding
     * (see PR #4328).
     */
    struct rand_pool_st *seed_pool;

    /*
     * Auxiliary pool for additional data.
     */
    struct rand_pool_st *adin_pool;

    /*
     * The following parameters are setup by the per-type "init" function.
     *
     * Currently the only type is CTR_DRBG, its init function is drbg_ctr_init().
     *
     * The parameters are closely related to the ones described in
     * section '10.2.1 CTR_DRBG' of [NIST SP 800-90Ar1], with one
     * crucial difference: In the NIST standard, all counts are given
     * in bits, whereas in OpenSSL entropy counts are given in bits
     * and buffer lengths are given in bytes.
     *
     * Since this difference has lead to some confusion in the past,
     * (see [GitHub Issue #2443], formerly [rt.openssl.org #4055])
     * the 'len' suffix has been added to all buffer sizes for
     * clarification.
     */

    int strength;
    size_t max_request;
    size_t min_entropylen, max_entropylen;
    size_t min_noncelen, max_noncelen;
    size_t max_perslen, max_adinlen;

    /* Counts the number of generate requests since the last reseed. */
    unsigned int reseed_gen_counter;
    /*
     * Maximum number of generate requests until a reseed is required.
     * This value is ignored if it is zero.
     */
    unsigned int reseed_interval;
    /* Stores the time when the last reseeding occurred */
    time_t reseed_time;
    /*
     * Specifies the maximum time interval (in seconds) between reseeds.
     * This value is ignored if it is zero.
     */
    time_t reseed_time_interval;
    /*
     * Counts the number of reseeds since instantiation.
     * This value is ignored if it is zero.
     *
     * This counter is used only for seed propagation from the <master> DRBG
     * to its two children, the <public> and <private> DRBG. This feature is
     * very special and its sole purpose is to ensure that any randomness which
     * is added by RAND_add() or RAND_seed() will have an immediate effect on
     * the output of RAND_bytes() resp. RAND_priv_bytes().
     */
    TSAN_QUALIFIER unsigned int reseed_prop_counter;
    unsigned int reseed_next_counter;

    size_t seedlen;
    DRBG_STATUS state;

    /* Application data, mainly used in the KATs. */
    CRYPTO_EX_DATA ex_data;

    /* Implementation specific data (currently only one implementation) */
    union {
        RAND_DRBG_CTR ctr;
    } data;

    /* Implementation specific methods */
    RAND_DRBG_METHOD *meth;

    /* Callback functions.  See comments in rand_lib.c */
    RAND_DRBG_get_entropy_fn get_entropy;
    RAND_DRBG_cleanup_entropy_fn cleanup_entropy;
    RAND_DRBG_get_nonce_fn get_nonce;
    RAND_DRBG_cleanup_nonce_fn cleanup_nonce;
};

/* The global RAND method, and the global buffer and DRBG instance. */
extern RAND_METHOD rand_meth;

/*
 * A "generation count" of forks.  Incremented in the child process after a
 * fork.  Since rand_fork_count is increment-only, and only ever written to in
 * the child process of the fork, which is guaranteed to be single-threaded, no
 * locking is needed for normal (read) accesses; the rest of pthread fork
 * processing is assumed to introduce the necessary memory barriers.  Sibling
 * children of a given parent will produce duplicate values, but this is not
 * problematic because the reseeding process pulls input from the system CSPRNG
 * and/or other global sources, so the siblings will end up generating
 * different output streams.
 */
extern int rand_fork_count;

/* DRBG helpers */
int rand_drbg_restart(RAND_DRBG *drbg,
                      const unsigned char *buffer, size_t len, size_t entropy);
size_t rand_drbg_seedlen(RAND_DRBG *drbg);
/* locking api */
int rand_drbg_lock(RAND_DRBG *drbg);
int rand_drbg_unlock(RAND_DRBG *drbg);
int rand_drbg_enable_locking(RAND_DRBG *drbg);


/* initializes the AES-CTR DRBG implementation */
int drbg_ctr_init(RAND_DRBG *drbg);

#endif
