// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue code for the SHA256 Secure Hash Algorithm assembly implementation
 * using optimized ARM assembler and NEON instructions.
 *
 * Copyright © 2015 Google Inc.
 *
 * This file is based on sha256_ssse3_glue.c:
 *   Copyright (C) 2013 Intel Corporation
 *   Author: Tim Chen <tim.c.chen@linux.intel.com>
 */

#include <asm/neon.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha256_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "sha256_glue.h"

asmlinkage void sha256_block_data_order(struct crypto_sha256_state *state,
					const u8 *data, int num_blks);

static int crypto_sha256_arm_update(struct shash_desc *desc, const u8 *data,
				    unsigned int len)
{
	/* make sure casting to sha256_block_fn() is safe */
	BUILD_BUG_ON(offsetof(struct crypto_sha256_state, state) != 0);

	return sha256_base_do_update_blocks(desc, data, len,
					    sha256_block_data_order);
}

static int crypto_sha256_arm_finup(struct shash_desc *desc, const u8 *data,
				   unsigned int len, u8 *out)
{
	sha256_base_do_finup(desc, data, len, sha256_block_data_order);
	return sha256_base_finish(desc, out);
}

static struct shash_alg algs[] = { {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_base_init,
	.update		=	crypto_sha256_arm_update,
	.finup		=	crypto_sha256_arm_finup,
	.descsize	=	sizeof(struct crypto_sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name =	"sha256-asm",
		.cra_priority	=	150,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	sha224_base_init,
	.update		=	crypto_sha256_arm_update,
	.finup		=	crypto_sha256_arm_finup,
	.descsize	=	sizeof(struct crypto_sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name =	"sha224-asm",
		.cra_priority	=	150,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

static int __init sha256_mod_init(void)
{
	int res = crypto_register_shashes(algs, ARRAY_SIZE(algs));

	if (res < 0)
		return res;

	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) && cpu_has_neon()) {
		res = crypto_register_shashes(sha256_neon_algs,
					      ARRAY_SIZE(sha256_neon_algs));

		if (res < 0)
			crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
	}

	return res;
}

static void __exit sha256_mod_fini(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));

	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) && cpu_has_neon())
		crypto_unregister_shashes(sha256_neon_algs,
					  ARRAY_SIZE(sha256_neon_algs));
}

module_init(sha256_mod_init);
module_exit(sha256_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA256 Secure Hash Algorithm (ARM), including NEON");

MODULE_ALIAS_CRYPTO("sha256");
