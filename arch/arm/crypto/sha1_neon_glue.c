// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue code for the SHA1 Secure Hash Algorithm assembler implementation using
 * ARM NEON instructions.
 *
 * Copyright © 2014 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * This file is based on sha1_generic.c and sha1_ssse3_glue.c:
 *  Copyright (c) Alan Smithee.
 *  Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 *  Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 *  Copyright (c) Mathias Krause <minipli@googlemail.com>
 *  Copyright (c) Chandramouli Narayanan <mouli@linux.intel.com>
 */

#include <asm/neon.h>
#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <crypto/sha1_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

asmlinkage void sha1_transform_neon(struct sha1_state *state_h,
				    const u8 *data, int rounds);

static int sha1_neon_update(struct shash_desc *desc, const u8 *data,
			  unsigned int len)
{
	int remain;

	kernel_neon_begin();
	remain = sha1_base_do_update_blocks(desc, data, len,
					    sha1_transform_neon);
	kernel_neon_end();

	return remain;
}

static int sha1_neon_finup(struct shash_desc *desc, const u8 *data,
			   unsigned int len, u8 *out)
{
	kernel_neon_begin();
	sha1_base_do_finup(desc, data, len, sha1_transform_neon);
	kernel_neon_end();

	return sha1_base_finish(desc, out);
}

static struct shash_alg alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	sha1_neon_update,
	.finup		=	sha1_neon_finup,
	.descsize		= SHA1_STATE_SIZE,
	.base		=	{
		.cra_name		= "sha1",
		.cra_driver_name	= "sha1-neon",
		.cra_priority		= 250,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize		= SHA1_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
};

static int __init sha1_neon_mod_init(void)
{
	if (!cpu_has_neon())
		return -ENODEV;

	return crypto_register_shash(&alg);
}

static void __exit sha1_neon_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(sha1_neon_mod_init);
module_exit(sha1_neon_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm, NEON accelerated");
MODULE_ALIAS_CRYPTO("sha1");
