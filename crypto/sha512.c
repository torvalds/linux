// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Crypto API support for SHA-384, SHA-512, HMAC-SHA384, and HMAC-SHA512
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2003 Kyle McMartin <kyle@debian.org>
 * Copyright 2025 Google LLC
 */
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <linux/kernel.h>
#include <linux/module.h>

/*
 * Export and import functions.  crypto_shash wants a particular format that
 * matches that used by some legacy drivers.  It currently is the same as the
 * library SHA context, except the value in bytecount_lo must be block-aligned
 * and the remainder must be stored in an extra u8 appended to the struct.
 */

#define SHA512_SHASH_STATE_SIZE 209
static_assert(offsetof(struct __sha512_ctx, state) == 0);
static_assert(offsetof(struct __sha512_ctx, bytecount_lo) == 64);
static_assert(offsetof(struct __sha512_ctx, bytecount_hi) == 72);
static_assert(offsetof(struct __sha512_ctx, buf) == 80);
static_assert(sizeof(struct __sha512_ctx) + 1 == SHA512_SHASH_STATE_SIZE);

static int __crypto_sha512_export(const struct __sha512_ctx *ctx0, void *out)
{
	struct __sha512_ctx ctx = *ctx0;
	unsigned int partial;
	u8 *p = out;

	partial = ctx.bytecount_lo % SHA512_BLOCK_SIZE;
	ctx.bytecount_lo -= partial;
	memcpy(p, &ctx, sizeof(ctx));
	p += sizeof(ctx);
	*p = partial;
	return 0;
}

static int __crypto_sha512_import(struct __sha512_ctx *ctx, const void *in)
{
	const u8 *p = in;

	memcpy(ctx, p, sizeof(*ctx));
	p += sizeof(*ctx);
	ctx->bytecount_lo += *p;
	return 0;
}

static int __crypto_sha512_export_core(const struct __sha512_ctx *ctx,
				       void *out)
{
	memcpy(out, ctx, offsetof(struct __sha512_ctx, buf));
	return 0;
}

static int __crypto_sha512_import_core(struct __sha512_ctx *ctx, const void *in)
{
	memcpy(ctx, in, offsetof(struct __sha512_ctx, buf));
	return 0;
}

/* SHA-384 */

const u8 sha384_zero_message_hash[SHA384_DIGEST_SIZE] = {
	0x38, 0xb0, 0x60, 0xa7, 0x51, 0xac, 0x96, 0x38,
	0x4c, 0xd9, 0x32, 0x7e, 0xb1, 0xb1, 0xe3, 0x6a,
	0x21, 0xfd, 0xb7, 0x11, 0x14, 0xbe, 0x07, 0x43,
	0x4c, 0x0c, 0xc7, 0xbf, 0x63, 0xf6, 0xe1, 0xda,
	0x27, 0x4e, 0xde, 0xbf, 0xe7, 0x6f, 0x65, 0xfb,
	0xd5, 0x1a, 0xd2, 0xf1, 0x48, 0x98, 0xb9, 0x5b
};
EXPORT_SYMBOL_GPL(sha384_zero_message_hash);

#define SHA384_CTX(desc) ((struct sha384_ctx *)shash_desc_ctx(desc))

static int crypto_sha384_init(struct shash_desc *desc)
{
	sha384_init(SHA384_CTX(desc));
	return 0;
}

static int crypto_sha384_update(struct shash_desc *desc,
				const u8 *data, unsigned int len)
{
	sha384_update(SHA384_CTX(desc), data, len);
	return 0;
}

static int crypto_sha384_final(struct shash_desc *desc, u8 *out)
{
	sha384_final(SHA384_CTX(desc), out);
	return 0;
}

static int crypto_sha384_digest(struct shash_desc *desc,
				const u8 *data, unsigned int len, u8 *out)
{
	sha384(data, len, out);
	return 0;
}

static int crypto_sha384_export(struct shash_desc *desc, void *out)
{
	return __crypto_sha512_export(&SHA384_CTX(desc)->ctx, out);
}

