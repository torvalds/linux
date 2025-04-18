// SPDX-License-Identifier: GPL-2.0-only
/*
 * sha2-ce-glue.c - SHA-224/SHA-256 using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha256_base.h>
#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/module.h>

MODULE_DESCRIPTION("SHA-224/SHA-256 secure hash using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

asmlinkage void sha2_ce_transform(struct crypto_sha256_state *sst,
				  u8 const *src, int blocks);

static int sha2_ce_update(struct shash_desc *desc, const u8 *data,
			  unsigned int len)
{
	int remain;

	kernel_neon_begin();
	remain = sha256_base_do_update_blocks(desc, data, len,
					      sha2_ce_transform);
	kernel_neon_end();
	return remain;
}

static int sha2_ce_finup(struct shash_desc *desc, const u8 *data,
			 unsigned int len, u8 *out)
{
	kernel_neon_begin();
	sha256_base_do_finup(desc, data, len, sha2_ce_transform);
	kernel_neon_end();
	return sha256_base_finish(desc, out);
}

static struct shash_alg algs[] = { {
	.init			= sha224_base_init,
	.update			= sha2_ce_update,
	.finup			= sha2_ce_finup,
	.descsize		= sizeof(struct crypto_sha256_state),
	.digestsize		= SHA224_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha224",
		.cra_driver_name	= "sha224-ce",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
					  CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize		= SHA256_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
}, {
	.init			= sha256_base_init,
	.update			= sha2_ce_update,
	.finup			= sha2_ce_finup,
	.descsize		= sizeof(struct crypto_sha256_state),
	.digestsize		= SHA256_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha256",
		.cra_driver_name	= "sha256-ce",
		.cra_priority		= 300,
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
