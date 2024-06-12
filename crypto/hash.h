/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cryptographic API.
 *
 * Copyright (c) 2023 Herbert Xu <herbert@gondor.apana.org.au>
 */
#ifndef _LOCAL_CRYPTO_HASH_H
#define _LOCAL_CRYPTO_HASH_H

#include <crypto/internal/hash.h>

#include "internal.h"

extern const struct crypto_type crypto_shash_type;

int hash_prepare_alg(struct hash_alg_common *alg);

#endif	/* _LOCAL_CRYPTO_HASH_H */
