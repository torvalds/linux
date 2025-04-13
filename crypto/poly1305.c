/*
 * Crypto API wrapper for the Poly1305 library functions
 *
 * Copyright (C) 2015 Martin Willi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <crypto/algapi.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/poly1305.h>
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/module.h>

struct crypto_poly1305_desc_ctx {
	struct poly1305_desc_ctx base;
	u8 key[POLY1305_KEY_SIZE];
	unsigned int keysize;
};

static int crypto_poly1305_init(struct shash_desc *desc)
{
	struct crypto_poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	dctx->keysize = 0;
	return 0;
}

static int crypto_poly1305_update(struct shash_desc *desc,
				  const u8 *src, unsigned int srclen, bool arch)
{
	struct crypto_poly1305_desc_ctx *dctx = shash_desc_ctx(desc);
	unsigned int bytes;

	/*
	 * The key is passed as the first 32 "data" bytes.  The actual
	 * poly1305_init() can be called only once the full key is available.
	 */
	if (dctx->keysize < POLY1305_KEY_SIZE) {
		bytes = min(srclen, POLY1305_KEY_SIZE - dctx->keysize);
		memcpy(&dctx->key[dctx->keysize], src, bytes);
		dctx->keysize += bytes;
		if (dctx->keysize < POLY1305_KEY_SIZE)
			return 0;
		if (arch)
			poly1305_init(&dctx->base, dctx->key);
		else
			poly1305_init_generic(&dctx->base, dctx->key);
		src += bytes;
		srclen -= bytes;
	}

	if (arch)
		poly1305_update(&dctx->base, src, srclen);
	else
		poly1305_update_generic(&dctx->base, src, srclen);

	return 0;
}

static int crypto_poly1305_update_generic(struct shash_desc *desc,
					  const u8 *src, unsigned int srclen)
{
	return crypto_poly1305_update(desc, src, srclen, false);
}

static int crypto_poly1305_update_arch(struct shash_desc *desc,
				       const u8 *src, unsigned int srclen)
{
	return crypto_poly1305_update(desc, src, srclen, true);
}

static int crypto_poly1305_final(struct shash_desc *desc, u8 *dst, bool arch)
{
	struct crypto_poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	if (unlikely(dctx->keysize != POLY1305_KEY_SIZE))
		return -ENOKEY;

	if (arch)
		poly1305_final(&dctx->base, dst);
	else
		poly1305_final_generic(&dctx->base, dst);
	memzero_explicit(&dctx->key, sizeof(dctx->key));
	return 0;
}

static int crypto_poly1305_final_generic(struct shash_desc *desc, u8 *dst)
{
	return crypto_poly1305_final(desc, dst, false);
}

static int crypto_poly1305_final_arch(struct shash_desc *desc, u8 *dst)
{
	return crypto_poly1305_final(desc, dst, true);
}

static struct shash_alg poly1305_algs[] = {
	{
		.base.cra_name		= "poly1305",
		.base.cra_driver_name	= "poly1305-generic",
		.base.cra_priority	= 100,
		.base.cra_blocksize	= POLY1305_BLOCK_SIZE,
		.base.cra_module	= THIS_MODULE,
		.digestsize		= POLY1305_DIGEST_SIZE,
		.init			= crypto_poly1305_init,
		.update			= crypto_poly1305_update_generic,
		.final			= crypto_poly1305_final_generic,
		.descsize		= sizeof(struct crypto_poly1305_desc_ctx),
	},
	{
		.base.cra_name		= "poly1305",
		.base.cra_driver_name	= "poly1305-" __stringify(ARCH),
		.base.cra_priority	= 300,
		.base.cra_blocksize	= POLY1305_BLOCK_SIZE,
		.base.cra_module	= THIS_MODULE,
		.digestsize		= POLY1305_DIGEST_SIZE,
		.init			= crypto_poly1305_init,
		.update			= crypto_poly1305_update_arch,
		.final			= crypto_poly1305_final_arch,
		.descsize		= sizeof(struct crypto_poly1305_desc_ctx),
	},
};

static int num_algs;

static int __init poly1305_mod_init(void)
{
	/* register the arch flavours only if they differ from generic */
	num_algs = poly1305_is_arch_optimized() ? 2 : 1;

	return crypto_register_shashes(poly1305_algs, num_algs);
}

static void __exit poly1305_mod_exit(void)
{
	crypto_unregister_shashes(poly1305_algs, num_algs);
}

subsys_initcall(poly1305_mod_init);
module_exit(poly1305_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("Crypto API wrapper for the Poly1305 library functions");
MODULE_ALIAS_CRYPTO("poly1305");
MODULE_ALIAS_CRYPTO("poly1305-generic");
MODULE_ALIAS_CRYPTO("poly1305-" __stringify(ARCH));
