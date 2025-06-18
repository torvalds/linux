// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue code for SHA-1 implementation for SPE instructions (PPC)
 *
 * Based on generic implementation.
 *
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 */

#include <asm/switch_to.h>
#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <crypto/sha1_base.h>
#include <linux/kernel.h>
#include <linux/preempt.h>
#include <linux/module.h>

/*
 * MAX_BYTES defines the number of bytes that are allowed to be processed
 * between preempt_disable() and preempt_enable(). SHA1 takes ~1000
 * operations per 64 bytes. e500 cores can issue two arithmetic instructions
 * per clock cycle using one 32/64 bit unit (SU1) and one 32 bit unit (SU2).
 * Thus 2KB of input data will need an estimated maximum of 18,000 cycles.
 * Headroom for cache misses included. Even with the low end model clocked
 * at 667 MHz this equals to a critical time window of less than 27us.
 *
 */
#define MAX_BYTES 2048

asmlinkage void ppc_spe_sha1_transform(u32 *state, const u8 *src, u32 blocks);

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

static void ppc_spe_sha1_block(struct sha1_state *sctx, const u8 *src,
			       int blocks)
{
	do {
		int unit = min(blocks, MAX_BYTES / SHA1_BLOCK_SIZE);

		spe_begin();
		ppc_spe_sha1_transform(sctx->state, src, unit);
		spe_end();

		src += unit * SHA1_BLOCK_SIZE;
		blocks -= unit;
	} while (blocks);
}

static int ppc_spe_sha1_update(struct shash_desc *desc, const u8 *data,
			unsigned int len)
{
	return sha1_base_do_update_blocks(desc, data, len, ppc_spe_sha1_block);
}

static int ppc_spe_sha1_finup(struct shash_desc *desc, const u8 *src,
			      unsigned int len, u8 *out)
{
	sha1_base_do_finup(desc, src, len, ppc_spe_sha1_block);
	return sha1_base_finish(desc, out);
}

static struct shash_alg alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	ppc_spe_sha1_update,
	.finup		=	ppc_spe_sha1_finup,
	.descsize	=	SHA1_STATE_SIZE,
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name=	"sha1-ppc-spe",
		.cra_priority	=	300,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int __init ppc_spe_sha1_mod_init(void)
{
	return crypto_register_shash(&alg);
}

static void __exit ppc_spe_sha1_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(ppc_spe_sha1_mod_init);
module_exit(ppc_spe_sha1_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm, SPE optimized");

MODULE_ALIAS_CRYPTO("sha1");
MODULE_ALIAS_CRYPTO("sha1-ppc-spe");