static int crypto_sha384_import(struct shash_desc *desc, const void *in)
{
	return __crypto_sha512_import(&SHA384_CTX(desc)->ctx, in);
}

static int crypto_sha384_export_core(struct shash_desc *desc, void *out)
{
	return __crypto_sha512_export_core(&SHA384_CTX(desc)->ctx, out);
}

static int crypto_sha384_import_core(struct shash_desc *desc, const void *in)
{
	return __crypto_sha512_import_core(&SHA384_CTX(desc)->ctx, in);
}

/* SHA-512 */

const u8 sha512_zero_message_hash[SHA512_DIGEST_SIZE] = {
	0xcf, 0x83, 0xe1, 0x35, 0x7e, 0xef, 0xb8, 0xbd,
	0xf1, 0x54, 0x28, 0x50, 0xd6, 0x6d, 0x80, 0x07,
	0xd6, 0x20, 0xe4, 0x05, 0x0b, 0x57, 0x15, 0xdc,
	0x83, 0xf4, 0xa9, 0x21, 0xd3, 0x6c, 0xe9, 0xce,
	0x47, 0xd0, 0xd1, 0x3c, 0x5d, 0x85, 0xf2, 0xb0,
	0xff, 0x83, 0x18, 0xd2, 0x87, 0x7e, 0xec, 0x2f,
	0x63, 0xb9, 0x31, 0xbd, 0x47, 0x41, 0x7a, 0x81,
	0xa5, 0x38, 0x32, 0x7a, 0xf9, 0x27, 0xda, 0x3e
};
EXPORT_SYMBOL_GPL(sha512_zero_message_hash);

#define SHA512_CTX(desc) ((struct sha512_ctx *)shash_desc_ctx(desc))

static int crypto_sha512_init(struct shash_desc *desc)
{
	sha512_init(SHA512_CTX(desc));
	return 0;
}

static int crypto_sha512_update(struct shash_desc *desc,
				const u8 *data, unsigned int len)
{
	sha512_update(SHA512_CTX(desc), data, len);
	return 0;
}

static int crypto_sha512_final(struct shash_desc *desc, u8 *out)
{
	sha512_final(SHA512_CTX(desc), out);
	return 0;
}

static int crypto_sha512_digest(struct shash_desc *desc,
				const u8 *data, unsigned int len, u8 *out)
{
	sha512(data, len, out);
	return 0;
}

static int crypto_sha512_export(struct shash_desc *desc, void *out)
{
	return __crypto_sha512_export(&SHA512_CTX(desc)->ctx, out);
}

static int crypto_sha512_import(struct shash_desc *desc, const void *in)
{
	return __crypto_sha512_import(&SHA512_CTX(desc)->ctx, in);
}

static int crypto_sha512_export_core(struct shash_desc *desc, void *out)
{
	return __crypto_sha512_export_core(&SHA512_CTX(desc)->ctx, out);
}

static int crypto_sha512_import_core(struct shash_desc *desc, const void *in)
{
	return __crypto_sha512_import_core(&SHA512_CTX(desc)->ctx, in);
}

/* HMAC-SHA384 */

#define HMAC_SHA384_KEY(tfm) ((struct hmac_sha384_key *)crypto_shash_ctx(tfm))
#define HMAC_SHA384_CTX(desc) ((struct hmac_sha384_ctx *)shash_desc_ctx(desc))

static int crypto_hmac_sha384_setkey(struct crypto_shash *tfm,
				     const u8 *raw_key, unsigned int keylen)
{
	hmac_sha384_preparekey(HMAC_SHA384_KEY(tfm), raw_key, keylen);
	return 0;
}

static int crypto_hmac_sha384_init(struct shash_desc *desc)
{
	hmac_sha384_init(HMAC_SHA384_CTX(desc), HMAC_SHA384_KEY(desc->tfm));
	return 0;
}

static int crypto_hmac_sha384_update(struct shash_desc *desc,
				     const u8 *data, unsigned int len)
{
	hmac_sha384_update(HMAC_SHA384_CTX(desc), data, len);
	return 0;
}

static int crypto_hmac_sha384_final(struct shash_desc *desc, u8 *out)
{
	hmac_sha384_final(HMAC_SHA384_CTX(desc), out);
	return 0;
}

