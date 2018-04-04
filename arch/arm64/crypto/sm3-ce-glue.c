/*
 * sm3-ce-glue.c - SM3 secure hash using ARMv8.2 Crypto Extensions
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
	if (!may_use_simd())
		return crypto_sm3_update(desc, data, len);

	kernel_neon_begin();
	sm3_base_do_update(desc, data, len, sm3_ce_transform);
	kernel_neon_end();

	return 0;
}

static int sm3_ce_final(struct shash_desc *desc, u8 *out)
{
	if (!may_use_simd())
		return crypto_sm3_finup(desc, NULL, 0, out);

	kernel_neon_begin();
	sm3_base_do_finalize(desc, sm3_ce_transform);
	kernel_neon_end();

	return sm3_base_finish(desc, out);
}

static int sm3_ce_finup(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out)
{
	if (!may_use_simd())
		return crypto_sm3_finup(desc, data, len, out);

	kernel_neon_begin();
	sm3_base_do_update(desc, data, len, sm3_ce_transform);
	kernel_neon_end();

	return sm3_ce_final(desc, out);
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
	.base.cra_flags		= CRYPTO_ALG_TYPE_SHASH,
	.base.cra_blocksize	= SM3_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
	.base.cra_priority	= 200,
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
