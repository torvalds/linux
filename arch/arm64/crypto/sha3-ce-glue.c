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
#include <asm/unaligned.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/sha3.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/module.h>

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
	unsigned int digest_size = crypto_shash_digestsize(desc->tfm);

	if (!crypto_simd_usable())
		return crypto_sha3_update(desc, data, len);

	if ((sctx->partial + len) >= sctx->rsiz) {
		int blocks;

		if (sctx->partial) {
			int p = sctx->rsiz - sctx->partial;

			memcpy(sctx->buf + sctx->partial, data, p);
			kernel_neon_begin();
			sha3_ce_transform(sctx->st, sctx->buf, 1, digest_size);
			kernel_neon_end();

			data += p;
			len -= p;
			sctx->partial = 0;
		}

		blocks = len / sctx->rsiz;
		len %= sctx->rsiz;

		while (blocks) {
			int rem;

			kernel_neon_begin();
			rem = sha3_ce_transform(sctx->st, data, blocks,
						digest_size);
			kernel_neon_end();
			data += (blocks - rem) * sctx->rsiz;
			blocks = rem;
		}
	}

	if (len) {
		memcpy(sctx->buf + sctx->partial, data, len);
		sctx->partial += len;
	}
	return 0;
}

static int sha3_final(struct shash_desc *desc, u8 *out)
{
	struct sha3_state *sctx = shash_desc_ctx(desc);
	unsigned int digest_size = crypto_shash_digestsize(desc->tfm);
	__le64 *digest = (__le64 *)out;
	int i;

	if (!crypto_simd_usable())
		return crypto_sha3_final(desc, out);

	sctx->buf[sctx->partial++] = 0x06;
	memset(sctx->buf + sctx->partial, 0, sctx->rsiz - sctx->partial);
	sctx->buf[sctx->rsiz - 1] |= 0x80;

	kernel_neon_begin();
	sha3_ce_transform(sctx->st, sctx->buf, 1, digest_size);
	kernel_neon_end();

	for (i = 0; i < digest_size / 8; i++)
		put_unaligned_le64(sctx->st[i], digest++);

	if (digest_size & 4)
		put_unaligned_le32(sctx->st[i], (__le32 *)digest);

	memzero_explicit(sctx, sizeof(*sctx));
	return 0;
}

static struct shash_alg algs[] = { {
	.digestsize		= SHA3_224_DIGEST_SIZE,
	.init			= crypto_sha3_init,
	.update			= sha3_update,
	.final			= sha3_final,
	.descsize		= sizeof(struct sha3_state),
	.base.cra_name		= "sha3-224",
	.base.cra_driver_name	= "sha3-224-ce",
	.base.cra_blocksize	= SHA3_224_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
	.base.cra_priority	= 200,
}, {
	.digestsize		= SHA3_256_DIGEST_SIZE,
	.init			= crypto_sha3_init,
	.update			= sha3_update,
	.final			= sha3_final,
	.descsize		= sizeof(struct sha3_state),
	.base.cra_name		= "sha3-256",
	.base.cra_driver_name	= "sha3-256-ce",
	.base.cra_blocksize	= SHA3_256_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
	.base.cra_priority	= 200,
}, {
	.digestsize		= SHA3_384_DIGEST_SIZE,
	.init			= crypto_sha3_init,
	.update			= sha3_update,
	.final			= sha3_final,
	.descsize		= sizeof(struct sha3_state),
	.base.cra_name		= "sha3-384",
	.base.cra_driver_name	= "sha3-384-ce",
	.base.cra_blocksize	= SHA3_384_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
	.base.cra_priority	= 200,
}, {
	.digestsize		= SHA3_512_DIGEST_SIZE,
	.init			= crypto_sha3_init,
	.update			= sha3_update,
	.final			= sha3_final,
	.descsize		= sizeof(struct sha3_state),
	.base.cra_name		= "sha3-512",
	.base.cra_driver_name	= "sha3-512-ce",
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
