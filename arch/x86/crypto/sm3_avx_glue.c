/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM3 Secure Hash Algorithm, AVX assembler accelerated.
 * specified in: https://datatracker.ietf.org/doc/html/draft-sca-cfrg-sm3-02
 *
 * Copyright (C) 2021 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/sm3.h>
#include <crypto/sm3_base.h>
#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/module.h>

asmlinkage void sm3_transform_avx(struct sm3_state *state,
			const u8 *data, int nblocks);

static int sm3_avx_update(struct shash_desc *desc, const u8 *data,
			 unsigned int len)
{
	int remain;

	/*
	 * Make sure struct sm3_state begins directly with the SM3
	 * 256-bit internal state, as this is what the asm functions expect.
	 */
	BUILD_BUG_ON(offsetof(struct sm3_state, state) != 0);

	kernel_fpu_begin();
	remain = sm3_base_do_update_blocks(desc, data, len, sm3_transform_avx);
	kernel_fpu_end();
	return remain;
}

static int sm3_avx_finup(struct shash_desc *desc, const u8 *data,
		      unsigned int len, u8 *out)
{
	kernel_fpu_begin();
	sm3_base_do_finup(desc, data, len, sm3_transform_avx);
	kernel_fpu_end();
	return sm3_base_finish(desc, out);
}

static struct shash_alg sm3_avx_alg = {
	.digestsize	=	SM3_DIGEST_SIZE,
	.init		=	sm3_base_init,
	.update		=	sm3_avx_update,
	.finup		=	sm3_avx_finup,
	.descsize	=	SM3_STATE_SIZE,
	.base		=	{
		.cra_name	=	"sm3",
		.cra_driver_name =	"sm3-avx",
		.cra_priority	=	300,
		.cra_flags	 =	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize	=	SM3_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int __init sm3_avx_mod_init(void)
{
	const char *feature_name;

	if (!boot_cpu_has(X86_FEATURE_AVX)) {
		pr_info("AVX instruction are not detected.\n");
		return -ENODEV;
	}

	if (!boot_cpu_has(X86_FEATURE_BMI2)) {
		pr_info("BMI2 instruction are not detected.\n");
		return -ENODEV;
	}

	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				&feature_name)) {
		pr_info("CPU feature '%s' is not supported.\n", feature_name);
		return -ENODEV;
	}

	return crypto_register_shash(&sm3_avx_alg);
}

static void __exit sm3_avx_mod_exit(void)
{
	crypto_unregister_shash(&sm3_avx_alg);
}

module_init(sm3_avx_mod_init);
module_exit(sm3_avx_mod_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Tianjia Zhang <tianjia.zhang@linux.alibaba.com>");
MODULE_DESCRIPTION("SM3 Secure Hash Algorithm, AVX assembler accelerated");
MODULE_ALIAS_CRYPTO("sm3");
MODULE_ALIAS_CRYPTO("sm3-avx");
