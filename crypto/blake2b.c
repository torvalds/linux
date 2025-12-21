// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Crypto API support for BLAKE2b
 *
 * Copyright 2025 Google LLC
 */
#include <crypto/blake2b.h>
#include <crypto/internal/hash.h>
#include <linux/kernel.h>
#include <linux/module.h>

struct blake2b_tfm_ctx {
	unsigned int keylen;
	u8 key[BLAKE2B_KEY_SIZE];
};

static int crypto_blake2b_setkey(struct crypto_shash *tfm,
				 const u8 *key, unsigned int keylen)
{
	struct blake2b_tfm_ctx *tctx = crypto_shash_ctx(tfm);

	if (keylen > BLAKE2B_KEY_SIZE)
		return -EINVAL;
	memcpy(tctx->key, key, keylen);
	tctx->keylen = keylen;
	return 0;
}

#define BLAKE2B_CTX(desc) ((struct blake2b_ctx *)shash_desc_ctx(desc))

static int crypto_blake2b_init(struct shash_desc *desc)
{
	const struct blake2b_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	unsigned int digestsize = crypto_shash_digestsize(desc->tfm);

	blake2b_init_key(BLAKE2B_CTX(desc), digestsize,
			 tctx->key, tctx->keylen);
	return 0;
}

static int crypto_blake2b_update(struct shash_desc *desc,
				 const u8 *data, unsigned int len)
{
	blake2b_update(BLAKE2B_CTX(desc), data, len);
	return 0;
}

static int crypto_blake2b_final(struct shash_desc *desc, u8 *out)
{
	blake2b_final(BLAKE2B_CTX(desc), out);
	return 0;
}

static int crypto_blake2b_digest(struct shash_desc *desc,
				 const u8 *data, unsigned int len, u8 *out)
{
	const struct blake2b_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	unsigned int digestsize = crypto_shash_digestsize(desc->tfm);

	blake2b(tctx->key, tctx->keylen, data, len, out, digestsize);
	return 0;
}

#define BLAKE2B_ALG(name, digest_size)					\
	{								\
		.base.cra_name		= name,				\
		.base.cra_driver_name	= name "-lib",			\
		.base.cra_priority	= 300,				\
		.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,	\
		.base.cra_blocksize	= BLAKE2B_BLOCK_SIZE,		\
		.base.cra_ctxsize	= sizeof(struct blake2b_tfm_ctx), \
		.base.cra_module	= THIS_MODULE,			\
		.digestsize		= digest_size,			\
		.setkey			= crypto_blake2b_setkey,	\
		.init			= crypto_blake2b_init,		\
		.update			= crypto_blake2b_update,	\
		.final			= crypto_blake2b_final,		\
		.digest			= crypto_blake2b_digest,	\
		.descsize		= sizeof(struct blake2b_ctx),	\
	}

static struct shash_alg algs[] = {
	BLAKE2B_ALG("blake2b-160", BLAKE2B_160_HASH_SIZE),
	BLAKE2B_ALG("blake2b-256", BLAKE2B_256_HASH_SIZE),
	BLAKE2B_ALG("blake2b-384", BLAKE2B_384_HASH_SIZE),
	BLAKE2B_ALG("blake2b-512", BLAKE2B_512_HASH_SIZE),
};

static int __init crypto_blake2b_mod_init(void)
{
	return crypto_register_shashes(algs, ARRAY_SIZE(algs));
}
module_init(crypto_blake2b_mod_init);

static void __exit crypto_blake2b_mod_exit(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}
module_exit(crypto_blake2b_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto API support for BLAKE2b");

MODULE_ALIAS_CRYPTO("blake2b-160");
MODULE_ALIAS_CRYPTO("blake2b-160-lib");
MODULE_ALIAS_CRYPTO("blake2b-256");
MODULE_ALIAS_CRYPTO("blake2b-256-lib");
MODULE_ALIAS_CRYPTO("blake2b-384");
MODULE_ALIAS_CRYPTO("blake2b-384-lib");
MODULE_ALIAS_CRYPTO("blake2b-512");
MODULE_ALIAS_CRYPTO("blake2b-512-lib");
