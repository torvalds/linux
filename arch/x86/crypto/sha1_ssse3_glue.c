// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <crypto/sha.h>
#include <crypto/sha1_base.h>
#include <asm/simd.h>

static int sha1_update(struct shash_desc *desc, const u8 *data,
			     unsigned int len, sha1_block_fn *sha1_xform)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);

	if (!crypto_simd_usable() ||
	    (sctx->count % SHA1_BLOCK_SIZE) + len < SHA1_BLOCK_SIZE)
		return crypto_sha1_update(desc, data, len);

	/*
	 * Make sure struct sha1_state begins directly with the SHA1
	 * 160-bit internal state, as this is what the asm functions expect.
	 */
	BUILD_BUG_ON(offsetof(struct sha1_state, state) != 0);

	kernel_fpu_begin();
	sha1_base_do_update(desc, data, len, sha1_xform);
	kernel_fpu_end();

	return 0;
}

static int sha1_finup(struct shash_desc *desc, const u8 *data,
		      unsigned int len, u8 *out, sha1_block_fn *sha1_xform)
{
	if (!crypto_simd_usable())
		return crypto_sha1_finup(desc, data, len, out);

	kernel_fpu_begin();
	if (len)
		sha1_base_do_update(desc, data, len, sha1_xform);
	sha1_base_do_finalize(desc, sha1_xform);
	kernel_fpu_end();

	return sha1_base_finish(desc, out);
}

asmlinkage void sha1_transform_ssse3(struct sha1_state *state,
				     const u8 *data, int blocks);

static int sha1_ssse3_update(struct shash_desc *desc, const u8 *data,
			     unsigned int len)
{
	return sha1_update(desc, data, len, sha1_transform_ssse3);
}

static int sha1_ssse3_finup(struct shash_desc *desc, const u8 *data,
			      unsigned int len, u8 *out)
{
	return sha1_finup(desc, data, len, out, sha1_transform_ssse3);
}

/* Add padding and return the message digest. */
static int sha1_ssse3_final(struct shash_desc *desc, u8 *out)
{
	return sha1_ssse3_finup(desc, NULL, 0, out);
}

static struct shash_alg sha1_ssse3_alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	sha1_ssse3_update,
	.final		=	sha1_ssse3_final,
	.finup		=	sha1_ssse3_finup,
	.descsize	=	sizeof(struct sha1_state),
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name =	"sha1-ssse3",
		.cra_priority	=	150,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int register_sha1_ssse3(void)
{
	if (boot_cpu_has(X86_FEATURE_SSSE3))
		return crypto_register_shash(&sha1_ssse3_alg);
	return 0;
}

static void unregister_sha1_ssse3(void)
{
	if (boot_cpu_has(X86_FEATURE_SSSE3))
		crypto_unregister_shash(&sha1_ssse3_alg);
}

#ifdef CONFIG_AS_AVX
asmlinkage void sha1_transform_avx(struct sha1_state *state,
				   const u8 *data, int blocks);

static int sha1_avx_update(struct shash_desc *desc, const u8 *data,
			     unsigned int len)
{
	return sha1_update(desc, data, len, sha1_transform_avx);
}

static int sha1_avx_finup(struct shash_desc *desc, const u8 *data,
			      unsigned int len, u8 *out)
{
	return sha1_finup(desc, data, len, out, sha1_transform_avx);
}

static int sha1_avx_final(struct shash_desc *desc, u8 *out)
{
	return sha1_avx_finup(desc, NULL, 0, out);
}

static struct shash_alg sha1_avx_alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	sha1_avx_update,
	.final		=	sha1_avx_final,
	.finup		=	sha1_avx_finup,
	.descsize	=	sizeof(struct sha1_state),
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name =	"sha1-avx",
		.cra_priority	=	160,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static bool avx_usable(void)
{
	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL)) {
		if (boot_cpu_has(X86_FEATURE_AVX))
			pr_info("AVX detected but unusable.\n");
		return false;
	}

	return true;
}

static int register_sha1_avx(void)
{
	if (avx_usable())
		return crypto_register_shash(&sha1_avx_alg);
	return 0;
}

static void unregister_sha1_avx(void)
{
	if (avx_usable())
		crypto_unregister_shash(&sha1_avx_alg);
}

#else  /* CONFIG_AS_AVX */
static inline int register_sha1_avx(void) { return 0; }
static inline void unregister_sha1_avx(void) { }
#endif /* CONFIG_AS_AVX */


#if defined(CONFIG_AS_AVX2) && (CONFIG_AS_AVX)
#define SHA1_AVX2_BLOCK_OPTSIZE	4	/* optimal 4*64 bytes of SHA1 blocks */

