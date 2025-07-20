// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * SHA1 Secure Hash Algorithm.
 *
 * Derived from cryptoapi implementation, adapted for in-place
 * scatterlist interface.
 *
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 */
#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <crypto/sha1_base.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

const u8 sha1_zero_message_hash[SHA1_DIGEST_SIZE] = {
	0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d,
	0x32, 0x55, 0xbf, 0xef, 0x95, 0x60, 0x18, 0x90,
	0xaf, 0xd8, 0x07, 0x09
};
EXPORT_SYMBOL_GPL(sha1_zero_message_hash);

static void sha1_generic_block_fn(struct sha1_state *sst, u8 const *src,
				  int blocks)
{
	u32 temp[SHA1_WORKSPACE_WORDS];

	while (blocks--) {
		sha1_transform(sst->state, src, temp);
		src += SHA1_BLOCK_SIZE;
	}
	memzero_explicit(temp, sizeof(temp));
}

static int crypto_sha1_update(struct shash_desc *desc, const u8 *data,
			      unsigned int len)
{
	return sha1_base_do_update_blocks(desc, data, len,
					  sha1_generic_block_fn);
}

static int crypto_sha1_finup(struct shash_desc *desc, const u8 *data,
			     unsigned int len, u8 *out)
{
	sha1_base_do_finup(desc, data, len, sha1_generic_block_fn);
	return sha1_base_finish(desc, out);
}

static struct shash_alg alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	crypto_sha1_update,
	.finup		=	crypto_sha1_finup,
	.descsize	=	SHA1_STATE_SIZE,
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name=	"sha1-generic",
		.cra_priority	=	100,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int __init sha1_generic_mod_init(void)
{
	return crypto_register_shash(&alg);
}

static void __exit sha1_generic_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(sha1_generic_mod_init);
module_exit(sha1_generic_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm");

MODULE_ALIAS_CRYPTO("sha1");
MODULE_ALIAS_CRYPTO("sha1-generic");
