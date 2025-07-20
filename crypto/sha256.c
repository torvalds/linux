// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Crypto API wrapper for the SHA-256 and SHA-224 library functions
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * SHA224 Support Copyright 2007 Intel Corporation <jonathan.lynch@intel.com>
 */
#include <crypto/internal/hash.h>
#include <crypto/internal/sha2.h>
#include <linux/kernel.h>
#include <linux/module.h>

const u8 sha224_zero_message_hash[SHA224_DIGEST_SIZE] = {
	0xd1, 0x4a, 0x02, 0x8c, 0x2a, 0x3a, 0x2b, 0xc9, 0x47,
	0x61, 0x02, 0xbb, 0x28, 0x82, 0x34, 0xc4, 0x15, 0xa2,
	0xb0, 0x1f, 0x82, 0x8e, 0xa6, 0x2a, 0xc5, 0xb3, 0xe4,
	0x2f
};
EXPORT_SYMBOL_GPL(sha224_zero_message_hash);

const u8 sha256_zero_message_hash[SHA256_DIGEST_SIZE] = {
	0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
	0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
	0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
	0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
};
EXPORT_SYMBOL_GPL(sha256_zero_message_hash);

static int crypto_sha256_init(struct shash_desc *desc)
{
	sha256_block_init(shash_desc_ctx(desc));
	return 0;
}

static inline int crypto_sha256_update(struct shash_desc *desc, const u8 *data,
				       unsigned int len, bool force_generic)
{
	struct crypto_sha256_state *sctx = shash_desc_ctx(desc);
	int remain = len % SHA256_BLOCK_SIZE;

	sctx->count += len - remain;
	sha256_choose_blocks(sctx->state, data, len / SHA256_BLOCK_SIZE,
			     force_generic, !force_generic);
	return remain;
}

static int crypto_sha256_update_generic(struct shash_desc *desc, const u8 *data,
					unsigned int len)
{
	return crypto_sha256_update(desc, data, len, true);
}

static int crypto_sha256_update_lib(struct shash_desc *desc, const u8 *data,
				    unsigned int len)
{
	sha256_update(shash_desc_ctx(desc), data, len);
	return 0;
}

static int crypto_sha256_update_arch(struct shash_desc *desc, const u8 *data,
				     unsigned int len)
{
	return crypto_sha256_update(desc, data, len, false);
}

static int crypto_sha256_final_lib(struct shash_desc *desc, u8 *out)
{
	sha256_final(shash_desc_ctx(desc), out);
	return 0;
}

static __always_inline int crypto_sha256_finup(struct shash_desc *desc,
					       const u8 *data,
					       unsigned int len, u8 *out,
					       bool force_generic)
{
	struct crypto_sha256_state *sctx = shash_desc_ctx(desc);
	unsigned int remain = len;
	u8 *buf;

	if (len >= SHA256_BLOCK_SIZE)
		remain = crypto_sha256_update(desc, data, len, force_generic);
	sctx->count += remain;
	buf = memcpy(sctx + 1, data + len - remain, remain);
	sha256_finup(sctx, buf, remain, out,
		     crypto_shash_digestsize(desc->tfm), force_generic,
		     !force_generic);
	return 0;
}

static int crypto_sha256_finup_generic(struct shash_desc *desc, const u8 *data,
				       unsigned int len, u8 *out)
{
	return crypto_sha256_finup(desc, data, len, out, true);
}

static int crypto_sha256_finup_arch(struct shash_desc *desc, const u8 *data,
				    unsigned int len, u8 *out)
{
	return crypto_sha256_finup(desc, data, len, out, false);
}

static int crypto_sha256_digest_generic(struct shash_desc *desc, const u8 *data,
					unsigned int len, u8 *out)
{
	crypto_sha256_init(desc);
	return crypto_sha256_finup_generic(desc, data, len, out);
}

static int crypto_sha256_digest_lib(struct shash_desc *desc, const u8 *data,
				    unsigned int len, u8 *out)
{
	sha256(data, len, out);
	return 0;
}

static int crypto_sha256_digest_arch(struct shash_desc *desc, const u8 *data,
				     unsigned int len, u8 *out)
{
	crypto_sha256_init(desc);
	return crypto_sha256_finup_arch(desc, data, len, out);
}

static int crypto_sha224_init(struct shash_desc *desc)
{
	sha224_block_init(shash_desc_ctx(desc));
	return 0;
}

static int crypto_sha224_final_lib(struct shash_desc *desc, u8 *out)
{
	sha224_final(shash_desc_ctx(desc), out);
	return 0;
}

static int crypto_sha256_import_lib(struct shash_desc *desc, const void *in)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	const u8 *p = in;

	memcpy(sctx, p, sizeof(*sctx));
	p += sizeof(*sctx);
	sctx->count += *p;
	return 0;
}

