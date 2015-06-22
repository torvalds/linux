/*
 * Cryptographic API.
 *
 * Glue code for the SHA1 Secure Hash Algorithm assembler implementation using
 * Supplemental SSE3 instructions.
 *
 * This file is based on sha1_generic.c
 *
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 * Copyright (c) Mathias Krause <minipli@googlemail.com>
 * Copyright (c) Chandramouli Narayanan <mouli@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <crypto/sha.h>
#include <crypto/sha1_base.h>
#include <asm/i387.h>
#include <asm/xcr.h>
#include <asm/xsave.h>


asmlinkage void sha1_transform_ssse3(u32 *digest, const char *data,
				     unsigned int rounds);
#ifdef CONFIG_AS_AVX
asmlinkage void sha1_transform_avx(u32 *digest, const char *data,
				   unsigned int rounds);
#endif
#ifdef CONFIG_AS_AVX2
#define SHA1_AVX2_BLOCK_OPTSIZE	4	/* optimal 4*64 bytes of SHA1 blocks */

asmlinkage void sha1_transform_avx2(u32 *digest, const char *data,
				    unsigned int rounds);
#endif

static void (*sha1_transform_asm)(u32 *, const char *, unsigned int);

static int sha1_ssse3_update(struct shash_desc *desc, const u8 *data,
			     unsigned int len)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);

	if (!irq_fpu_usable() ||
	    (sctx->count % SHA1_BLOCK_SIZE) + len < SHA1_BLOCK_SIZE)
		return crypto_sha1_update(desc, data, len);

	/* make sure casting to sha1_block_fn() is safe */
	BUILD_BUG_ON(offsetof(struct sha1_state, state) != 0);

	kernel_fpu_begin();
	sha1_base_do_update(desc, data, len,
			    (sha1_block_fn *)sha1_transform_asm);
	kernel_fpu_end();

	return 0;
}

static int sha1_ssse3_finup(struct shash_desc *desc, const u8 *data,
			      unsigned int len, u8 *out)
{
	if (!irq_fpu_usable())
		return crypto_sha1_finup(desc, data, len, out);

	kernel_fpu_begin();
	if (len)
		sha1_base_do_update(desc, data, len,
				    (sha1_block_fn *)sha1_transform_asm);
	sha1_base_do_finalize(desc, (sha1_block_fn *)sha1_transform_asm);
	kernel_fpu_end();

	return sha1_base_finish(desc, out);
}

/* Add padding and return the message digest. */
static int sha1_ssse3_final(struct shash_desc *desc, u8 *out)
{
	return sha1_ssse3_finup(desc, NULL, 0, out);
}

#ifdef CONFIG_AS_AVX2
static void sha1_apply_transform_avx2(u32 *digest, const char *data,
				unsigned int rounds)
{
	/* Select the optimal transform based on data block size */
	if (rounds >= SHA1_AVX2_BLOCK_OPTSIZE)
		sha1_transform_avx2(digest, data, rounds);
	else
		sha1_transform_avx(digest, data, rounds);
}
#endif

static struct shash_alg alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	sha1_ssse3_update,
	.final		=	sha1_ssse3_final,
	.finup		=	sha1_ssse3_finup,
	.descsize	=	sizeof(struct sha1_state),
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name=	"sha1-ssse3",
		.cra_priority	=	150,
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

#ifdef CONFIG_AS_AVX
static bool __init avx_usable(void)
{
	u64 xcr0;

	if (!cpu_has_avx || !cpu_has_osxsave)
		return false;

	xcr0 = xgetbv(XCR_XFEATURE_ENABLED_MASK);
	if ((xcr0 & (XSTATE_SSE | XSTATE_YMM)) != (XSTATE_SSE | XSTATE_YMM)) {
		pr_info("AVX detected but unusable.\n");

		return false;
	}

	return true;
}

#ifdef CONFIG_AS_AVX2
static bool __init avx2_usable(void)
{
	if (avx_usable() && cpu_has_avx2 && boot_cpu_has(X86_FEATURE_BMI1) &&
	    boot_cpu_has(X86_FEATURE_BMI2))
		return true;

	return false;
}
#endif
#endif

static int __init sha1_ssse3_mod_init(void)
{
	char *algo_name;

	/* test for SSSE3 first */
	if (cpu_has_ssse3) {
		sha1_transform_asm = sha1_transform_ssse3;
		algo_name = "SSSE3";
	}

#ifdef CONFIG_AS_AVX
	/* allow AVX to override SSSE3, it's a little faster */
	if (avx_usable()) {
		sha1_transform_asm = sha1_transform_avx;
		algo_name = "AVX";
#ifdef CONFIG_AS_AVX2
		/* allow AVX2 to override AVX, it's a little faster */
		if (avx2_usable()) {
			sha1_transform_asm = sha1_apply_transform_avx2;
			algo_name = "AVX2";
		}
#endif
	}
#endif

	if (sha1_transform_asm) {
		pr_info("Using %s optimized SHA-1 implementation\n", algo_name);
		return crypto_register_shash(&alg);
	}
	pr_info("Neither AVX nor AVX2 nor SSSE3 is available/usable.\n");

	return -ENODEV;
}

static void __exit sha1_ssse3_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(sha1_ssse3_mod_init);
module_exit(sha1_ssse3_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm, Supplemental SSE3 accelerated");

MODULE_ALIAS_CRYPTO("sha1");
