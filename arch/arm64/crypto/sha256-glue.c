// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux/arm64 port of the OpenSSL SHA256 implementation for AArch64
 *
 * Copyright (c) 2016 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha256_base.h>
#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/module.h>

MODULE_DESCRIPTION("SHA-224/SHA-256 secure hash for arm64");
MODULE_AUTHOR("Andy Polyakov <appro@openssl.org>");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("sha224");
MODULE_ALIAS_CRYPTO("sha256");

asmlinkage void sha256_block_data_order(u32 *digest, const void *data,
					unsigned int num_blks);
EXPORT_SYMBOL(sha256_block_data_order);

static void sha256_arm64_transform(struct crypto_sha256_state *sst,
				   u8 const *src, int blocks)
{
	sha256_block_data_order(sst->state, src, blocks);
}

asmlinkage void sha256_block_neon(u32 *digest, const void *data,
				  unsigned int num_blks);

static void sha256_neon_transform(struct crypto_sha256_state *sst,
				  u8 const *src, int blocks)
{
	kernel_neon_begin();
	sha256_block_neon(sst->state, src, blocks);
	kernel_neon_end();
}

static int crypto_sha256_arm64_update(struct shash_desc *desc, const u8 *data,
				      unsigned int len)
{
	return sha256_base_do_update_blocks(desc, data, len,
					    sha256_arm64_transform);
}

static int crypto_sha256_arm64_finup(struct shash_desc *desc, const u8 *data,
				     unsigned int len, u8 *out)
{
	sha256_base_do_finup(desc, data, len, sha256_arm64_transform);
	return sha256_base_finish(desc, out);
}

static struct shash_alg algs[] = { {
	.digestsize		= SHA256_DIGEST_SIZE,
	.init			= sha256_base_init,
	.update			= crypto_sha256_arm64_update,
	.finup			= crypto_sha256_arm64_finup,
	.descsize		= sizeof(struct crypto_sha256_state),
	.base.cra_name		= "sha256",
	.base.cra_driver_name	= "sha256-arm64",
	.base.cra_priority	= 125,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
				  CRYPTO_AHASH_ALG_FINUP_MAX,
	.base.cra_blocksize	= SHA256_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}, {
	.digestsize		= SHA224_DIGEST_SIZE,
	.init			= sha224_base_init,
	.update			= crypto_sha256_arm64_update,
	.finup			= crypto_sha256_arm64_finup,
	.descsize		= sizeof(struct crypto_sha256_state),
	.base.cra_name		= "sha224",
	.base.cra_driver_name	= "sha224-arm64",
	.base.cra_priority	= 125,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
				  CRYPTO_AHASH_ALG_FINUP_MAX,
	.base.cra_blocksize	= SHA224_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
} };

static int sha256_update_neon(struct shash_desc *desc, const u8 *data,
			      unsigned int len)
{
	do {
		unsigned int chunk = len;

		/*
		 * Don't hog the CPU for the entire time it takes to process all
		 * input when running on a preemptible kernel, but process the
		 * data block by block instead.
		 */
		if (IS_ENABLED(CONFIG_PREEMPTION))
			chunk = SHA256_BLOCK_SIZE;

		chunk -= sha256_base_do_update_blocks(desc, data, chunk,
						      sha256_neon_transform);
		data += chunk;
		len -= chunk;
	} while (len >= SHA256_BLOCK_SIZE);
	return len;
}

static int sha256_finup_neon(struct shash_desc *desc, const u8 *data,
			     unsigned int len, u8 *out)
{
	if (len >= SHA256_BLOCK_SIZE) {
		int remain = sha256_update_neon(desc, data, len);

		data += len - remain;
		len = remain;
	}
	sha256_base_do_finup(desc, data, len, sha256_neon_transform);
	return sha256_base_finish(desc, out);
}

static struct shash_alg neon_algs[] = { {
	.digestsize		= SHA256_DIGEST_SIZE,
	.init			= sha256_base_init,
	.update			= sha256_update_neon,
	.finup			= sha256_finup_neon,
	.descsize		= sizeof(struct crypto_sha256_state),
	.base.cra_name		= "sha256",
	.base.cra_driver_name	= "sha256-arm64-neon",
	.base.cra_priority	= 150,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
				  CRYPTO_AHASH_ALG_FINUP_MAX,
	.base.cra_blocksize	= SHA256_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}, {
	.digestsize		= SHA224_DIGEST_SIZE,
	.init			= sha224_base_init,
	.update			= sha256_update_neon,
	.finup			= sha256_finup_neon,
	.descsize		= sizeof(struct crypto_sha256_state),
	.base.cra_name		= "sha224",
	.base.cra_driver_name	= "sha224-arm64-neon",
	.base.cra_priority	= 150,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
				  CRYPTO_AHASH_ALG_FINUP_MAX,
	.base.cra_blocksize	= SHA224_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
} };

static int __init sha256_mod_init(void)
{
	int ret = crypto_register_shashes(algs, ARRAY_SIZE(algs));
	if (ret)
		return ret;

	if (cpu_have_named_feature(ASIMD)) {
		ret = crypto_register_shashes(neon_algs, ARRAY_SIZE(neon_algs));
		if (ret)
			crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
	}
	return ret;
}

static void __exit sha256_mod_fini(void)
{
	if (cpu_have_named_feature(ASIMD))
		crypto_unregister_shashes(neon_algs, ARRAY_SIZE(neon_algs));
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}

module_init(sha256_mod_init);
module_exit(sha256_mod_fini);
