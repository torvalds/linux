/*
 * sha2-ce-glue.c - SHA-224/SHA-256 using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <crypto/internal/hash.h>
#include <crypto/sha.h>
#include <crypto/sha256_base.h>
#include <linux/crypto.h>
#include <linux/module.h>

#include <asm/hwcap.h>
#include <asm/simd.h>
#include <asm/neon.h>
#include <asm/unaligned.h>

#include "sha256_glue.h"

MODULE_DESCRIPTION("SHA-224/SHA-256 secure hash using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

asmlinkage void sha2_ce_transform(struct sha256_state *sst, u8 const *src,
				  int blocks);

static int sha2_ce_update(struct shash_desc *desc, const u8 *data,
			  unsigned int len)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	if (!may_use_simd() ||
	    (sctx->count % SHA256_BLOCK_SIZE) + len < SHA256_BLOCK_SIZE)
		return crypto_sha256_arm_update(desc, data, len);

	kernel_neon_begin();
	sha256_base_do_update(desc, data, len,
			      (sha256_block_fn *)sha2_ce_transform);
	kernel_neon_end();

	return 0;
}

static int sha2_ce_finup(struct shash_desc *desc, const u8 *data,
			 unsigned int len, u8 *out)
{
	if (!may_use_simd())
		return crypto_sha256_arm_finup(desc, data, len, out);

	kernel_neon_begin();
	if (len)
		sha256_base_do_update(desc, data, len,
				      (sha256_block_fn *)sha2_ce_transform);
	sha256_base_do_finalize(desc, (sha256_block_fn *)sha2_ce_transform);
	kernel_neon_end();

	return sha256_base_finish(desc, out);
}

static int sha2_ce_final(struct shash_desc *desc, u8 *out)
{
	return sha2_ce_finup(desc, NULL, 0, out);
}

static struct shash_alg algs[] = { {
	.init			= sha224_base_init,
	.update			= sha2_ce_update,
	.final			= sha2_ce_final,
	.finup			= sha2_ce_finup,
	.descsize		= sizeof(struct sha256_state),
	.digestsize		= SHA224_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha224",
		.cra_driver_name	= "sha224-ce",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize		= SHA256_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
}, {
	.init			= sha256_base_init,
	.update			= sha2_ce_update,
	.final			= sha2_ce_final,
	.finup			= sha2_ce_finup,
	.descsize		= sizeof(struct sha256_state),
	.digestsize		= SHA256_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha256",
		.cra_driver_name	= "sha256-ce",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize		= SHA256_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
} };

static int __init sha2_ce_mod_init(void)
{
	if (!(elf_hwcap2 & HWCAP2_SHA2))
		return -ENODEV;
	return crypto_register_shashes(algs, ARRAY_SIZE(algs));
}

static void __exit sha2_ce_mod_fini(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}

module_init(sha2_ce_mod_init);
module_exit(sha2_ce_mod_fini);