static int crypto_hmac_sha384_digest(struct shash_desc *desc,
				     const u8 *data, unsigned int len,
				     u8 *out)
{
	hmac_sha384(HMAC_SHA384_KEY(desc->tfm), data, len, out);
	return 0;
}

static int crypto_hmac_sha384_export(struct shash_desc *desc, void *out)
{
	return __crypto_sha512_export(&HMAC_SHA384_CTX(desc)->ctx.sha_ctx, out);
}

static int crypto_hmac_sha384_import(struct shash_desc *desc, const void *in)
{
	struct hmac_sha384_ctx *ctx = HMAC_SHA384_CTX(desc);

	ctx->ctx.ostate = HMAC_SHA384_KEY(desc->tfm)->key.ostate;
	return __crypto_sha512_import(&ctx->ctx.sha_ctx, in);
}

static int crypto_hmac_sha384_export_core(struct shash_desc *desc, void *out)
{
	return __crypto_sha512_export_core(&HMAC_SHA384_CTX(desc)->ctx.sha_ctx,
					   out);
}

static int crypto_hmac_sha384_import_core(struct shash_desc *desc,
					  const void *in)
{
	struct hmac_sha384_ctx *ctx = HMAC_SHA384_CTX(desc);

	ctx->ctx.ostate = HMAC_SHA384_KEY(desc->tfm)->key.ostate;
	return __crypto_sha512_import_core(&ctx->ctx.sha_ctx, in);
}

/* HMAC-SHA512 */

#define HMAC_SHA512_KEY(tfm) ((struct hmac_sha512_key *)crypto_shash_ctx(tfm))
#define HMAC_SHA512_CTX(desc) ((struct hmac_sha512_ctx *)shash_desc_ctx(desc))

static int crypto_hmac_sha512_setkey(struct crypto_shash *tfm,
				     const u8 *raw_key, unsigned int keylen)
{
	hmac_sha512_preparekey(HMAC_SHA512_KEY(tfm), raw_key, keylen);
	return 0;
}

static int crypto_hmac_sha512_init(struct shash_desc *desc)
{
	hmac_sha512_init(HMAC_SHA512_CTX(desc), HMAC_SHA512_KEY(desc->tfm));
	return 0;
}

static int crypto_hmac_sha512_update(struct shash_desc *desc,
				     const u8 *data, unsigned int len)
{
	hmac_sha512_update(HMAC_SHA512_CTX(desc), data, len);
	return 0;
}

static int crypto_hmac_sha512_final(struct shash_desc *desc, u8 *out)
{
	hmac_sha512_final(HMAC_SHA512_CTX(desc), out);
	return 0;
}

static int crypto_hmac_sha512_digest(struct shash_desc *desc,
				     const u8 *data, unsigned int len,
				     u8 *out)
{
	hmac_sha512(HMAC_SHA512_KEY(desc->tfm), data, len, out);
	return 0;
}

static int crypto_hmac_sha512_export(struct shash_desc *desc, void *out)
{
	return __crypto_sha512_export(&HMAC_SHA512_CTX(desc)->ctx.sha_ctx, out);
}

static int crypto_hmac_sha512_import(struct shash_desc *desc, const void *in)
{
	struct hmac_sha512_ctx *ctx = HMAC_SHA512_CTX(desc);

	ctx->ctx.ostate = HMAC_SHA512_KEY(desc->tfm)->key.ostate;
	return __crypto_sha512_import(&ctx->ctx.sha_ctx, in);
}

static int crypto_hmac_sha512_export_core(struct shash_desc *desc, void *out)
{
	return __crypto_sha512_export_core(&HMAC_SHA512_CTX(desc)->ctx.sha_ctx,
					   out);
}

static int crypto_hmac_sha512_import_core(struct shash_desc *desc,
					  const void *in)
{
	struct hmac_sha512_ctx *ctx = HMAC_SHA512_CTX(desc);

	ctx->ctx.ostate = HMAC_SHA512_KEY(desc->tfm)->key.ostate;
	return __crypto_sha512_import_core(&ctx->ctx.sha_ctx, in);
}

