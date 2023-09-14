/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cryptographic API.
 *
 * Copyright (c) 2023 Herbert Xu <herbert@gondor.apana.org.au>
 */
#ifndef _LOCAL_CRYPTO_SKCIPHER_H
#define _LOCAL_CRYPTO_SKCIPHER_H

#include <crypto/internal/skcipher.h>
#include "internal.h"

static inline struct crypto_istat_cipher *skcipher_get_stat_common(
	struct skcipher_alg_common *alg)
{
#ifdef CONFIG_CRYPTO_STATS
	return &alg->stat;
#else
	return NULL;
#endif
}

int crypto_lskcipher_setkey_sg(struct crypto_skcipher *tfm, const u8 *key,
			       unsigned int keylen);
int crypto_lskcipher_encrypt_sg(struct skcipher_request *req);
int crypto_lskcipher_decrypt_sg(struct skcipher_request *req);
int crypto_init_lskcipher_ops_sg(struct crypto_tfm *tfm);
int skcipher_prepare_alg_common(struct skcipher_alg_common *alg);

#endif	/* _LOCAL_CRYPTO_SKCIPHER_H */