asmlinkage void sha1_transform_avx2(struct sha1_state *state,
				    const u8 *data, int blocks);

static bool avx2_usable(void)
{
	if (avx_usable() && boot_cpu_has(X86_FEATURE_AVX2)
		&& boot_cpu_has(X86_FEATURE_BMI1)
		&& boot_cpu_has(X86_FEATURE_BMI2))
		return true;

	return false;
}

static void sha1_apply_transform_avx2(struct sha1_state *state,
				      const u8 *data, int blocks)
{
	/* Select the optimal transform based on data block size */
	if (blocks >= SHA1_AVX2_BLOCK_OPTSIZE)
		sha1_transform_avx2(state, data, blocks);
	else
		sha1_transform_avx(state, data, blocks);
}

static int sha1_avx2_update(struct shash_desc *desc, const u8 *data,
			     unsigned int len)
{
	return sha1_update(desc, data, len, sha1_apply_transform_avx2);
}

static int sha1_avx2_finup(struct shash_desc *desc, const u8 *data,
			      unsigned int len, u8 *out)
{
	return sha1_finup(desc, data, len, out, sha1_apply_transform_avx2);
}

static int sha1_avx2_final(struct shash_desc *desc, u8 *out)
{
	return sha1_avx2_finup(desc, NULL, 0, out);
}

static struct shash_alg sha1_avx2_alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	sha1_avx2_update,
	.final		=	sha1_avx2_final,
	.finup		=	sha1_avx2_finup,
	.descsize	=	sizeof(struct sha1_state),
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name =	"sha1-avx2",
		.cra_priority	=	170,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int register_sha1_avx2(void)
{
	if (avx2_usable())
		return crypto_register_shash(&sha1_avx2_alg);
	return 0;
}

static void unregister_sha1_avx2(void)
{
	if (avx2_usable())
		crypto_unregister_shash(&sha1_avx2_alg);
}

#else
static inline int register_sha1_avx2(void) { return 0; }
static inline void unregister_sha1_avx2(void) { }
#endif

#ifdef CONFIG_AS_SHA1_NI
asmlinkage void sha1_ni_transform(struct sha1_state *digest, const u8 *data,
				  int rounds);

static int sha1_ni_update(struct shash_desc *desc, const u8 *data,
			     unsigned int len)
{
	return sha1_update(desc, data, len, sha1_ni_transform);
}

static int sha1_ni_finup(struct shash_desc *desc, const u8 *data,
			      unsigned int len, u8 *out)
{
	return sha1_finup(desc, data, len, out, sha1_ni_transform);
}

static int sha1_ni_final(struct shash_desc *desc, u8 *out)
{
	return sha1_ni_finup(desc, NULL, 0, out);
}

static struct shash_alg sha1_ni_alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	sha1_ni_update,
	.final		=	sha1_ni_final,
	.finup		=	sha1_ni_finup,
	.descsize	=	sizeof(struct sha1_state),
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name =	"sha1-ni",
		.cra_priority	=	250,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int register_sha1_ni(void)
{
	if (boot_cpu_has(X86_FEATURE_SHA_NI))
		return crypto_register_shash(&sha1_ni_alg);
	return 0;
}

static void unregister_sha1_ni(void)
{
	if (boot_cpu_has(X86_FEATURE_SHA_NI))
		crypto_unregister_shash(&sha1_ni_alg);
}

#else
static inline int register_sha1_ni(void) { return 0; }
static inline void unregister_sha1_ni(void) { }
#endif

static int __init sha1_ssse3_mod_init(void)
{
	if (register_sha1_ssse3())
		goto fail;

	if (register_sha1_avx()) {
		unregister_sha1_ssse3();
		goto fail;
	}

	if (register_sha1_avx2()) {
		unregister_sha1_avx();
		unregister_sha1_ssse3();
		goto fail;
	}

	if (register_sha1_ni()) {
		unregister_sha1_avx2();
		unregister_sha1_avx();
		unregister_sha1_ssse3();
		goto fail;
	}

	return 0;
fail:
	return -ENODEV;
}

static void __exit sha1_ssse3_mod_fini(void)
{
	unregister_sha1_ni();
	unregister_sha1_avx2();
	unregister_sha1_avx();
	unregister_sha1_ssse3();
}

module_init(sha1_ssse3_mod_init);
module_exit(sha1_ssse3_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm, Supplemental SSE3 accelerated");

MODULE_ALIAS_CRYPTO("sha1");
MODULE_ALIAS_CRYPTO("sha1-ssse3");
MODULE_ALIAS_CRYPTO("sha1-avx");
MODULE_ALIAS_CRYPTO("sha1-avx2");
#ifdef CONFIG_AS_SHA1_NI
MODULE_ALIAS_CRYPTO("sha1-ni");
#endif
