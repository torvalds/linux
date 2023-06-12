/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cryptographic API.
 *
 * Copyright (c) 2023 Herbert Xu <herbert@gondor.apana.org.au>
 */
#ifndef _LOCAL_CRYPTO_HASH_H
#define _LOCAL_CRYPTO_HASH_H

#include <crypto/internal/hash.h>
#include <linux/cryptouser.h>

#include "internal.h"

static inline int crypto_hash_report_stat(struct sk_buff *skb,
					  struct crypto_alg *alg,
					  const char *type)
{
	struct hash_alg_common *halg = __crypto_hash_alg_common(alg);
	struct crypto_istat_hash *istat = hash_get_stat(halg);
	struct crypto_stat_hash rhash;

	memset(&rhash, 0, sizeof(rhash));

	strscpy(rhash.type, type, sizeof(rhash.type));

	rhash.stat_hash_cnt = atomic64_read(&istat->hash_cnt);
	rhash.stat_hash_tlen = atomic64_read(&istat->hash_tlen);
	rhash.stat_err_cnt = atomic64_read(&istat->err_cnt);

	return nla_put(skb, CRYPTOCFGA_STAT_HASH, sizeof(rhash), &rhash);
}

int crypto_init_shash_ops_async(struct crypto_tfm *tfm);
struct crypto_ahash *crypto_clone_shash_ops_async(struct crypto_ahash *nhash,
						  struct crypto_ahash *hash);

int hash_prepare_alg(struct hash_alg_common *alg);

#endif	/* _LOCAL_CRYPTO_HASH_H */
