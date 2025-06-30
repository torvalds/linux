// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Crypto API support for SHA-224, SHA-256, HMAC-SHA224, and HMAC-SHA256
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * SHA224 Support Copyright 2007 Intel Corporation <jonathan.lynch@intel.com>
 * Copyright 2025 Google LLC
 */
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <linux/kernel.h>
#include <linux/module.h>

/* SHA-224 */

const u8 sha224_zero_message_hash[SHA224_DIGEST_SIZE] = {
	0xd1, 0x4a, 0x02, 0x8c, 0x2a, 0x3a, 0x2b, 0xc9, 0x47,
	0x61, 0x02, 0xbb, 0x28, 0x82, 0x34, 0xc4, 0x15, 0xa2,
	0xb0, 0x1f, 0x82, 0x8e, 0xa6, 0x2a, 0xc5, 0xb3, 0xe4,
	0x2f
};
EXPORT_SYMBOL_GPL(sha224_zero_message_hash);

#define SHA224_CTX(desc) ((struct sha224_ctx *)shash_desc_ctx(desc))

static int crypto_sha224_init(struct shash_desc *desc)
{
	sha224_init(SHA224_CTX(desc));
	return 0;
}

static int crypto_sha224_update(struct shash_desc *desc,
				const u8 *data, unsigned int len)
{
	sha224_update(SHA224_CTX(desc), data, len);
	return 0;
}

static int crypto_sha224_final(struct shash_desc *desc, u8 *out)
{
	sha224_final(SHA224_CTX(desc), out);
	return 0;
}

static int crypto_sha224_digest(struct shash_desc *desc,
				const u8 *data, unsigned int len, u8 *out)
{
	sha224(data, len, out);
	return 0;
}

/* SHA-256 */

const u8 sha256_zero_message_hash[SHA256_DIGEST_SIZE] = {
	0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
	0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
	0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
	0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
};
EXPORT_SYMBOL_GPL(sha256_zero_message_hash);

#define SHA256_CTX(desc) ((struct sha256_ctx *)shash_desc_ctx(desc))

static int crypto_sha256_init(struct shash_desc *desc)
{
	sha256_init(SHA256_CTX(desc));
	return 0;
}

static int crypto_sha256_update(struct shash_desc *desc,
				const u8 *data, unsigned int len)
{
	sha256_update(SHA256_CTX(desc), data, len);
	return 0;
}

static int crypto_sha256_final(struct shash_desc *desc, u8 *out)
{
	sha256_final(SHA256_CTX(desc), out);
	return 0;
}

static int crypto_sha256_digest(struct shash_desc *desc,
				const u8 *data, unsigned int len, u8 *out)
{
	sha256(data, len, out);
	return 0;
}

/* HMAC-SHA224 */

#define HMAC_SHA224_KEY(tfm) ((struct hmac_sha224_key *)crypto_shash_ctx(tfm))
#define HMAC_SHA224_CTX(desc) ((struct hmac_sha224_ctx *)shash_desc_ctx(desc))

static int crypto_hmac_sha224_setkey(struct crypto_shash *tfm,
				     const u8 *raw_key, unsigned int keylen)
{
	hmac_sha224_preparekey(HMAC_SHA224_KEY(tfm), raw_key, keylen);
	return 0;
}

static int crypto_hmac_sha224_init(struct shash_desc *desc)
{
	hmac_sha224_init(HMAC_SHA224_CTX(desc), HMAC_SHA224_KEY(desc->tfm));
	return 0;
}

static int crypto_hmac_sha224_update(struct shash_desc *desc,
				     const u8 *data, unsigned int len)
{
	hmac_sha224_update(HMAC_SHA224_CTX(desc), data, len);
	return 0;
}

static int crypto_hmac_sha224_final(struct shash_desc *desc, u8 *out)
{
	hmac_sha224_final(HMAC_SHA224_CTX(desc), out);
	return 0;
}

static int crypto_hmac_sha224_digest(struct shash_desc *desc,
				     const u8 *data, unsigned int len,
				     u8 *out)
{
	hmac_sha224(HMAC_SHA224_KEY(desc->tfm), data, len, out);
	return 0;
}

/* HMAC-SHA256 */

