/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef CIPHERAPI_H
# define CIPHERAPI_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0))
# include <linux/crypto.h>

typedef struct crypto_ablkcipher cryptodev_crypto_blkcipher_t;
typedef struct ablkcipher_request cryptodev_blkcipher_request_t;

# define cryptodev_crypto_alloc_blkcipher crypto_alloc_ablkcipher
# define cryptodev_crypto_blkcipher_blocksize crypto_ablkcipher_blocksize
# define cryptodev_crypto_blkcipher_ivsize crypto_ablkcipher_ivsize
# define cryptodev_crypto_blkcipher_alignmask crypto_ablkcipher_alignmask
# define cryptodev_crypto_blkcipher_setkey crypto_ablkcipher_setkey

static inline void cryptodev_crypto_free_blkcipher(cryptodev_crypto_blkcipher_t *c) {
	if (c)
		crypto_free_ablkcipher(c);
}

# define cryptodev_blkcipher_request_alloc ablkcipher_request_alloc
# define cryptodev_blkcipher_request_set_callback ablkcipher_request_set_callback

static inline void cryptodev_blkcipher_request_free(cryptodev_blkcipher_request_t *r) {
	if (r)
		ablkcipher_request_free(r);
}

# define cryptodev_blkcipher_request_set_crypt ablkcipher_request_set_crypt
# define cryptodev_crypto_blkcipher_encrypt crypto_ablkcipher_encrypt
# define cryptodev_crypto_blkcipher_decrypt crypto_ablkcipher_decrypt
# define cryptodev_crypto_blkcipher_tfm crypto_ablkcipher_tfm
#else
#include <crypto/skcipher.h>

typedef struct crypto_skcipher cryptodev_crypto_blkcipher_t;
typedef struct skcipher_request cryptodev_blkcipher_request_t;

# define cryptodev_crypto_alloc_blkcipher crypto_alloc_skcipher
# define cryptodev_crypto_blkcipher_blocksize crypto_skcipher_blocksize
# define cryptodev_crypto_blkcipher_ivsize crypto_skcipher_ivsize
# define cryptodev_crypto_blkcipher_alignmask crypto_skcipher_alignmask
# define cryptodev_crypto_blkcipher_setkey crypto_skcipher_setkey
# define cryptodev_crypto_free_blkcipher crypto_free_skcipher
# define cryptodev_blkcipher_request_alloc skcipher_request_alloc
# define cryptodev_blkcipher_request_set_callback skcipher_request_set_callback
# define cryptodev_blkcipher_request_free skcipher_request_free
# define cryptodev_blkcipher_request_set_crypt skcipher_request_set_crypt
# define cryptodev_crypto_blkcipher_encrypt crypto_skcipher_encrypt
# define cryptodev_crypto_blkcipher_decrypt crypto_skcipher_decrypt
# define cryptodev_crypto_blkcipher_tfm crypto_skcipher_tfm
#endif

#endif
