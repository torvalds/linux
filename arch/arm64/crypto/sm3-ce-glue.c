// SPDX-License-Identifier: GPL-2.0-only
/*
 * sm3-ce-glue.c - SM3 secure hash using ARMv8.2 Crypto Extensions
 *
 * Copyright (C) 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <asm/simd.h>
#include <asm/unaligned.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/sm3.h>
#include <crypto/sm3_base.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/module.h>

MODULE_DESCRIPTION("SM3 secure hash using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

asmlinkage void sm3_ce_transform(struct sm3_state *sst, u8 const *src,
				 int blocks);

static int sm3_ce_update(struct shash_desc *desc, const u8 *data,
			 unsigned int len)
{
	if (!crypto_simd_usable()) {
		sm3_update(shash_desc_ctx(desc), data, len);
		return 0;
	}

	kernel_neon_begin();
	sm3_base_do_update(desc, data, len, sm3_ce_transform);
	kernel_neon_end();

	return 0;
}

static int sm3_ce_final(struct shash_desc *desc, u8 *out)
{
	if (!crypto_simd_usable()) {
		sm3_final(shash_desc_ctx(desc), out);
		return 0;
	}

	kernel_neon_begin();
	sm3_base_do_finalize(desc, sm3_ce_transform);
	kernel_neon_end();

	return sm3_base_finish(desc, out);
}

static int sm3_ce_finup(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out)
{
	if (!crypto_simd_usable()) {
		struct sm3_state *sctx = shash_desc_ctx(desc);

		if (len)
			sm3_update(sctx, data, len);
		sm3_final(sctx, out);
		return 0;
	}

	kernel_neon_begin();
	if (len)
		sm3_base_do_update(desc, data, len, sm3_ce_transform);
	sm3_base_do_finalize(desc, sm3_ce_transform);
	kernel_neon_end();

	return sm3_base_finish(desc, out);
}

static struct shash_alg sm3_alg = {
	.digestsize		= SM3_DIGEST_SIZE,
	.init			= sm3_base_init,
	.update			= sm3_ce_update,
	.final			= sm3_ce_final,
	.finup			= sm3_ce_finup,
	.descsize		= sizeof(struct sm3_state),
	.base.cra_name		= "sm3",
	.base.cra_driver_name	= "sm3-ce",
	.base.cra_blocksize	= SM3_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
	.base.cra_priority	= 400,
};

static int __init sm3_ce_mod_init(void)
{
	return crypto_register_shash(&sm3_alg);
}

static void __exit sm3_ce_mod_fini(void)
{
	crypto_unregister_shash(&sm3_alg);
}

module_cpu_feature_match(SM3, sm3_ce_mod_init);
module_exit(sm3_ce_mod_fini);
