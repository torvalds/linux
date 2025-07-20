// SPDX-License-Identifier: GPL-2.0-only
/*
 * sha512-glue.c - accelerated SHA-384/512 for ARM
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha512_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "sha512.h"

MODULE_DESCRIPTION("Accelerated SHA-384/SHA-512 secure hash for ARM");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

MODULE_ALIAS_CRYPTO("sha384");
MODULE_ALIAS_CRYPTO("sha512");
MODULE_ALIAS_CRYPTO("sha384-arm");
MODULE_ALIAS_CRYPTO("sha512-arm");

asmlinkage void sha512_block_data_order(struct sha512_state *state,
					u8 const *src, int blocks);

static int sha512_arm_update(struct shash_desc *desc, const u8 *data,
			     unsigned int len)
{
	return sha512_base_do_update_blocks(desc, data, len,
					    sha512_block_data_order);
}

static int sha512_arm_finup(struct shash_desc *desc, const u8 *data,
			    unsigned int len, u8 *out)
{
	sha512_base_do_finup(desc, data, len, sha512_block_data_order);
	return sha512_base_finish(desc, out);
}

static struct shash_alg sha512_arm_algs[] = { {
	.init			= sha384_base_init,
	.update			= sha512_arm_update,
	.finup			= sha512_arm_finup,
	.descsize		= SHA512_STATE_SIZE,
	.digestsize		= SHA384_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha384",
		.cra_driver_name	= "sha384-arm",
		.cra_priority		= 250,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
					  CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize		= SHA512_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
},  {
	.init			= sha512_base_init,
	.update			= sha512_arm_update,
	.finup			= sha512_arm_finup,
	.descsize		= SHA512_STATE_SIZE,
	.digestsize		= SHA512_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha512",
		.cra_driver_name	= "sha512-arm",
		.cra_priority		= 250,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
					  CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize		= SHA512_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
} };

static int __init sha512_arm_mod_init(void)
{
	int err;

	err = crypto_register_shashes(sha512_arm_algs,
				      ARRAY_SIZE(sha512_arm_algs));
	if (err)
		return err;

	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) && cpu_has_neon()) {
		err = crypto_register_shashes(sha512_neon_algs,
					      ARRAY_SIZE(sha512_neon_algs));
		if (err)
			goto err_unregister;
	}
	return 0;

err_unregister:
	crypto_unregister_shashes(sha512_arm_algs,
				  ARRAY_SIZE(sha512_arm_algs));

	return err;
}

static void __exit sha512_arm_mod_fini(void)
{
	crypto_unregister_shashes(sha512_arm_algs,
				  ARRAY_SIZE(sha512_arm_algs));
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) && cpu_has_neon())
		crypto_unregister_shashes(sha512_neon_algs,
					  ARRAY_SIZE(sha512_neon_algs));
}

module_init(sha512_arm_mod_init);
module_exit(sha512_arm_mod_fini);
