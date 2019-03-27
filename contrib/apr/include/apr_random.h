/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef APR_RANDOM_H
#define APR_RANDOM_H

/**
 * @file apr_random.h
 * @brief APR PRNG routines
 */

#include "apr_pools.h"
#include "apr_thread_proc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup apr_random PRNG Routines
 * @ingroup APR
 * @{
 */

typedef struct apr_crypto_hash_t apr_crypto_hash_t;

typedef void apr_crypto_hash_init_t(apr_crypto_hash_t *hash);
typedef void apr_crypto_hash_add_t(apr_crypto_hash_t *hash, const void *data,
                                   apr_size_t bytes);
typedef void apr_crypto_hash_finish_t(apr_crypto_hash_t *hash,
                                      unsigned char *result);


/* FIXME: make this opaque */
struct apr_crypto_hash_t {
    apr_crypto_hash_init_t *init;
    apr_crypto_hash_add_t *add;
    apr_crypto_hash_finish_t *finish;
    apr_size_t size;
    void *data;
};

/**
 * Allocate and initialize the SHA-256 context
 * @param p The pool to allocate from
 */
APR_DECLARE(apr_crypto_hash_t *) apr_crypto_sha256_new(apr_pool_t *p);

/** Opaque PRNG structure. */
typedef struct apr_random_t apr_random_t;

/**
 * Initialize a PRNG state
 * @param g The PRNG state
 * @param p The pool to allocate from
 * @param pool_hash Pool hash functions
 * @param key_hash Key hash functions
 * @param prng_hash PRNG hash functions
 */
APR_DECLARE(void) apr_random_init(apr_random_t *g, apr_pool_t *p,
                                  apr_crypto_hash_t *pool_hash,
                                  apr_crypto_hash_t *key_hash,
                                  apr_crypto_hash_t *prng_hash);
/**
 * Allocate and initialize (apr_crypto_sha256_new) a new PRNG state.
 * @param p The pool to allocate from
 */
APR_DECLARE(apr_random_t *) apr_random_standard_new(apr_pool_t *p);

/**
 * Mix the randomness pools.
 * @param g The PRNG state
 * @param entropy_ Entropy buffer
 * @param bytes Length of entropy_ in bytes
 */
APR_DECLARE(void) apr_random_add_entropy(apr_random_t *g,
                                         const void *entropy_,
                                         apr_size_t bytes);
/**
 * Generate cryptographically insecure random bytes.
 * @param g The RNG state
 * @param random Buffer to fill with random bytes
 * @param bytes Length of buffer in bytes
 */
APR_DECLARE(apr_status_t) apr_random_insecure_bytes(apr_random_t *g,
                                                    void *random,
                                                    apr_size_t bytes);

/**
 * Generate cryptographically secure random bytes.
 * @param g The RNG state
 * @param random Buffer to fill with random bytes
 * @param bytes Length of buffer in bytes
 */
APR_DECLARE(apr_status_t) apr_random_secure_bytes(apr_random_t *g,
                                                  void *random,
                                                  apr_size_t bytes);
/**
 * Ensures that E bits of conditional entropy are mixed into the PRNG
 * before any further randomness is extracted.
 * @param g The RNG state
 */
APR_DECLARE(void) apr_random_barrier(apr_random_t *g);

/**
 * Return APR_SUCCESS if the cryptographic PRNG has been seeded with
 * enough data, APR_ENOTENOUGHENTROPY otherwise.
 * @param r The RNG state
 */
APR_DECLARE(apr_status_t) apr_random_secure_ready(apr_random_t *r);

/**
 * Return APR_SUCCESS if the PRNG has been seeded with enough data,
 * APR_ENOTENOUGHENTROPY otherwise.
 * @param r The PRNG state
 */
APR_DECLARE(apr_status_t) apr_random_insecure_ready(apr_random_t *r);

/**
 * Mix the randomness pools after forking.
 * @param proc The resulting process handle from apr_proc_fork()
 * @remark Call this in the child after forking to mix the randomness
 * pools. Note that its generally a bad idea to fork a process with a
 * real PRNG in it - better to have the PRNG externally and get the
 * randomness from there. However, if you really must do it, then you
 * should supply all your entropy to all the PRNGs - don't worry, they
 * won't produce the same output.
 * @remark Note that apr_proc_fork() calls this for you, so only weird
 * applications need ever call it themselves.
 * @internal
 */
APR_DECLARE(void) apr_random_after_fork(apr_proc_t *proc);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* !APR_RANDOM_H */