#define HMAC_SHA256_KEY(tfm) ((struct hmac_sha256_key *)crypto_shash_ctx(tfm))
#define HMAC_SHA256_CTX(desc) ((struct hmac_sha256_ctx *)shash_desc_ctx(desc))

static int crypto_hmac_sha256_setkey(struct crypto_shash *tfm,
				     const u8 *raw_key, unsigned int keylen)
{
	hmac_sha256_preparekey(HMAC_SHA256_KEY(tfm), raw_key, keylen);
	return 0;
}

static int crypto_hmac_sha256_init(struct shash_desc *desc)
{
	hmac_sha256_init(HMAC_SHA256_CTX(desc), HMAC_SHA256_KEY(desc->tfm));
	return 0;
}

static int crypto_hmac_sha256_update(struct shash_desc *desc,
				     const u8 *data, unsigned int len)
{
	hmac_sha256_update(HMAC_SHA256_CTX(desc), data, len);
	return 0;
}

static int crypto_hmac_sha256_final(struct shash_desc *desc, u8 *out)
{
	hmac_sha256_final(HMAC_SHA256_CTX(desc), out);
	return 0;
}

static int crypto_hmac_sha256_digest(struct shash_desc *desc,
				     const u8 *data, unsigned int len,
				     u8 *out)
{
	hmac_sha256(HMAC_SHA256_KEY(desc->tfm), data, len, out);
	return 0;
}

/* Algorithm definitions */

static struct shash_alg algs[] = {
	{
		.base.cra_name		= "sha224",
		.base.cra_driver_name	= "sha224-lib",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= SHA224_BLOCK_SIZE,
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA224_DIGEST_SIZE,
		.init			= crypto_sha224_init,
		.update			= crypto_sha224_update,
		.final			= crypto_sha224_final,
		.digest			= crypto_sha224_digest,
		.descsize		= sizeof(struct sha224_ctx),
	},
	{
		.base.cra_name		= "sha256",
		.base.cra_driver_name	= "sha256-lib",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= SHA256_BLOCK_SIZE,
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA256_DIGEST_SIZE,
		.init			= crypto_sha256_init,
		.update			= crypto_sha256_update,
		.final			= crypto_sha256_final,
		.digest			= crypto_sha256_digest,
		.descsize		= sizeof(struct sha256_ctx),
	},
	{
		.base.cra_name		= "hmac(sha224)",
		.base.cra_driver_name	= "hmac-sha224-lib",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= SHA224_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct hmac_sha224_key),
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA224_DIGEST_SIZE,
		.setkey			= crypto_hmac_sha224_setkey,
		.init			= crypto_hmac_sha224_init,
		.update			= crypto_hmac_sha224_update,
		.final			= crypto_hmac_sha224_final,
		.digest			= crypto_hmac_sha224_digest,
		.descsize		= sizeof(struct hmac_sha224_ctx),
	},
	{
		.base.cra_name		= "hmac(sha256)",
		.base.cra_driver_name	= "hmac-sha256-lib",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= SHA256_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct hmac_sha256_key),
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA256_DIGEST_SIZE,
		.setkey			= crypto_hmac_sha256_setkey,
		.init			= crypto_hmac_sha256_init,
		.update			= crypto_hmac_sha256_update,
		.final			= crypto_hmac_sha256_final,
		.digest			= crypto_hmac_sha256_digest,
		.descsize		= sizeof(struct hmac_sha256_ctx),
	},
};

static int __init crypto_sha256_mod_init(void)
{
	return crypto_register_shashes(algs, ARRAY_SIZE(algs));
}
module_init(crypto_sha256_mod_init);

static void __exit crypto_sha256_mod_exit(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}
module_exit(crypto_sha256_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto API support for SHA-224, SHA-256, HMAC-SHA224, and HMAC-SHA256");

MODULE_ALIAS_CRYPTO("sha224");
MODULE_ALIAS_CRYPTO("sha224-lib");
MODULE_ALIAS_CRYPTO("sha256");
MODULE_ALIAS_CRYPTO("sha256-lib");
MODULE_ALIAS_CRYPTO("hmac(sha224)");
MODULE_ALIAS_CRYPTO("hmac-sha224-lib");
MODULE_ALIAS_CRYPTO("hmac(sha256)");
MODULE_ALIAS_CRYPTO("hmac-sha256-lib");
