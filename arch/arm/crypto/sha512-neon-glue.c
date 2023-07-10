// SPDX-License-Identifier: GPL-2.0-only
/*
 * sha512-neon-glue.c - accelerated SHA-384/512 for ARM NEON
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/sha2.h>
#include <crypto/sha512_base.h>
#include <linux/crypto.h>
#include <linux/module.h>

#include <asm/simd.h>
#include <asm/neon.h>

#include "sha512.h"

MODULE_ALIAS_CRYPTO("sha384-neon");
MODULE_ALIAS_CRYPTO("sha512-neon");

asmlinkage void sha512_block_data_order_neon(struct sha512_state *state,
					     const u8 *src, int blocks);

static int sha512_neon_update(struct shash_desc *desc, const u8 *data,
			      unsigned int len)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);

	if (!crypto_simd_usable() ||
	    (sctx->count[0] % SHA512_BLOCK_SIZE) + len < SHA512_BLOCK_SIZE)
		return sha512_arm_update(desc, data, len);

	kernel_neon_begin();
	sha512_base_do_update(desc, data, len, sha512_block_data_order_neon);
	kernel_neon_end();

	return 0;
}

static int sha512_neon_finup(struct shash_desc *desc, const u8 *data,
			     unsigned int len, u8 *out)
{
	if (!crypto_simd_usable())
		return sha512_arm_finup(desc, data, len, out);

	kernel_neon_begin();
	if (len)
		sha512_base_do_update(desc, data, len,
				      sha512_block_data_order_neon);
	sha512_base_do_finalize(desc, sha512_block_data_order_neon);
	kernel_neon_end();

	return sha512_base_finish(desc, out);
}

static int sha512_neon_final(struct shash_desc *desc, u8 *out)
{
	return sha512_neon_finup(desc, NULL, 0, out);
}

struct shash_alg sha512_neon_algs[] = { {
	.init			= sha384_base_init,
	.update			= sha512_neon_update,
	.final			= sha512_neon_final,
	.finup			= sha512_neon_finup,
	.descsize		= sizeof(struct sha512_state),
	.digestsize		= SHA384_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha384",
		.cra_driver_name	= "sha384-neon",
		.cra_priority		= 300,
		.cra_blocksize		= SHA384_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,

	}
},  {
	.init			= sha512_base_init,
	.update			= sha512_neon_update,
	.final			= sha512_neon_final,
	.finup			= sha512_neon_finup,
	.descsize		= sizeof(struct sha512_state),
	.digestsize		= SHA512_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha512",
		.cra_driver_name	= "sha512-neon",
		.cra_priority		= 300,
		.cra_blocksize		= SHA512_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
} };
