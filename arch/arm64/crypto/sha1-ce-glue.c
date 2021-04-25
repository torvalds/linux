// SPDX-License-Identifier: GPL-2.0-only
/*
 * sha1-ce-glue.c - SHA-1 secure hash using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2014 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <asm/simd.h>
#include <asm/unaligned.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/sha.h>
#include <crypto/sha1_base.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/module.h>

MODULE_DESCRIPTION("SHA1 secure hash using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("sha1");

struct sha1_ce_state {
	struct sha1_state	sst;
	u32			finalize;
};

extern const u32 sha1_ce_offsetof_count;
extern const u32 sha1_ce_offsetof_finalize;

asmlinkage void sha1_ce_transform(struct sha1_ce_state *sst, u8 const *src,
				  int blocks);

static void __sha1_ce_transform(struct sha1_state *sst, u8 const *src,
				int blocks)
{
	sha1_ce_transform(container_of(sst, struct sha1_ce_state, sst), src,
			  blocks);
}

const u32 sha1_ce_offsetof_count = offsetof(struct sha1_ce_state, sst.count);
const u32 sha1_ce_offsetof_finalize = offsetof(struct sha1_ce_state, finalize);

static int sha1_ce_update(struct shash_desc *desc, const u8 *data,
			  unsigned int len)
{
	struct sha1_ce_state *sctx = shash_desc_ctx(desc);

	if (!crypto_simd_usable())
		return crypto_sha1_update(desc, data, len);

	sctx->finalize = 0;
	kernel_neon_begin();
	sha1_base_do_update(desc, data, len, __sha1_ce_transform);
	kernel_neon_end();

	return 0;
}

static int sha1_ce_finup(struct shash_desc *desc, const u8 *data,
			 unsigned int len, u8 *out)
{
	struct sha1_ce_state *sctx = shash_desc_ctx(desc);
	bool finalize = !sctx->sst.count && !(len % SHA1_BLOCK_SIZE) && len;

	if (!crypto_simd_usable())
		return crypto_sha1_finup(desc, data, len, out);

	/*
	 * Allow the asm code to perform the finalization if there is no
	 * partial data and the input is a round multiple of the block size.
	 */
	sctx->finalize = finalize;

	kernel_neon_begin();
	sha1_base_do_update(desc, data, len, __sha1_ce_transform);
	if (!finalize)
		sha1_base_do_finalize(desc, __sha1_ce_transform);
	kernel_neon_end();
	return sha1_base_finish(desc, out);
}

static int sha1_ce_final(struct shash_desc *desc, u8 *out)
{
	struct sha1_ce_state *sctx = shash_desc_ctx(desc);

	if (!crypto_simd_usable())
		return crypto_sha1_finup(desc, NULL, 0, out);

	sctx->finalize = 0;
	kernel_neon_begin();
	sha1_base_do_finalize(desc, __sha1_ce_transform);
	kernel_neon_end();
	return sha1_base_finish(desc, out);
}

static int sha1_ce_export(struct shash_desc *desc, void *out)
{
	struct sha1_ce_state *sctx = shash_desc_ctx(desc);

	memcpy(out, &sctx->sst, sizeof(struct sha1_state));
	return 0;
}

static int sha1_ce_import(struct shash_desc *desc, const void *in)
{
	struct sha1_ce_state *sctx = shash_desc_ctx(desc);

	memcpy(&sctx->sst, in, sizeof(struct sha1_state));
	sctx->finalize = 0;
	return 0;
}

static struct shash_alg alg = {
	.init			= sha1_base_init,
	.update			= sha1_ce_update,
	.final			= sha1_ce_final,
	.finup			= sha1_ce_finup,
	.import			= sha1_ce_import,
	.export			= sha1_ce_export,
	.descsize		= sizeof(struct sha1_ce_state),
	.statesize		= sizeof(struct sha1_state),
	.digestsize		= SHA1_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha1",
		.cra_driver_name	= "sha1-ce",
		.cra_priority		= 200,
		.cra_blocksize		= SHA1_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
};

static int __init sha1_ce_mod_init(void)
{
	return crypto_register_shash(&alg);
}

static void __exit sha1_ce_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_cpu_feature_match(SHA1, sha1_ce_mod_init);
module_exit(sha1_ce_mod_fini);
