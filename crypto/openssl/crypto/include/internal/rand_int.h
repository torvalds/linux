/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * Licensed under the OpenSSL licenses, (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * https://www.openssl.org/source/license.html
 * or in the file LICENSE in the source distribution.
 */

#ifndef HEADER_RAND_INT_H
# define HEADER_RAND_INT_H

# include <openssl/rand.h>

/* forward declaration */
typedef struct rand_pool_st RAND_POOL;

void rand_cleanup_int(void);
void rand_drbg_cleanup_int(void);
void drbg_delete_thread_state(void);
void rand_fork(void);

/* Hardware-based seeding functions. */
size_t rand_acquire_entropy_from_tsc(RAND_POOL *pool);
size_t rand_acquire_entropy_from_cpu(RAND_POOL *pool);

/* DRBG entropy callbacks. */
size_t rand_drbg_get_entropy(RAND_DRBG *drbg,
                             unsigned char **pout,
                             int entropy, size_t min_len, size_t max_len,
                             int prediction_resistance);
void rand_drbg_cleanup_entropy(RAND_DRBG *drbg,
                               unsigned char *out, size_t outlen);
size_t rand_drbg_get_nonce(RAND_DRBG *drbg,
                           unsigned char **pout,
                           int entropy, size_t min_len, size_t max_len);
void rand_drbg_cleanup_nonce(RAND_DRBG *drbg,
                             unsigned char *out, size_t outlen);

size_t rand_drbg_get_additional_data(RAND_POOL *pool, unsigned char **pout);

void rand_drbg_cleanup_additional_data(RAND_POOL *pool, unsigned char *out);

/*
 * RAND_POOL functions
 */
RAND_POOL *rand_pool_new(int entropy_requested, size_t min_len, size_t max_len);
RAND_POOL *rand_pool_attach(const unsigned char *buffer, size_t len,
                            size_t entropy);
void rand_pool_free(RAND_POOL *pool);

const unsigned char *rand_pool_buffer(RAND_POOL *pool);
unsigned char *rand_pool_detach(RAND_POOL *pool);
void rand_pool_reattach(RAND_POOL *pool, unsigned char *buffer);

size_t rand_pool_entropy(RAND_POOL *pool);
size_t rand_pool_length(RAND_POOL *pool);

size_t rand_pool_entropy_available(RAND_POOL *pool);
size_t rand_pool_entropy_needed(RAND_POOL *pool);
/* |entropy_factor| expresses how many bits of data contain 1 bit of entropy */
size_t rand_pool_bytes_needed(RAND_POOL *pool, unsigned int entropy_factor);
size_t rand_pool_bytes_remaining(RAND_POOL *pool);

int rand_pool_add(RAND_POOL *pool,
                  const unsigned char *buffer, size_t len, size_t entropy);
unsigned char *rand_pool_add_begin(RAND_POOL *pool, size_t len);
int rand_pool_add_end(RAND_POOL *pool, size_t len, size_t entropy);


/*
 * Add random bytes to the pool to acquire requested amount of entropy
 *
 * This function is platform specific and tries to acquire the requested
 * amount of entropy by polling platform specific entropy sources.
 *
 * If the function succeeds in acquiring at least |entropy_requested| bits
 * of entropy, the total entropy count is returned. If it fails, it returns
 * an entropy count of 0.
 */
size_t rand_pool_acquire_entropy(RAND_POOL *pool);

/*
 * Add some application specific nonce data
 *
 * This function is platform specific and adds some application specific
 * data to the nonce used for instantiating the drbg.
 *
 * This data currently consists of the process and thread id, and a high
 * resolution timestamp. The data does not include an atomic counter,
 * because that is added by the calling function rand_drbg_get_nonce().
 *
 * Returns 1 on success and 0 on failure.
 */
int rand_pool_add_nonce_data(RAND_POOL *pool);


/*
 * Add some platform specific additional data
 *
 * This function is platform specific and adds some random noise to the
 * additional data used for generating random bytes and for reseeding
 * the drbg.
 *
 * Returns 1 on success and 0 on failure.
 */
int rand_pool_add_additional_data(RAND_POOL *pool);

/*
 * Initialise the random pool reseeding sources.
 *
 * Returns 1 on success and 0 on failure.
 */
int rand_pool_init(void);

/*
 * Finalise the random pool reseeding sources.
 */
void rand_pool_cleanup(void);

/*
 * Control the random pool use of open file descriptors.
 */
void rand_pool_keep_random_devices_open(int keep);

#endif
