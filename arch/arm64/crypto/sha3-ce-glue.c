// SPDX-License-Identifier: GPL-2.0
/*
 * sha3-ce-glue.c - core SHA-3 transform using v8.2 Crypto Extensions
 *
 * Copyright (C) 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/internal/hash.h>
#include <crypto/sha3.h>
#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>

MODULE_DESCRIPTION("SHA3 secure hash using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("sha3-224");
MODULE_ALIAS_CRYPTO("sha3-256");
MODULE_ALIAS_CRYPTO("sha3-384");
MODULE_ALIAS_CRYPTO("sha3-512");

asmlinkage int sha3_ce_transform(u64 *st, const u8 *data, int blocks,
				 int md_len);

static int sha3_update(struct shash_desc *desc, const u8 *data,
		       unsigned int len)
{
	struct sha3_state *sctx = shash_desc_ctx(desc);
	struct crypto_shash *tfm = desc->tfm;
	unsigned int bs, ds;
	int blocks;

	ds = crypto_shash_digestsize(tfm);
	bs = crypto_shash_blocksize(tfm);
	blocks = len / bs;
	len -= blocks * bs;
	do {
		int rem;

		kernel_neon_begin();
		rem = sha3_ce_transform(sctx->st, data, blocks, ds);
		kernel_neon_end();
		data += (blocks - rem) * bs;
		blocks = rem;
	} while (blocks);
	return len;
}

static int sha3_finup(struct shash_desc *desc, const u8 *src, unsigned int len,
		      u8 *out)
{
	struct sha3_state *sctx = shash_desc_ctx(desc);
	struct crypto_shash *tfm = desc->tfm;
	__le64 *digest = (__le64 *)out;
	u8 block[SHA3_224_BLOCK_SIZE];
	unsigned int bs, ds;
	int i;

	ds = crypto_shash_digestsize(tfm);
	bs = crypto_shash_blocksize(tfm);
	memcpy(block, src, len);

	block[len++] = 0x06;
	memset(block + len, 0, bs - len);
	block[bs - 1] |= 0x80;

	kernel_neon_begin();
	sha3_ce_transform(sctx->st, block, 1, ds);
	kernel_neon_end();
	memzero_explicit(block , sizeof(block));

	for (i = 0; i < ds / 8; i++)
		put_unaligned_le64(sctx->st[i], digest++);

	if (ds & 4)
		put_unaligned_le32(sctx->st[i], (__le32 *)digest);

	return 0;
}

static struct shash_alg algs[] = { {
	.digestsize		= SHA3_224_DIGEST_SIZE,
	.init			= crypto_sha3_init,
	.update			= sha3_update,
	.finup			= sha3_finup,
	.descsize		= SHA3_STATE_SIZE,
	.base.cra_name		= "sha3-224",
	.base.cra_driver_name	= "sha3-224-ce",
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= SHA3_224_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
	.base.cra_priority	= 200,
}, {
	.digestsize		= SHA3_256_DIGEST_SIZE,
	.init			= crypto_sha3_init,
	.update			= sha3_update,
	.finup			= sha3_finup,
	.descsize		= SHA3_STATE_SIZE,
	.base.cra_name		= "sha3-256",
	.base.cra_driver_name	= "sha3-256-ce",
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= SHA3_256_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
	.base.cra_priority	= 200,
}, {
	.digestsize		= SHA3_384_DIGEST_SIZE,
	.init			= crypto_sha3_init,
	.update			= sha3_update,
	.finup			= sha3_finup,
	.descsize		= SHA3_STATE_SIZE,
	.base.cra_name		= "sha3-384",
	.base.cra_driver_name	= "sha3-384-ce",
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= SHA3_384_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
	.base.cra_priority	= 200,
}, {
	.digestsize		= SHA3_512_DIGEST_SIZE,
	.init			= crypto_sha3_init,
	.update			= sha3_update,
	.finup			= sha3_finup,
	.descsize		= SHA3_STATE_SIZE,
	.base.cra_name		= "sha3-512",
	.base.cra_driver_name	= "sha3-512-ce",
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= SHA3_512_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
	.base.cra_priority	= 200,
} };

static int __init sha3_neon_mod_init(void)
{
	return crypto_register_shashes(algs, ARRAY_SIZE(algs));
}

static void __exit sha3_neon_mod_fini(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}

module_cpu_feature_match(SHA3, sha3_neon_mod_init);
module_exit(sha3_neon_mod_fini);
