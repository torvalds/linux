/*
 * Accelerated poly_hash implementation with ARMv8 PMULL instructions.
 *
 * Based on ghash-ce-glue.c.
 *
 * poly_hash is part of the HEH (Hash-Encrypt-Hash) encryption mode, proposed in
 * Internet Draft https://tools.ietf.org/html/draft-cope-heh-01.
 *
 * poly_hash is very similar to GHASH: both algorithms are keyed hashes which
 * interpret their input data as coefficients of a polynomial over GF(2^128),
 * then calculate a hash value by evaluating that polynomial at the point given
 * by the key, e.g. using Horner's rule.  The difference is that poly_hash uses
 * the more natural "ble" convention to represent GF(2^128) elements, whereas
 * GHASH uses the less natural "lle" convention (see include/crypto/gf128mul.h).
 * The ble convention makes it simpler to implement GF(2^128) multiplication.
 *
 * Copyright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 * Copyright (C) 2017 Google Inc. <ebiggers@google.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <asm/neon.h>
#include <crypto/b128ops.h>
#include <crypto/internal/hash.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/module.h>

/*
 * Note: in this algorithm we currently use 'le128' to represent GF(2^128)
 * elements, even though poly_hash-generic uses 'be128'.  Both types are
 * actually "wrong" because the elements are actually in 'ble' format, and there
 * should be a ble type to represent this --- as well as lle, bbe, and lbe types
 * for the other conventions for representing GF(2^128) elements.  But
 * practically it doesn't matter which type we choose here, so we just use le128
 * since it's arguably more accurate, while poly_hash-generic still has to use
 * be128 because the generic GF(2^128) multiplication functions all take be128.
 */

struct poly_hash_desc_ctx {
	le128 digest;
	unsigned int count;
};

asmlinkage void pmull_poly_hash_update(le128 *digest, const le128 *key,
				       const u8 *src, unsigned int blocks,
				       unsigned int partial);

static int poly_hash_setkey(struct crypto_shash *tfm,
			    const u8 *key, unsigned int keylen)
{
	if (keylen != sizeof(le128)) {
		crypto_shash_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(crypto_shash_ctx(tfm), key, sizeof(le128));
	return 0;
}

static int poly_hash_init(struct shash_desc *desc)
{
	struct poly_hash_desc_ctx *ctx = shash_desc_ctx(desc);

	ctx->digest = (le128) { 0 };
	ctx->count = 0;
	return 0;
}

static int poly_hash_update(struct shash_desc *desc, const u8 *src,
			    unsigned int len)
{
	struct poly_hash_desc_ctx *ctx = shash_desc_ctx(desc);
	unsigned int partial = ctx->count % sizeof(le128);
	u8 *dst = (u8 *)&ctx->digest + partial;

	ctx->count += len;

	/* Finishing at least one block? */
	if (partial + len >= sizeof(le128)) {
		const le128 *key = crypto_shash_ctx(desc->tfm);

		if (partial) {
			/* Finish the pending block. */
			unsigned int n = sizeof(le128) - partial;

			len -= n;
			do {
				*dst++ ^= *src++;
			} while (--n);
		}

		/*
		 * Do the real work.  If 'partial' is nonzero, this starts by
		 * multiplying 'digest' by 'key'.  Then for each additional full
		 * block it adds the block to 'digest' and multiplies by 'key'.
		 */
		kernel_neon_begin_partial(8);
		pmull_poly_hash_update(&ctx->digest, key, src,
				       len / sizeof(le128), partial);
		kernel_neon_end();

		src += len - (len % sizeof(le128));
		len %= sizeof(le128);
		dst = (u8 *)&ctx->digest;
	}

	/* Continue adding the next block to 'digest'. */
	while (len--)
		*dst++ ^= *src++;
	return 0;
}

static int poly_hash_final(struct shash_desc *desc, u8 *out)
{
	struct poly_hash_desc_ctx *ctx = shash_desc_ctx(desc);
	unsigned int partial = ctx->count % sizeof(le128);

	/* Finish the last block if needed. */
	if (partial) {
		const le128 *key = crypto_shash_ctx(desc->tfm);

		kernel_neon_begin_partial(8);
		pmull_poly_hash_update(&ctx->digest, key, NULL, 0, partial);
		kernel_neon_end();
	}

	memcpy(out, &ctx->digest, sizeof(le128));
	return 0;
}

static struct shash_alg poly_hash_alg = {
	.digestsize	= sizeof(le128),
	.init		= poly_hash_init,
	.update		= poly_hash_update,
	.final		= poly_hash_final,
	.setkey		= poly_hash_setkey,
	.descsize	= sizeof(struct poly_hash_desc_ctx),
	.base		= {
		.cra_name		= "poly_hash",
		.cra_driver_name	= "poly_hash-ce",
		.cra_priority		= 300,
		.cra_ctxsize		= sizeof(le128),
		.cra_module		= THIS_MODULE,
	},
};

static int __init poly_hash_ce_mod_init(void)
{
	return crypto_register_shash(&poly_hash_alg);
}

static void __exit poly_hash_ce_mod_exit(void)
{
	crypto_unregister_shash(&poly_hash_alg);
}

MODULE_DESCRIPTION("Polynomial evaluation hash using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Eric Biggers <ebiggers@google.com>");
MODULE_LICENSE("GPL v2");

module_cpu_feature_match(PMULL, poly_hash_ce_mod_init);
module_exit(poly_hash_ce_mod_exit);
