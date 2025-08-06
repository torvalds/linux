// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * powerpc implementation of the SHA1 Secure Hash Algorithm.
 *
 * Derived from cryptoapi implementation, adapted for in-place
 * scatterlist interface.
 *
 * Derived from "crypto/sha1.c"
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 */
#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <crypto/sha1_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

asmlinkage void powerpc_sha_transform(u32 *state, const u8 *src);

static void powerpc_sha_block(struct sha1_state *sctx, const u8 *data,
			      int blocks)
{
	do {
		powerpc_sha_transform(sctx->state, data);
		data += 64;
	} while (--blocks);
}

static int powerpc_sha1_update(struct shash_desc *desc, const u8 *data,
			       unsigned int len)
{
	return sha1_base_do_update_blocks(desc, data, len, powerpc_sha_block);
}

/* Add padding and return the message digest. */
static int powerpc_sha1_finup(struct shash_desc *desc, const u8 *src,
			      unsigned int len, u8 *out)
{
	sha1_base_do_finup(desc, src, len, powerpc_sha_block);
	return sha1_base_finish(desc, out);
}

static struct shash_alg alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	powerpc_sha1_update,
	.finup		=	powerpc_sha1_finup,
	.descsize	=	SHA1_STATE_SIZE,
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name=	"sha1-powerpc",
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int __init sha1_powerpc_mod_init(void)
{
	return crypto_register_shash(&alg);
}

static void __exit sha1_powerpc_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(sha1_powerpc_mod_init);
module_exit(sha1_powerpc_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm");

MODULE_ALIAS_CRYPTO("sha1");
MODULE_ALIAS_CRYPTO("sha1-powerpc");
