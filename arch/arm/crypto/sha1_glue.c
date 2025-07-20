// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 * Glue code for the SHA1 Secure Hash Algorithm assembler implementation
 *
 * This file is based on sha1_generic.c and sha1_ssse3_glue.c
 *
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 * Copyright (c) Mathias Krause <minipli@googlemail.com>
 */

#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <crypto/sha1_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

asmlinkage void sha1_block_data_order(struct sha1_state *digest,
		const u8 *data, int rounds);

static int sha1_update_arm(struct shash_desc *desc, const u8 *data,
			   unsigned int len)
{
	/* make sure signature matches sha1_block_fn() */
	BUILD_BUG_ON(offsetof(struct sha1_state, state) != 0);

	return sha1_base_do_update_blocks(desc, data, len,
					  sha1_block_data_order);
}

static int sha1_finup_arm(struct shash_desc *desc, const u8 *data,
			  unsigned int len, u8 *out)
{
	sha1_base_do_finup(desc, data, len, sha1_block_data_order);
	return sha1_base_finish(desc, out);
}

static struct shash_alg alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	sha1_update_arm,
	.finup		=	sha1_finup_arm,
	.descsize	=	SHA1_STATE_SIZE,
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name=	"sha1-asm",
		.cra_priority	=	150,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};


static int __init sha1_mod_init(void)
{
	return crypto_register_shash(&alg);
}


static void __exit sha1_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}


module_init(sha1_mod_init);
module_exit(sha1_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm (ARM)");
MODULE_ALIAS_CRYPTO("sha1");
MODULE_AUTHOR("David McCullough <ucdevel@gmail.com>");