/* Algorithm definitions */

static struct shash_alg algs[] = {
	{
		.base.cra_name		= "sha384",
		.base.cra_driver_name	= "sha384-lib",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= SHA384_BLOCK_SIZE,
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA384_DIGEST_SIZE,
		.init			= crypto_sha384_init,
		.update			= crypto_sha384_update,
		.final			= crypto_sha384_final,
		.digest			= crypto_sha384_digest,
		.export			= crypto_sha384_export,
		.import			= crypto_sha384_import,
		.export_core		= crypto_sha384_export_core,
		.import_core		= crypto_sha384_import_core,
		.descsize		= sizeof(struct sha384_ctx),
		.statesize		= SHA512_SHASH_STATE_SIZE,
	},
	{
		.base.cra_name		= "sha512",
		.base.cra_driver_name	= "sha512-lib",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= SHA512_BLOCK_SIZE,
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA512_DIGEST_SIZE,
		.init			= crypto_sha512_init,
		.update			= crypto_sha512_update,
		.final			= crypto_sha512_final,
		.digest			= crypto_sha512_digest,
		.export			= crypto_sha512_export,
		.import			= crypto_sha512_import,
		.export_core		= crypto_sha512_export_core,
		.import_core		= crypto_sha512_import_core,
		.descsize		= sizeof(struct sha512_ctx),
		.statesize		= SHA512_SHASH_STATE_SIZE,
	},
	{
		.base.cra_name		= "hmac(sha384)",
		.base.cra_driver_name	= "hmac-sha384-lib",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= SHA384_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct hmac_sha384_key),
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA384_DIGEST_SIZE,
		.setkey			= crypto_hmac_sha384_setkey,
		.init			= crypto_hmac_sha384_init,
		.update			= crypto_hmac_sha384_update,
		.final			= crypto_hmac_sha384_final,
		.digest			= crypto_hmac_sha384_digest,
		.export			= crypto_hmac_sha384_export,
		.import			= crypto_hmac_sha384_import,
		.export_core		= crypto_hmac_sha384_export_core,
		.import_core		= crypto_hmac_sha384_import_core,
		.descsize		= sizeof(struct hmac_sha384_ctx),
		.statesize		= SHA512_SHASH_STATE_SIZE,
	},
	{
		.base.cra_name		= "hmac(sha512)",
		.base.cra_driver_name	= "hmac-sha512-lib",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= SHA512_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct hmac_sha512_key),
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA512_DIGEST_SIZE,
		.setkey			= crypto_hmac_sha512_setkey,
		.init			= crypto_hmac_sha512_init,
		.update			= crypto_hmac_sha512_update,
		.final			= crypto_hmac_sha512_final,
		.digest			= crypto_hmac_sha512_digest,
		.export			= crypto_hmac_sha512_export,
		.import			= crypto_hmac_sha512_import,
		.export_core		= crypto_hmac_sha512_export_core,
		.import_core		= crypto_hmac_sha512_import_core,
		.descsize		= sizeof(struct hmac_sha512_ctx),
		.statesize		= SHA512_SHASH_STATE_SIZE,
	},
};

static int __init crypto_sha512_mod_init(void)
{
	return crypto_register_shashes(algs, ARRAY_SIZE(algs));
}
module_init(crypto_sha512_mod_init);

static void __exit crypto_sha512_mod_exit(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}
module_exit(crypto_sha512_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto API support for SHA-384, SHA-512, HMAC-SHA384, and HMAC-SHA512");

MODULE_ALIAS_CRYPTO("sha384");
MODULE_ALIAS_CRYPTO("sha384-lib");
MODULE_ALIAS_CRYPTO("sha512");
MODULE_ALIAS_CRYPTO("sha512-lib");
MODULE_ALIAS_CRYPTO("hmac(sha384)");
MODULE_ALIAS_CRYPTO("hmac-sha384-lib");
MODULE_ALIAS_CRYPTO("hmac(sha512)");
MODULE_ALIAS_CRYPTO("hmac-sha512-lib");
