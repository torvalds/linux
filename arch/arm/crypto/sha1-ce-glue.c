// SPDX-License-Identifier: GPL-2.0-only
/*
 * sha1-ce-glue.c - SHA-1 secure hash using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <crypto/sha1_base.h>
#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/module.h>

MODULE_DESCRIPTION("SHA1 secure hash using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

asmlinkage void sha1_ce_transform(struct sha1_state *sst, u8 const *src,
				  int blocks);

static int sha1_ce_update(struct shash_desc *desc, const u8 *data,
			  unsigned int len)
{
	int remain;

	kernel_neon_begin();
	remain = sha1_base_do_update_blocks(desc, data, len, sha1_ce_transform);
	kernel_neon_end();

	return remain;
}

static int sha1_ce_finup(struct shash_desc *desc, const u8 *data,
			 unsigned int len, u8 *out)
{
	kernel_neon_begin();
	sha1_base_do_finup(desc, data, len, sha1_ce_transform);
	kernel_neon_end();

	return sha1_base_finish(desc, out);
}

static struct shash_alg alg = {
	.init			= sha1_base_init,
	.update			= sha1_ce_update,
	.finup			= sha1_ce_finup,
	.descsize		= SHA1_STATE_SIZE,
	.digestsize		= SHA1_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha1",
		.cra_driver_name	= "sha1-ce",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize		= SHA1_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
};

static int __init sha1_ce_mod_init(void)
{
	return crypto_register_shash(&alg);
}

static void __exit sha1_ce_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_cpu_feature_match(SHA1, sha1_ce_mod_init);
module_exit(sha1_ce_mod_fini);
