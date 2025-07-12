// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Crypto API support for SHA-1 and HMAC-SHA1
 *
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 * Copyright 2025 Google LLC
 */
#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <linux/kernel.h>
#include <linux/module.h>

const u8 sha1_zero_message_hash[SHA1_DIGEST_SIZE] = {
	0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d,
	0x32, 0x55, 0xbf, 0xef, 0x95, 0x60, 0x18, 0x90,
	0xaf, 0xd8, 0x07, 0x09
};
EXPORT_SYMBOL_GPL(sha1_zero_message_hash);

#define SHA1_CTX(desc) ((struct sha1_ctx *)shash_desc_ctx(desc))

static int crypto_sha1_init(struct shash_desc *desc)
{
	sha1_init(SHA1_CTX(desc));
	return 0;
}

static int crypto_sha1_update(struct shash_desc *desc,
			      const u8 *data, unsigned int len)
{
	sha1_update(SHA1_CTX(desc), data, len);
	return 0;
}

static int crypto_sha1_final(struct shash_desc *desc, u8 *out)
{
	sha1_final(SHA1_CTX(desc), out);
	return 0;
}

static int crypto_sha1_digest(struct shash_desc *desc,
			      const u8 *data, unsigned int len, u8 *out)
{
	sha1(data, len, out);
	return 0;
}

#define HMAC_SHA1_KEY(tfm) ((struct hmac_sha1_key *)crypto_shash_ctx(tfm))
#define HMAC_SHA1_CTX(desc) ((struct hmac_sha1_ctx *)shash_desc_ctx(desc))

static int crypto_hmac_sha1_setkey(struct crypto_shash *tfm,
				   const u8 *raw_key, unsigned int keylen)
{
	hmac_sha1_preparekey(HMAC_SHA1_KEY(tfm), raw_key, keylen);
	return 0;
}

static int crypto_hmac_sha1_init(struct shash_desc *desc)
{
	hmac_sha1_init(HMAC_SHA1_CTX(desc), HMAC_SHA1_KEY(desc->tfm));
	return 0;
}

static int crypto_hmac_sha1_update(struct shash_desc *desc,
				   const u8 *data, unsigned int len)
{
	hmac_sha1_update(HMAC_SHA1_CTX(desc), data, len);
	return 0;
}

static int crypto_hmac_sha1_final(struct shash_desc *desc, u8 *out)
{
	hmac_sha1_final(HMAC_SHA1_CTX(desc), out);
	return 0;
}

static int crypto_hmac_sha1_digest(struct shash_desc *desc,
				   const u8 *data, unsigned int len, u8 *out)
{
	hmac_sha1(HMAC_SHA1_KEY(desc->tfm), data, len, out);
	return 0;
}

static struct shash_alg algs[] = {
	{
		.base.cra_name		= "sha1",
		.base.cra_driver_name	= "sha1-lib",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= SHA1_BLOCK_SIZE,
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA1_DIGEST_SIZE,
		.init			= crypto_sha1_init,
		.update			= crypto_sha1_update,
		.final			= crypto_sha1_final,
		.digest			= crypto_sha1_digest,
		.descsize		= sizeof(struct sha1_ctx),
	},
	{
		.base.cra_name		= "hmac(sha1)",
		.base.cra_driver_name	= "hmac-sha1-lib",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= SHA1_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct hmac_sha1_key),
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA1_DIGEST_SIZE,
		.setkey			= crypto_hmac_sha1_setkey,
		.init			= crypto_hmac_sha1_init,
		.update			= crypto_hmac_sha1_update,
		.final			= crypto_hmac_sha1_final,
		.digest			= crypto_hmac_sha1_digest,
		.descsize		= sizeof(struct hmac_sha1_ctx),
	},
};

static int __init crypto_sha1_mod_init(void)
{
	return crypto_register_shashes(algs, ARRAY_SIZE(algs));
}
module_init(crypto_sha1_mod_init);

static void __exit crypto_sha1_mod_exit(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}
module_exit(crypto_sha1_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto API support for SHA-1 and HMAC-SHA1");

MODULE_ALIAS_CRYPTO("sha1");
MODULE_ALIAS_CRYPTO("sha1-lib");
MODULE_ALIAS_CRYPTO("hmac(sha1)");
MODULE_ALIAS_CRYPTO("hmac-sha1-lib");
