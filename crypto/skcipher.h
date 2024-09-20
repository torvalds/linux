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

int crypto_lskcipher_encrypt_sg(struct skcipher_request *req);
int crypto_lskcipher_decrypt_sg(struct skcipher_request *req);
int crypto_init_lskcipher_ops_sg(struct crypto_tfm *tfm);
int skcipher_prepare_alg_common(struct skcipher_alg_common *alg);

#endif	/* _LOCAL_CRYPTO_SKCIPHER_H */
