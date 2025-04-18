// SPDX-License-Identifier: GPL-2.0-only
/*
 * sha2-ce-glue.c - SHA-224/SHA-256 using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2014 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha256_base.h>
#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

MODULE_DESCRIPTION("SHA-224/SHA-256 secure hash using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("sha224");
MODULE_ALIAS_CRYPTO("sha256");

struct sha256_ce_state {
	struct crypto_sha256_state sst;
	u32			finalize;
};

extern const u32 sha256_ce_offsetof_count;
extern const u32 sha256_ce_offsetof_finalize;

asmlinkage int __sha256_ce_transform(struct sha256_ce_state *sst, u8 const *src,
				     int blocks);

static void sha256_ce_transform(struct crypto_sha256_state *sst, u8 const *src,
				int blocks)
{
	while (blocks) {
		int rem;

		kernel_neon_begin();
		rem = __sha256_ce_transform(container_of(sst,
							 struct sha256_ce_state,
							 sst), src, blocks);
		kernel_neon_end();
		src += (blocks - rem) * SHA256_BLOCK_SIZE;
		blocks = rem;
	}
}

const u32 sha256_ce_offsetof_count = offsetof(struct sha256_ce_state,
					      sst.count);
const u32 sha256_ce_offsetof_finalize = offsetof(struct sha256_ce_state,
						 finalize);

static int sha256_ce_update(struct shash_desc *desc, const u8 *data,
			    unsigned int len)
{
	struct sha256_ce_state *sctx = shash_desc_ctx(desc);

	sctx->finalize = 0;
	return sha256_base_do_update_blocks(desc, data, len,
					    sha256_ce_transform);
}

static int sha256_ce_finup(struct shash_desc *desc, const u8 *data,
			   unsigned int len, u8 *out)
{
	struct sha256_ce_state *sctx = shash_desc_ctx(desc);
	bool finalize = !(len % SHA256_BLOCK_SIZE) && len;

	/*
	 * Allow the asm code to perform the finalization if there is no
	 * partial data and the input is a round multiple of the block size.
	 */
	sctx->finalize = finalize;

	if (finalize)
		sha256_base_do_update_blocks(desc, data, len,
					     sha256_ce_transform);
	else
		sha256_base_do_finup(desc, data, len, sha256_ce_transform);
	return sha256_base_finish(desc, out);
}

static int sha256_ce_digest(struct shash_desc *desc, const u8 *data,
			    unsigned int len, u8 *out)
{
	sha256_base_init(desc);
	return sha256_ce_finup(desc, data, len, out);
}

static struct shash_alg algs[] = { {
	.init			= sha224_base_init,
	.update			= sha256_ce_update,
	.finup			= sha256_ce_finup,
	.descsize		= sizeof(struct sha256_ce_state),
	.statesize		= sizeof(struct crypto_sha256_state),
	.digestsize		= SHA224_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha224",
		.cra_driver_name	= "sha224-ce",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
					  CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize		= SHA256_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
}, {
	.init			= sha256_base_init,
	.update			= sha256_ce_update,
	.finup			= sha256_ce_finup,
	.digest			= sha256_ce_digest,
	.descsize		= sizeof(struct sha256_ce_state),
	.statesize		= sizeof(struct crypto_sha256_state),
	.digestsize		= SHA256_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha256",
		.cra_driver_name	= "sha256-ce",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
					  CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize		= SHA256_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
} };

static int __init sha2_ce_mod_init(void)
{
	return crypto_register_shashes(algs, ARRAY_SIZE(algs));
}

static void __exit sha2_ce_mod_fini(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}

module_cpu_feature_match(SHA2, sha2_ce_mod_init);
module_exit(sha2_ce_mod_fini);
