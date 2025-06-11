// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BLAKE2b digest algorithm, NEON accelerated
 *
 * Copyright 2020 Google LLC
 */

#include <crypto/internal/blake2b.h>
#include <crypto/internal/hash.h>

#include <linux/module.h>
#include <linux/sizes.h>

#include <asm/neon.h>
#include <asm/simd.h>

asmlinkage void blake2b_compress_neon(struct blake2b_state *state,
				      const u8 *block, size_t nblocks, u32 inc);

static void blake2b_compress_arch(struct blake2b_state *state,
				  const u8 *block, size_t nblocks, u32 inc)
{
	do {
		const size_t blocks = min_t(size_t, nblocks,
					    SZ_4K / BLAKE2B_BLOCK_SIZE);

		kernel_neon_begin();
		blake2b_compress_neon(state, block, blocks, inc);
		kernel_neon_end();

		nblocks -= blocks;
		block += blocks * BLAKE2B_BLOCK_SIZE;
	} while (nblocks);
}

static int crypto_blake2b_update_neon(struct shash_desc *desc,
				      const u8 *in, unsigned int inlen)
{
	return crypto_blake2b_update_bo(desc, in, inlen, blake2b_compress_arch);
}

static int crypto_blake2b_finup_neon(struct shash_desc *desc, const u8 *in,
				     unsigned int inlen, u8 *out)
{
	return crypto_blake2b_finup(desc, in, inlen, out,
				    blake2b_compress_arch);
}

#define BLAKE2B_ALG(name, driver_name, digest_size)			\
	{								\
		.base.cra_name		= name,				\
		.base.cra_driver_name	= driver_name,			\
		.base.cra_priority	= 200,				\
		.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY |	\
					  CRYPTO_AHASH_ALG_BLOCK_ONLY |	\
					  CRYPTO_AHASH_ALG_FINAL_NONZERO, \
		.base.cra_blocksize	= BLAKE2B_BLOCK_SIZE,		\
		.base.cra_ctxsize	= sizeof(struct blake2b_tfm_ctx), \
		.base.cra_module	= THIS_MODULE,			\
		.digestsize		= digest_size,			\
		.setkey			= crypto_blake2b_setkey,	\
		.init			= crypto_blake2b_init,		\
		.update			= crypto_blake2b_update_neon,	\
		.finup			= crypto_blake2b_finup_neon,	\
		.descsize		= sizeof(struct blake2b_state),	\
		.statesize		= BLAKE2B_STATE_SIZE,		\
	}

static struct shash_alg blake2b_neon_algs[] = {
	BLAKE2B_ALG("blake2b-160", "blake2b-160-neon", BLAKE2B_160_HASH_SIZE),
	BLAKE2B_ALG("blake2b-256", "blake2b-256-neon", BLAKE2B_256_HASH_SIZE),
	BLAKE2B_ALG("blake2b-384", "blake2b-384-neon", BLAKE2B_384_HASH_SIZE),
	BLAKE2B_ALG("blake2b-512", "blake2b-512-neon", BLAKE2B_512_HASH_SIZE),
};

static int __init blake2b_neon_mod_init(void)
{
	if (!(elf_hwcap & HWCAP_NEON))
		return -ENODEV;

	return crypto_register_shashes(blake2b_neon_algs,
				       ARRAY_SIZE(blake2b_neon_algs));
}

static void __exit blake2b_neon_mod_exit(void)
{
	crypto_unregister_shashes(blake2b_neon_algs,
				  ARRAY_SIZE(blake2b_neon_algs));
}

module_init(blake2b_neon_mod_init);
module_exit(blake2b_neon_mod_exit);

MODULE_DESCRIPTION("BLAKE2b digest algorithm, NEON accelerated");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Biggers <ebiggers@google.com>");
MODULE_ALIAS_CRYPTO("blake2b-160");
MODULE_ALIAS_CRYPTO("blake2b-160-neon");
MODULE_ALIAS_CRYPTO("blake2b-256");
MODULE_ALIAS_CRYPTO("blake2b-256-neon");
MODULE_ALIAS_CRYPTO("blake2b-384");
MODULE_ALIAS_CRYPTO("blake2b-384-neon");
MODULE_ALIAS_CRYPTO("blake2b-512");
MODULE_ALIAS_CRYPTO("blake2b-512-neon");
