// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue code for SHA-256 implementation for SPE instructions (PPC)
 *
 * Based on generic implementation. The assembler module takes care 
 * about the SPE registers so it can run from interrupt context.
 *
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 */

#include <asm/switch_to.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha256_base.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/preempt.h>

/*
 * MAX_BYTES defines the number of bytes that are allowed to be processed
 * between preempt_disable() and preempt_enable(). SHA256 takes ~2,000
 * operations per 64 bytes. e500 cores can issue two arithmetic instructions
 * per clock cycle using one 32/64 bit unit (SU1) and one 32 bit unit (SU2).
 * Thus 1KB of input data will need an estimated maximum of 18,000 cycles.
 * Headroom for cache misses included. Even with the low end model clocked
 * at 667 MHz this equals to a critical time window of less than 27us.
 *
 */
#define MAX_BYTES 1024

extern void ppc_spe_sha256_transform(u32 *state, const u8 *src, u32 blocks);

static void spe_begin(void)
{
	/* We just start SPE operations and will save SPE registers later. */
	preempt_disable();
	enable_kernel_spe();
}

static void spe_end(void)
{
	disable_kernel_spe();
	/* reenable preemption */
	preempt_enable();
}

static void ppc_spe_sha256_block(struct crypto_sha256_state *sctx,
				 const u8 *src, int blocks)
{
	do {
		/* cut input data into smaller blocks */
		int unit = min(blocks, MAX_BYTES / SHA256_BLOCK_SIZE);

		spe_begin();
		ppc_spe_sha256_transform(sctx->state, src, unit);
		spe_end();

		src += unit * SHA256_BLOCK_SIZE;
		blocks -= unit;
	} while (blocks);
}

static int ppc_spe_sha256_update(struct shash_desc *desc, const u8 *data,
			unsigned int len)
{
	return sha256_base_do_update_blocks(desc, data, len,
					    ppc_spe_sha256_block);
}

static int ppc_spe_sha256_finup(struct shash_desc *desc, const u8 *src,
				unsigned int len, u8 *out)
{
	sha256_base_do_finup(desc, src, len, ppc_spe_sha256_block);
	return sha256_base_finish(desc, out);
}

static struct shash_alg algs[2] = { {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_base_init,
	.update		=	ppc_spe_sha256_update,
	.finup		=	ppc_spe_sha256_finup,
	.descsize	=	sizeof(struct crypto_sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name=	"sha256-ppc-spe",
		.cra_priority	=	300,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	sha224_base_init,
	.update		=	ppc_spe_sha256_update,
	.finup		=	ppc_spe_sha256_finup,
	.descsize	=	sizeof(struct crypto_sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name=	"sha224-ppc-spe",
		.cra_priority	=	300,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

static int __init ppc_spe_sha256_mod_init(void)
{
	return crypto_register_shashes(algs, ARRAY_SIZE(algs));
}

static void __exit ppc_spe_sha256_mod_fini(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}

module_init(ppc_spe_sha256_mod_init);
module_exit(ppc_spe_sha256_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-224 and SHA-256 Secure Hash Algorithm, SPE optimized");

MODULE_ALIAS_CRYPTO("sha224");
MODULE_ALIAS_CRYPTO("sha224-ppc-spe");
MODULE_ALIAS_CRYPTO("sha256");
MODULE_ALIAS_CRYPTO("sha256-ppc-spe");
