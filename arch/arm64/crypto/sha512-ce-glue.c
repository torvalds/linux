/* SPDX-License-Identifier: GPL-2.0 */
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
#include <asm/simd.h>
#include <asm/unaligned.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/sha2.h>
#include <crypto/sha512_base.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/module.h>

MODULE_DESCRIPTION("SHA-384/SHA-512 secure hash using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

asmlinkage void sha512_ce_transform(struct sha512_state *sst, u8 const *src,
				    int blocks);

asmlinkage void sha512_block_data_order(u64 *digest, u8 const *src, int blocks);

static void __sha512_block_data_order(struct sha512_state *sst, u8 const *src,
				      int blocks)
{
	sha512_block_data_order(sst->state, src, blocks);
}

static int sha512_ce_update(struct shash_desc *desc, const u8 *data,
			    unsigned int len)
{
	if (!crypto_simd_usable())
		return sha512_base_do_update(desc, data, len,
					     __sha512_block_data_order);

	kernel_neon_begin();
	sha512_base_do_update(desc, data, len, sha512_ce_transform);
	kernel_neon_end();

	return 0;
}

static int sha512_ce_finup(struct shash_desc *desc, const u8 *data,
			   unsigned int len, u8 *out)
{
	if (!crypto_simd_usable()) {
		if (len)
			sha512_base_do_update(desc, data, len,
					      __sha512_block_data_order);
		sha512_base_do_finalize(desc, __sha512_block_data_order);
		return sha512_base_finish(desc, out);
	}

	kernel_neon_begin();
	sha512_base_do_update(desc, data, len, sha512_ce_transform);
	sha512_base_do_finalize(desc, sha512_ce_transform);
	kernel_neon_end();
	return sha512_base_finish(desc, out);
}

static int sha512_ce_final(struct shash_desc *desc, u8 *out)
{
	if (!crypto_simd_usable()) {
		sha512_base_do_finalize(desc, __sha512_block_data_order);
		return sha512_base_finish(desc, out);
	}

	kernel_neon_begin();
	sha512_base_do_finalize(desc, sha512_ce_transform);
	kernel_neon_end();
	return sha512_base_finish(desc, out);
}

static struct shash_alg algs[] = { {
	.init			= sha384_base_init,
	.update			= sha512_ce_update,
	.final			= sha512_ce_final,
	.finup			= sha512_ce_finup,
	.descsize		= sizeof(struct sha512_state),
	.digestsize		= SHA384_DIGEST_SIZE,
	.base.cra_name		= "sha384",
	.base.cra_driver_name	= "sha384-ce",
	.base.cra_priority	= 200,
	.base.cra_blocksize	= SHA512_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}, {
	.init			= sha512_base_init,
	.update			= sha512_ce_update,
	.final			= sha512_ce_final,
	.finup			= sha512_ce_finup,
	.descsize		= sizeof(struct sha512_state),
	.digestsize		= SHA512_DIGEST_SIZE,
	.base.cra_name		= "sha512",
	.base.cra_driver_name	= "sha512-ce",
	.base.cra_priority	= 200,
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
