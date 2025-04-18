// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue code for the SHA256 Secure Hash Algorithm assembly implementation
 * using NEON instructions.
 *
 * Copyright © 2015 Google Inc.
 *
 * This file is based on sha512_neon_glue.c:
 *   Copyright © 2014 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 */

#include <asm/neon.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha256_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "sha256_glue.h"

asmlinkage void sha256_block_data_order_neon(
	struct crypto_sha256_state *digest, const u8 *data, int num_blks);

static int crypto_sha256_neon_update(struct shash_desc *desc, const u8 *data,
				     unsigned int len)
{
	int remain;

	kernel_neon_begin();
	remain = sha256_base_do_update_blocks(desc, data, len,
					      sha256_block_data_order_neon);
	kernel_neon_end();
	return remain;
}

static int crypto_sha256_neon_finup(struct shash_desc *desc, const u8 *data,
				    unsigned int len, u8 *out)
{
	kernel_neon_begin();
	sha256_base_do_finup(desc, data, len, sha256_block_data_order_neon);
	kernel_neon_end();
	return sha256_base_finish(desc, out);
}

struct shash_alg sha256_neon_algs[] = { {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_base_init,
	.update		=	crypto_sha256_neon_update,
	.finup		=	crypto_sha256_neon_finup,
	.descsize	=	sizeof(struct crypto_sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name =	"sha256-neon",
		.cra_priority	=	250,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	sha224_base_init,
	.update		=	crypto_sha256_neon_update,
	.finup		=	crypto_sha256_neon_finup,
	.descsize	=	sizeof(struct crypto_sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name =	"sha224-neon",
		.cra_priority	=	250,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };
