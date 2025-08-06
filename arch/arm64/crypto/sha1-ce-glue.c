// SPDX-License-Identifier: GPL-2.0-only
/*
 * sha1-ce-glue.c - SHA-1 secure hash using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2014 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/sha1.h>
#include <crypto/sha1_base.h>
#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

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

asmlinkage int __sha1_ce_transform(struct sha1_ce_state *sst, u8 const *src,
				   int blocks);

static void sha1_ce_transform(struct sha1_state *sst, u8 const *src,
			      int blocks)
{
	while (blocks) {
		int rem;

		kernel_neon_begin();
		rem = __sha1_ce_transform(container_of(sst,
						       struct sha1_ce_state,
						       sst), src, blocks);
		kernel_neon_end();
		src += (blocks - rem) * SHA1_BLOCK_SIZE;
		blocks = rem;
	}
}

const u32 sha1_ce_offsetof_count = offsetof(struct sha1_ce_state, sst.count);
const u32 sha1_ce_offsetof_finalize = offsetof(struct sha1_ce_state, finalize);

static int sha1_ce_update(struct shash_desc *desc, const u8 *data,
			  unsigned int len)
{
	struct sha1_ce_state *sctx = shash_desc_ctx(desc);

	sctx->finalize = 0;
	return sha1_base_do_update_blocks(desc, data, len, sha1_ce_transform);
}

static int sha1_ce_finup(struct shash_desc *desc, const u8 *data,
			 unsigned int len, u8 *out)
{
	struct sha1_ce_state *sctx = shash_desc_ctx(desc);
	bool finalized = false;

	/*
	 * Allow the asm code to perform the finalization if there is no
	 * partial data and the input is a round multiple of the block size.
	 */
	if (len >= SHA1_BLOCK_SIZE) {
		unsigned int remain = len - round_down(len, SHA1_BLOCK_SIZE);

		finalized = !remain;
		sctx->finalize = finalized;
		sha1_base_do_update_blocks(desc, data, len, sha1_ce_transform);
		data += len - remain;
		len = remain;
	}
	if (!finalized) {
		sctx->finalize = 0;
		sha1_base_do_finup(desc, data, len, sha1_ce_transform);
	}
	return sha1_base_finish(desc, out);
}

static struct shash_alg alg = {
	.init			= sha1_base_init,
	.update			= sha1_ce_update,
	.finup			= sha1_ce_finup,
	.descsize		= sizeof(struct sha1_ce_state),
	.statesize		= SHA1_STATE_SIZE,
	.digestsize		= SHA1_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha1",
		.cra_driver_name	= "sha1-ce",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY |
					  CRYPTO_AHASH_ALG_FINUP_MAX,
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
