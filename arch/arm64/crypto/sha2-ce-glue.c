// SPDX-License-Identifier: GPL-2.0-only
/*
 * sha2-ce-glue.c - SHA-224/SHA-256 using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2014 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <asm/simd.h>
#include <asm/unaligned.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/sha.h>
#include <crypto/sha256_base.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/module.h>

MODULE_DESCRIPTION("SHA-224/SHA-256 secure hash using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

struct sha256_ce_state {
	struct sha256_state	sst;
	u32			finalize;
};

extern const u32 sha256_ce_offsetof_count;
extern const u32 sha256_ce_offsetof_finalize;

asmlinkage void sha2_ce_transform(struct sha256_ce_state *sst, u8 const *src,
				  int blocks);

static void __sha2_ce_transform(struct sha256_state *sst, u8 const *src,
				int blocks)
{
	sha2_ce_transform(container_of(sst, struct sha256_ce_state, sst), src,
			  blocks);
}

const u32 sha256_ce_offsetof_count = offsetof(struct sha256_ce_state,
					      sst.count);
const u32 sha256_ce_offsetof_finalize = offsetof(struct sha256_ce_state,
						 finalize);

asmlinkage void sha256_block_data_order(u32 *digest, u8 const *src, int blocks);

static void __sha256_block_data_order(struct sha256_state *sst, u8 const *src,
				      int blocks)
{
	sha256_block_data_order(sst->state, src, blocks);
}

static int sha256_ce_update(struct shash_desc *desc, const u8 *data,
			    unsigned int len)
{
	struct sha256_ce_state *sctx = shash_desc_ctx(desc);

	if (!crypto_simd_usable())
		return sha256_base_do_update(desc, data, len,
				__sha256_block_data_order);

	sctx->finalize = 0;
	kernel_neon_begin();
	sha256_base_do_update(desc, data, len, __sha2_ce_transform);
	kernel_neon_end();

	return 0;
}

static int sha256_ce_finup(struct shash_desc *desc, const u8 *data,
			   unsigned int len, u8 *out)
{
	struct sha256_ce_state *sctx = shash_desc_ctx(desc);
	bool finalize = !sctx->sst.count && !(len % SHA256_BLOCK_SIZE) && len;

	if (!crypto_simd_usable()) {
		if (len)
			sha256_base_do_update(desc, data, len,
				__sha256_block_data_order);
		sha256_base_do_finalize(desc, __sha256_block_data_order);
		return sha256_base_finish(desc, out);
	}

	/*
	 * Allow the asm code to perform the finalization if there is no
	 * partial data and the input is a round multiple of the block size.
	 */
	sctx->finalize = finalize;

	kernel_neon_begin();
	sha256_base_do_update(desc, data, len, __sha2_ce_transform);
	if (!finalize)
		sha256_base_do_finalize(desc, __sha2_ce_transform);
	kernel_neon_end();
	return sha256_base_finish(desc, out);
}

static int sha256_ce_final(struct shash_desc *desc, u8 *out)
{
	struct sha256_ce_state *sctx = shash_desc_ctx(desc);

	if (!crypto_simd_usable()) {
		sha256_base_do_finalize(desc, __sha256_block_data_order);
		return sha256_base_finish(desc, out);
	}

	sctx->finalize = 0;
	kernel_neon_begin();
	sha256_base_do_finalize(desc, __sha2_ce_transform);
	kernel_neon_end();
	return sha256_base_finish(desc, out);
}

static int sha256_ce_export(struct shash_desc *desc, void *out)
{
	struct sha256_ce_state *sctx = shash_desc_ctx(desc);

	memcpy(out, &sctx->sst, sizeof(struct sha256_state));
	return 0;
}

static int sha256_ce_import(struct shash_desc *desc, const void *in)
{
	struct sha256_ce_state *sctx = shash_desc_ctx(desc);

	memcpy(&sctx->sst, in, sizeof(struct sha256_state));
	sctx->finalize = 0;
	return 0;
}

static struct shash_alg algs[] = { {
	.init			= sha224_base_init,
	.update			= sha256_ce_update,
	.final			= sha256_ce_final,
	.finup			= sha256_ce_finup,
	.export			= sha256_ce_export,
	.import			= sha256_ce_import,
	.descsize		= sizeof(struct sha256_ce_state),
	.statesize		= sizeof(struct sha256_state),
	.digestsize		= SHA224_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha224",
		.cra_driver_name	= "sha224-ce",
		.cra_priority		= 200,
		.cra_blocksize		= SHA256_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
}, {
	.init			= sha256_base_init,
	.update			= sha256_ce_update,
	.final			= sha256_ce_final,
	.finup			= sha256_ce_finup,
	.export			= sha256_ce_export,
	.import			= sha256_ce_import,
	.descsize		= sizeof(struct sha256_ce_state),
	.statesize		= sizeof(struct sha256_state),
	.digestsize		= SHA256_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha256",
		.cra_driver_name	= "sha256-ce",
		.cra_priority		= 200,
		.cra_blocksize		= SHA256_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
} };

static int __init sha2_ce_mod_init(void)
{
	return crypto_register_shashes(algs, ARRAY_SIZE(algs));
}

static void __exit sha2_ce_mod_fini(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}

module_cpu_feature_match(SHA2, sha2_ce_mod_init);
module_exit(sha2_ce_mod_fini);
