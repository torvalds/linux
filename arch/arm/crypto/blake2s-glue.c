// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BLAKE2s digest algorithm, ARM scalar implementation
 *
 * Copyright 2020 Google LLC
 */

#include <crypto/internal/blake2s.h>
#include <crypto/internal/hash.h>

#include <linux/module.h>

/* defined in blake2s-core.S */
EXPORT_SYMBOL(blake2s_compress_arch);

static int crypto_blake2s_update_arm(struct shash_desc *desc,
				     const u8 *in, unsigned int inlen)
{
	return crypto_blake2s_update(desc, in, inlen, blake2s_compress_arch);
}

static int crypto_blake2s_final_arm(struct shash_desc *desc, u8 *out)
{
	return crypto_blake2s_final(desc, out, blake2s_compress_arch);
}

#define BLAKE2S_ALG(name, driver_name, digest_size)			\
	{								\
		.base.cra_name		= name,				\
		.base.cra_driver_name	= driver_name,			\
		.base.cra_priority	= 200,				\
		.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,	\
		.base.cra_blocksize	= BLAKE2S_BLOCK_SIZE,		\
		.base.cra_ctxsize	= sizeof(struct blake2s_tfm_ctx), \
		.base.cra_module	= THIS_MODULE,			\
		.digestsize		= digest_size,			\
		.setkey			= crypto_blake2s_setkey,	\
		.init			= crypto_blake2s_init,		\
		.update			= crypto_blake2s_update_arm,	\
		.final			= crypto_blake2s_final_arm,	\
		.descsize		= sizeof(struct blake2s_state),	\
	}

static struct shash_alg blake2s_arm_algs[] = {
	BLAKE2S_ALG("blake2s-128", "blake2s-128-arm", BLAKE2S_128_HASH_SIZE),
	BLAKE2S_ALG("blake2s-160", "blake2s-160-arm", BLAKE2S_160_HASH_SIZE),
	BLAKE2S_ALG("blake2s-224", "blake2s-224-arm", BLAKE2S_224_HASH_SIZE),
	BLAKE2S_ALG("blake2s-256", "blake2s-256-arm", BLAKE2S_256_HASH_SIZE),
};

static int __init blake2s_arm_mod_init(void)
{
	return IS_REACHABLE(CONFIG_CRYPTO_HASH) ?
		crypto_register_shashes(blake2s_arm_algs,
					ARRAY_SIZE(blake2s_arm_algs)) : 0;
}

static void __exit blake2s_arm_mod_exit(void)
{
	if (IS_REACHABLE(CONFIG_CRYPTO_HASH))
		crypto_unregister_shashes(blake2s_arm_algs,
					  ARRAY_SIZE(blake2s_arm_algs));
}

module_init(blake2s_arm_mod_init);
module_exit(blake2s_arm_mod_exit);

MODULE_DESCRIPTION("BLAKE2s digest algorithm, ARM scalar implementation");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Biggers <ebiggers@google.com>");
MODULE_ALIAS_CRYPTO("blake2s-128");
MODULE_ALIAS_CRYPTO("blake2s-128-arm");
MODULE_ALIAS_CRYPTO("blake2s-160");
MODULE_ALIAS_CRYPTO("blake2s-160-arm");
MODULE_ALIAS_CRYPTO("blake2s-224");
MODULE_ALIAS_CRYPTO("blake2s-224-arm");
MODULE_ALIAS_CRYPTO("blake2s-256");
MODULE_ALIAS_CRYPTO("blake2s-256-arm");
