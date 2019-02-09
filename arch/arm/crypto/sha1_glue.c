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
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <crypto/sha.h>
#include <crypto/sha1_base.h>
#include <asm/byteorder.h>

#include "sha1.h"

asmlinkage void sha1_block_data_order(u32 *digest,
		const unsigned char *data, unsigned int rounds);

int sha1_update_arm(struct shash_desc *desc, const u8 *data,
		    unsigned int len)
{
	/* make sure casting to sha1_block_fn() is safe */
	BUILD_BUG_ON(offsetof(struct sha1_state, state) != 0);

	return sha1_base_do_update(desc, data, len,
				   (sha1_block_fn *)sha1_block_data_order);
}
EXPORT_SYMBOL_GPL(sha1_update_arm);

static int sha1_final(struct shash_desc *desc, u8 *out)
{
	sha1_base_do_finalize(desc, (sha1_block_fn *)sha1_block_data_order);
	return sha1_base_finish(desc, out);
}

int sha1_finup_arm(struct shash_desc *desc, const u8 *data,
		   unsigned int len, u8 *out)
{
	sha1_base_do_update(desc, data, len,
			    (sha1_block_fn *)sha1_block_data_order);
	return sha1_final(desc, out);
}
EXPORT_SYMBOL_GPL(sha1_finup_arm);

static struct shash_alg alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	sha1_update_arm,
	.final		=	sha1_final,
	.finup		=	sha1_finup_arm,
	.descsize	=	sizeof(struct sha1_state),
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name=	"sha1-asm",
		.cra_priority	=	150,
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
