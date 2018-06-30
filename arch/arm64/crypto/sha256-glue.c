/*
 * Linux/arm64 port of the OpenSSL SHA256 implementation for AArch64
 *
 * Copyright (c) 2016 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/internal/hash.h>
#include <crypto/sha.h>
#include <crypto/sha256_base.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <linux/string.h>

MODULE_DESCRIPTION("SHA-224/SHA-256 secure hash for arm64");
MODULE_AUTHOR("Andy Polyakov <appro@openssl.org>");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("sha224");
MODULE_ALIAS_CRYPTO("sha256");

asmlinkage void sha256_block_data_order(u32 *digest, const void *data,
					unsigned int num_blks);
EXPORT_SYMBOL(sha256_block_data_order);

asmlinkage void sha256_block_neon(u32 *digest, const void *data,
				  unsigned int num_blks);

static int sha256_update(struct shash_desc *desc, const u8 *data,
			 unsigned int len)
{
	return sha256_base_do_update(desc, data, len,
				(sha256_block_fn *)sha256_block_data_order);
}

static int sha256_finup(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out)
{
	if (len)
		sha256_base_do_update(desc, data, len,
				(sha256_block_fn *)sha256_block_data_order);
	sha256_base_do_finalize(desc,
				(sha256_block_fn *)sha256_block_data_order);

	return sha256_base_finish(desc, out);
}

static int sha256_final(struct shash_desc *desc, u8 *out)
{
	return sha256_finup(desc, NULL, 0, out);
}

static struct shash_alg algs[] = { {
	.digestsize		= SHA256_DIGEST_SIZE,
	.init			= sha256_base_init,
	.update			= sha256_update,
	.final			= sha256_final,
	.finup			= sha256_finup,
	.descsize		= sizeof(struct sha256_state),
	.base.cra_name		= "sha256",
	.base.cra_driver_name	= "sha256-arm64",
	.base.cra_priority	= 100,
	.base.cra_blocksize	= SHA256_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}, {
	.digestsize		= SHA224_DIGEST_SIZE,
	.init			= sha224_base_init,
	.update			= sha256_update,
	.final			= sha256_final,
	.finup			= sha256_finup,
	.descsize		= sizeof(struct sha256_state),
	.base.cra_name		= "sha224",
	.base.cra_driver_name	= "sha224-arm64",
	.base.cra_priority	= 100,
	.base.cra_blocksize	= SHA224_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
} };

static int sha256_update_neon(struct shash_desc *desc, const u8 *data,
			      unsigned int len)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	if (!may_use_simd())
		return sha256_base_do_update(desc, data, len,
				(sha256_block_fn *)sha256_block_data_order);

	while (len > 0) {
		unsigned int chunk = len;

		/*
		 * Don't hog the CPU for the entire time it takes to process all
		 * input when running on a preemptible kernel, but process the
		 * data block by block instead.
		 */
		if (IS_ENABLED(CONFIG_PREEMPT) &&
		    chunk + sctx->count % SHA256_BLOCK_SIZE > SHA256_BLOCK_SIZE)
			chunk = SHA256_BLOCK_SIZE -
				sctx->count % SHA256_BLOCK_SIZE;

		kernel_neon_begin();
		sha256_base_do_update(desc, data, chunk,
				      (sha256_block_fn *)sha256_block_neon);
		kernel_neon_end();
		data += chunk;
		len -= chunk;
	}
	return 0;
}

static int sha256_finup_neon(struct shash_desc *desc, const u8 *data,
			     unsigned int len, u8 *out)
{
	if (!may_use_simd()) {
		if (len)
			sha256_base_do_update(desc, data, len,
				(sha256_block_fn *)sha256_block_data_order);
		sha256_base_do_finalize(desc,
				(sha256_block_fn *)sha256_block_data_order);
	} else {
		if (len)
			sha256_update_neon(desc, data, len);
		kernel_neon_begin();
		sha256_base_do_finalize(desc,
				(sha256_block_fn *)sha256_block_neon);
		kernel_neon_end();
	}
	return sha256_base_finish(desc, out);
}

static int sha256_final_neon(struct shash_desc *desc, u8 *out)
{
	return sha256_finup_neon(desc, NULL, 0, out);
}

static struct shash_alg neon_algs[] = { {
	.digestsize		= SHA256_DIGEST_SIZE,
	.init			= sha256_base_init,
	.update			= sha256_update_neon,
	.final			= sha256_final_neon,
	.finup			= sha256_finup_neon,
	.descsize		= sizeof(struct sha256_state),
	.base.cra_name		= "sha256",
	.base.cra_driver_name	= "sha256-arm64-neon",
	.base.cra_priority	= 150,
	.base.cra_blocksize	= SHA256_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}, {
	.digestsize		= SHA224_DIGEST_SIZE,
	.init			= sha224_base_init,
	.update			= sha256_update_neon,
	.final			= sha256_final_neon,
	.finup			= sha256_finup_neon,
	.descsize		= sizeof(struct sha256_state),
	.base.cra_name		= "sha224",
	.base.cra_driver_name	= "sha224-arm64-neon",
	.base.cra_priority	= 150,
	.base.cra_blocksize	= SHA224_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
} };

static int __init sha256_mod_init(void)
{
	int ret = crypto_register_shashes(algs, ARRAY_SIZE(algs));
	if (ret)
		return ret;

	if (elf_hwcap & HWCAP_ASIMD) {
		ret = crypto_register_shashes(neon_algs, ARRAY_SIZE(neon_algs));
		if (ret)
			crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
	}
	return ret;
}

static void __exit sha256_mod_fini(void)
{
	if (elf_hwcap & HWCAP_ASIMD)
		crypto_unregister_shashes(neon_algs, ARRAY_SIZE(neon_algs));
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}

module_init(sha256_mod_init);
module_exit(sha256_mod_fini);
