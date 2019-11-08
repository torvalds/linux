/*
 * Poly1305 authenticator algorithm, RFC7539
 *
 * Copyright (C) 2015 Martin Willi
 *
 * Based on public domain code by Andrew Moon and Daniel J. Bernstein.
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
#include <asm/unaligned.h>

int crypto_poly1305_init(struct shash_desc *desc)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	poly1305_core_init(&dctx->h);
	dctx->buflen = 0;
	dctx->rset = 0;
	dctx->sset = false;

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_poly1305_init);

static void poly1305_blocks(struct poly1305_desc_ctx *dctx, const u8 *src,
			    unsigned int srclen)
{
	unsigned int datalen;

	if (unlikely(!dctx->sset)) {
		datalen = crypto_poly1305_setdesckey(dctx, src, srclen);
		src += srclen - datalen;
		srclen = datalen;
	}

	poly1305_core_blocks(&dctx->h, dctx->r, src,
			     srclen / POLY1305_BLOCK_SIZE, 1);
}

int crypto_poly1305_update(struct shash_desc *desc,
			   const u8 *src, unsigned int srclen)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);
	unsigned int bytes;

	if (unlikely(dctx->buflen)) {
		bytes = min(srclen, POLY1305_BLOCK_SIZE - dctx->buflen);
		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		srclen -= bytes;
		dctx->buflen += bytes;

		if (dctx->buflen == POLY1305_BLOCK_SIZE) {
			poly1305_blocks(dctx, dctx->buf,
					POLY1305_BLOCK_SIZE);
			dctx->buflen = 0;
		}
	}

	if (likely(srclen >= POLY1305_BLOCK_SIZE)) {
		poly1305_blocks(dctx, src, srclen);
		src += srclen - (srclen % POLY1305_BLOCK_SIZE);
		srclen %= POLY1305_BLOCK_SIZE;
	}

	if (unlikely(srclen)) {
		dctx->buflen = srclen;
		memcpy(dctx->buf, src, srclen);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_poly1305_update);

int crypto_poly1305_final(struct shash_desc *desc, u8 *dst)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	if (unlikely(!dctx->sset))
		return -ENOKEY;

	poly1305_final_generic(dctx, dst);
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_poly1305_final);

static struct shash_alg poly1305_alg = {
	.digestsize	= POLY1305_DIGEST_SIZE,
	.init		= crypto_poly1305_init,
	.update		= crypto_poly1305_update,
	.final		= crypto_poly1305_final,
	.descsize	= sizeof(struct poly1305_desc_ctx),
	.base		= {
		.cra_name		= "poly1305",
		.cra_driver_name	= "poly1305-generic",
		.cra_priority		= 100,
		.cra_blocksize		= POLY1305_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	},
};

static int __init poly1305_mod_init(void)
{
	return crypto_register_shash(&poly1305_alg);
}

static void __exit poly1305_mod_exit(void)
{
	crypto_unregister_shash(&poly1305_alg);
}

module_init(poly1305_mod_init);
module_exit(poly1305_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("Poly1305 authenticator");
MODULE_ALIAS_CRYPTO("poly1305");
MODULE_ALIAS_CRYPTO("poly1305-generic");