static int crypto_sha256_export_lib(struct shash_desc *desc, void *out)
{
	struct sha256_state *sctx0 = shash_desc_ctx(desc);
	struct sha256_state sctx = *sctx0;
	unsigned int partial;
	u8 *p = out;

	partial = sctx.count % SHA256_BLOCK_SIZE;
	sctx.count -= partial;
	memcpy(p, &sctx, sizeof(sctx));
	p += sizeof(sctx);
	*p = partial;
	return 0;
}

static struct shash_alg algs[] = {
	{
		.base.cra_name		= "sha256",
		.base.cra_driver_name	= "sha256-generic",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
					  CRYPTO_AHASH_ALG_FINUP_MAX,
		.base.cra_blocksize	= SHA256_BLOCK_SIZE,
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA256_DIGEST_SIZE,
		.init			= crypto_sha256_init,
		.update			= crypto_sha256_update_generic,
		.finup			= crypto_sha256_finup_generic,
		.digest			= crypto_sha256_digest_generic,
		.descsize		= sizeof(struct crypto_sha256_state),
	},
	{
		.base.cra_name		= "sha224",
		.base.cra_driver_name	= "sha224-generic",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
					  CRYPTO_AHASH_ALG_FINUP_MAX,
		.base.cra_blocksize	= SHA224_BLOCK_SIZE,
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA224_DIGEST_SIZE,
		.init			= crypto_sha224_init,
		.update			= crypto_sha256_update_generic,
		.finup			= crypto_sha256_finup_generic,
		.descsize		= sizeof(struct crypto_sha256_state),
	},
	{
		.base.cra_name		= "sha256",
		.base.cra_driver_name	= "sha256-lib",
		.base.cra_blocksize	= SHA256_BLOCK_SIZE,
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA256_DIGEST_SIZE,
		.init			= crypto_sha256_init,
		.update			= crypto_sha256_update_lib,
		.final			= crypto_sha256_final_lib,
		.digest			= crypto_sha256_digest_lib,
		.descsize		= sizeof(struct sha256_state),
		.statesize		= sizeof(struct crypto_sha256_state) +
					  SHA256_BLOCK_SIZE + 1,
		.import			= crypto_sha256_import_lib,
		.export			= crypto_sha256_export_lib,
	},
	{
		.base.cra_name		= "sha224",
		.base.cra_driver_name	= "sha224-lib",
		.base.cra_blocksize	= SHA224_BLOCK_SIZE,
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA224_DIGEST_SIZE,
		.init			= crypto_sha224_init,
		.update			= crypto_sha256_update_lib,
		.final			= crypto_sha224_final_lib,
		.descsize		= sizeof(struct sha256_state),
		.statesize		= sizeof(struct crypto_sha256_state) +
					  SHA256_BLOCK_SIZE + 1,
		.import			= crypto_sha256_import_lib,
		.export			= crypto_sha256_export_lib,
	},
	{
		.base.cra_name		= "sha256",
		.base.cra_driver_name	= "sha256-" __stringify(ARCH),
		.base.cra_priority	= 300,
		.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
					  CRYPTO_AHASH_ALG_FINUP_MAX,
		.base.cra_blocksize	= SHA256_BLOCK_SIZE,
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA256_DIGEST_SIZE,
		.init			= crypto_sha256_init,
		.update			= crypto_sha256_update_arch,
		.finup			= crypto_sha256_finup_arch,
		.digest			= crypto_sha256_digest_arch,
		.descsize		= sizeof(struct crypto_sha256_state),
	},
	{
		.base.cra_name		= "sha224",
		.base.cra_driver_name	= "sha224-" __stringify(ARCH),
		.base.cra_priority	= 300,
		.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
					  CRYPTO_AHASH_ALG_FINUP_MAX,
		.base.cra_blocksize	= SHA224_BLOCK_SIZE,
		.base.cra_module	= THIS_MODULE,
		.digestsize		= SHA224_DIGEST_SIZE,
		.init			= crypto_sha224_init,
		.update			= crypto_sha256_update_arch,
		.finup			= crypto_sha256_finup_arch,
		.descsize		= sizeof(struct crypto_sha256_state),
	},
};

static unsigned int num_algs;

static int __init crypto_sha256_mod_init(void)
{
	/* register the arch flavours only if they differ from generic */
	num_algs = ARRAY_SIZE(algs);
	BUILD_BUG_ON(ARRAY_SIZE(algs) <= 2);
	if (!sha256_is_arch_optimized())
		num_algs -= 2;
	return crypto_register_shashes(algs, ARRAY_SIZE(algs));
}
module_init(crypto_sha256_mod_init);

static void __exit crypto_sha256_mod_exit(void)
{
	crypto_unregister_shashes(algs, num_algs);
}
module_exit(crypto_sha256_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto API wrapper for the SHA-256 and SHA-224 library functions");

MODULE_ALIAS_CRYPTO("sha256");
MODULE_ALIAS_CRYPTO("sha256-generic");
MODULE_ALIAS_CRYPTO("sha256-" __stringify(ARCH));
MODULE_ALIAS_CRYPTO("sha224");
MODULE_ALIAS_CRYPTO("sha224-generic");
MODULE_ALIAS_CRYPTO("sha224-" __stringify(ARCH));
