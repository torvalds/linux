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
#include <linux/mm.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <crypto/sha.h>
#include <crypto/sha1_base.h>
#include <asm/byteorder.h>

const u8 sha1_zero_message_hash[SHA1_DIGEST_SIZE] = {
	0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d,
	0x32, 0x55, 0xbf, 0xef, 0x95, 0x60, 0x18, 0x90,
	0xaf, 0xd8, 0x07, 0x09
};
EXPORT_SYMBOL_GPL(sha1_zero_message_hash);

static void sha1_generic_block_fn(struct sha1_state *sst, u8 const *src,
				  int blocks)
{
	u32 temp[SHA_WORKSPACE_WORDS];

	while (blocks--) {
		sha_transform(sst->state, src, temp);
		src += SHA1_BLOCK_SIZE;
	}
	memzero_explicit(temp, sizeof(temp));
}

int crypto_sha1_update(struct shash_desc *desc, const u8 *data,
		       unsigned int len)
{
	return sha1_base_do_update(desc, data, len, sha1_generic_block_fn);
}
EXPORT_SYMBOL(crypto_sha1_update);

static int sha1_final(struct shash_desc *desc, u8 *out)
{
	sha1_base_do_finalize(desc, sha1_generic_block_fn);
	return sha1_base_finish(desc, out);
}

int crypto_sha1_finup(struct shash_desc *desc, const u8 *data,
		      unsigned int len, u8 *out)
{
	sha1_base_do_update(desc, data, len, sha1_generic_block_fn);
	return sha1_final(desc, out);
}
EXPORT_SYMBOL(crypto_sha1_finup);

static struct shash_alg alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	crypto_sha1_update,
	.final		=	sha1_final,
	.finup		=	crypto_sha1_finup,
	.descsize	=	sizeof(struct sha1_state),
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name=	"sha1-generic",
		.cra_priority	=	100,
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
