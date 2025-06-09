// SPDX-License-Identifier: GPL-2.0-only
/*
 * sha512-neon-glue.c - accelerated SHA-384/512 for ARM NEON
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha512_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "sha512.h"

MODULE_ALIAS_CRYPTO("sha384-neon");
MODULE_ALIAS_CRYPTO("sha512-neon");

asmlinkage void sha512_block_data_order_neon(struct sha512_state *state,
					     const u8 *src, int blocks);

static int sha512_neon_update(struct shash_desc *desc, const u8 *data,
			      unsigned int len)
{
	int remain;

	kernel_neon_begin();
	remain = sha512_base_do_update_blocks(desc, data, len,
					      sha512_block_data_order_neon);
	kernel_neon_end();
	return remain;
}

static int sha512_neon_finup(struct shash_desc *desc, const u8 *data,
			     unsigned int len, u8 *out)
{
	kernel_neon_begin();
	sha512_base_do_finup(desc, data, len, sha512_block_data_order_neon);
	kernel_neon_end();
	return sha512_base_finish(desc, out);
}

struct shash_alg sha512_neon_algs[] = { {
	.init			= sha384_base_init,
	.update			= sha512_neon_update,
	.finup			= sha512_neon_finup,
	.descsize		= SHA512_STATE_SIZE,
	.digestsize		= SHA384_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha384",
		.cra_driver_name	= "sha384-neon",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
					  CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize		= SHA384_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,

	}
},  {
	.init			= sha512_base_init,
	.update			= sha512_neon_update,
	.finup			= sha512_neon_finup,
	.descsize		= SHA512_STATE_SIZE,
	.digestsize		= SHA512_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha512",
		.cra_driver_name	= "sha512-neon",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
					  CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize		= SHA512_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
} };
