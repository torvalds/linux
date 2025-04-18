// SPDX-License-Identifier: GPL-2.0
/*
 * sha512-ce-glue.c - SHA-384/SHA-512 using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/neon.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha512_base.h>
#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/module.h>

MODULE_DESCRIPTION("SHA-384/SHA-512 secure hash using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("sha384");
MODULE_ALIAS_CRYPTO("sha512");

asmlinkage int __sha512_ce_transform(struct sha512_state *sst, u8 const *src,
				     int blocks);

static void sha512_ce_transform(struct sha512_state *sst, u8 const *src,
				int blocks)
{
	do {
		int rem;

		kernel_neon_begin();
		rem = __sha512_ce_transform(sst, src, blocks);
		kernel_neon_end();
		src += (blocks - rem) * SHA512_BLOCK_SIZE;
		blocks = rem;
	} while (blocks);
}

static int sha512_ce_update(struct shash_desc *desc, const u8 *data,
			    unsigned int len)
{
	return sha512_base_do_update_blocks(desc, data, len,
					    sha512_ce_transform);
}

static int sha512_ce_finup(struct shash_desc *desc, const u8 *data,
			   unsigned int len, u8 *out)
{
	sha512_base_do_finup(desc, data, len, sha512_ce_transform);
	return sha512_base_finish(desc, out);
}

static struct shash_alg algs[] = { {
	.init			= sha384_base_init,
	.update			= sha512_ce_update,
	.finup			= sha512_ce_finup,
	.descsize		= SHA512_STATE_SIZE,
	.digestsize		= SHA384_DIGEST_SIZE,
	.base.cra_name		= "sha384",
	.base.cra_driver_name	= "sha384-ce",
	.base.cra_priority	= 200,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
				  CRYPTO_AHASH_ALG_FINUP_MAX,
	.base.cra_blocksize	= SHA512_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}, {
	.init			= sha512_base_init,
	.update			= sha512_ce_update,
	.finup			= sha512_ce_finup,
	.descsize		= SHA512_STATE_SIZE,
	.digestsize		= SHA512_DIGEST_SIZE,
	.base.cra_name		= "sha512",
	.base.cra_driver_name	= "sha512-ce",
	.base.cra_priority	= 200,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
				  CRYPTO_AHASH_ALG_FINUP_MAX,
	.base.cra_blocksize	= SHA512_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
} };

static int __init sha512_ce_mod_init(void)
{
	return crypto_register_shashes(algs, ARRAY_SIZE(algs));
}

static void __exit sha512_ce_mod_fini(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}

module_cpu_feature_match(SHA512, sha512_ce_mod_init);
module_exit(sha512_ce_mod_fini);
