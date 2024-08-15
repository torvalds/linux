/*
 * Cryptographic API.
 *
 * Glue code for the SHA512 Secure Hash Algorithm assembler
 * implementation using supplemental SSE3 / AVX / AVX2 instructions.
 *
 * This file is based on sha512_generic.c
 *
 * Copyright (C) 2013 Intel Corporation
 * Author: Tim Chen <tim.c.chen@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>
#include <crypto/sha2.h>
#include <crypto/sha512_base.h>
#include <asm/cpu_device_id.h>
#include <asm/simd.h>

asmlinkage void sha512_transform_ssse3(struct sha512_state *state,
				       const u8 *data, int blocks);

static int sha512_update(struct shash_desc *desc, const u8 *data,
		       unsigned int len, sha512_block_fn *sha512_xform)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);

	if (!crypto_simd_usable() ||
	    (sctx->count[0] % SHA512_BLOCK_SIZE) + len < SHA512_BLOCK_SIZE)
		return crypto_sha512_update(desc, data, len);

	/*
	 * Make sure struct sha512_state begins directly with the SHA512
	 * 512-bit internal state, as this is what the asm functions expect.
	 */
	BUILD_BUG_ON(offsetof(struct sha512_state, state) != 0);

	kernel_fpu_begin();
	sha512_base_do_update(desc, data, len, sha512_xform);
	kernel_fpu_end();

	return 0;
}

static int sha512_finup(struct shash_desc *desc, const u8 *data,
	      unsigned int len, u8 *out, sha512_block_fn *sha512_xform)
{
	if (!crypto_simd_usable())
		return crypto_sha512_finup(desc, data, len, out);

	kernel_fpu_begin();
	if (len)
		sha512_base_do_update(desc, data, len, sha512_xform);
	sha512_base_do_finalize(desc, sha512_xform);
	kernel_fpu_end();

	return sha512_base_finish(desc, out);
}

static int sha512_ssse3_update(struct shash_desc *desc, const u8 *data,
		       unsigned int len)
{
	return sha512_update(desc, data, len, sha512_transform_ssse3);
}

static int sha512_ssse3_finup(struct shash_desc *desc, const u8 *data,
	      unsigned int len, u8 *out)
{
	return sha512_finup(desc, data, len, out, sha512_transform_ssse3);
}

/* Add padding and return the message digest. */
static int sha512_ssse3_final(struct shash_desc *desc, u8 *out)
{
	return sha512_ssse3_finup(desc, NULL, 0, out);
}

static struct shash_alg sha512_ssse3_algs[] = { {
	.digestsize	=	SHA512_DIGEST_SIZE,
	.init		=	sha512_base_init,
	.update		=	sha512_ssse3_update,
	.final		=	sha512_ssse3_final,
	.finup		=	sha512_ssse3_finup,
	.descsize	=	sizeof(struct sha512_state),
	.base		=	{
		.cra_name	=	"sha512",
		.cra_driver_name =	"sha512-ssse3",
		.cra_priority	=	150,
		.cra_blocksize	=	SHA512_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
},  {
	.digestsize	=	SHA384_DIGEST_SIZE,
	.init		=	sha384_base_init,
	.update		=	sha512_ssse3_update,
	.final		=	sha512_ssse3_final,
	.finup		=	sha512_ssse3_finup,
	.descsize	=	sizeof(struct sha512_state),
	.base		=	{
		.cra_name	=	"sha384",
		.cra_driver_name =	"sha384-ssse3",
		.cra_priority	=	150,
		.cra_blocksize	=	SHA384_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

static int register_sha512_ssse3(void)
{
	if (boot_cpu_has(X86_FEATURE_SSSE3))
		return crypto_register_shashes(sha512_ssse3_algs,
			ARRAY_SIZE(sha512_ssse3_algs));
	return 0;
}

static void unregister_sha512_ssse3(void)
{
	if (boot_cpu_has(X86_FEATURE_SSSE3))
		crypto_unregister_shashes(sha512_ssse3_algs,
			ARRAY_SIZE(sha512_ssse3_algs));
}

asmlinkage void sha512_transform_avx(struct sha512_state *state,
				     const u8 *data, int blocks);
static bool avx_usable(void)
{
	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL)) {
		if (boot_cpu_has(X86_FEATURE_AVX))
			pr_info("AVX detected but unusable.\n");
		return false;
	}

	return true;
}

static int sha512_avx_update(struct shash_desc *desc, const u8 *data,
		       unsigned int len)
{
	return sha512_update(desc, data, len, sha512_transform_avx);
}

static int sha512_avx_finup(struct shash_desc *desc, const u8 *data,
	      unsigned int len, u8 *out)
{
	return sha512_finup(desc, data, len, out, sha512_transform_avx);
}

/* Add padding and return the message digest. */
static int sha512_avx_final(struct shash_desc *desc, u8 *out)
{
	return sha512_avx_finup(desc, NULL, 0, out);
}

static struct shash_alg sha512_avx_algs[] = { {
	.digestsize	=	SHA512_DIGEST_SIZE,
	.init		=	sha512_base_init,
	.update		=	sha512_avx_update,
	.final		=	sha512_avx_final,
	.finup		=	sha512_avx_finup,
	.descsize	=	sizeof(struct sha512_state),
	.base		=	{
		.cra_name	=	"sha512",
		.cra_driver_name =	"sha512-avx",
		.cra_priority	=	160,
		.cra_blocksize	=	SHA512_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
},  {
	.digestsize	=	SHA384_DIGEST_SIZE,
	.init		=	sha384_base_init,
	.update		=	sha512_avx_update,
	.final		=	sha512_avx_final,
	.finup		=	sha512_avx_finup,
	.descsize	=	sizeof(struct sha512_state),
	.base		=	{
		.cra_name	=	"sha384",
		.cra_driver_name =	"sha384-avx",
		.cra_priority	=	160,
		.cra_blocksize	=	SHA384_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

static int register_sha512_avx(void)
{
	if (avx_usable())
		return crypto_register_shashes(sha512_avx_algs,
			ARRAY_SIZE(sha512_avx_algs));
	return 0;
}

static void unregister_sha512_avx(void)
{
	if (avx_usable())
		crypto_unregister_shashes(sha512_avx_algs,
			ARRAY_SIZE(sha512_avx_algs));
}

asmlinkage void sha512_transform_rorx(struct sha512_state *state,
				      const u8 *data, int blocks);

static int sha512_avx2_update(struct shash_desc *desc, const u8 *data,
		       unsigned int len)
{
	return sha512_update(desc, data, len, sha512_transform_rorx);
}

static int sha512_avx2_finup(struct shash_desc *desc, const u8 *data,
	      unsigned int len, u8 *out)
{
	return sha512_finup(desc, data, len, out, sha512_transform_rorx);
}

/* Add padding and return the message digest. */
static int sha512_avx2_final(struct shash_desc *desc, u8 *out)
{
	return sha512_avx2_finup(desc, NULL, 0, out);
}

static struct shash_alg sha512_avx2_algs[] = { {
	.digestsize	=	SHA512_DIGEST_SIZE,
	.init		=	sha512_base_init,
	.update		=	sha512_avx2_update,
	.final		=	sha512_avx2_final,
	.finup		=	sha512_avx2_finup,
	.descsize	=	sizeof(struct sha512_state),
	.base		=	{
		.cra_name	=	"sha512",
		.cra_driver_name =	"sha512-avx2",
		.cra_priority	=	170,
		.cra_blocksize	=	SHA512_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
},  {
	.digestsize	=	SHA384_DIGEST_SIZE,
	.init		=	sha384_base_init,
	.update		=	sha512_avx2_update,
	.final		=	sha512_avx2_final,
	.finup		=	sha512_avx2_finup,
	.descsize	=	sizeof(struct sha512_state),
	.base		=	{
		.cra_name	=	"sha384",
		.cra_driver_name =	"sha384-avx2",
		.cra_priority	=	170,
		.cra_blocksize	=	SHA384_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

static bool avx2_usable(void)
{
	if (avx_usable() && boot_cpu_has(X86_FEATURE_AVX2) &&
		    boot_cpu_has(X86_FEATURE_BMI2))
		return true;

	return false;
}

static int register_sha512_avx2(void)
{
	if (avx2_usable())
		return crypto_register_shashes(sha512_avx2_algs,
			ARRAY_SIZE(sha512_avx2_algs));
	return 0;
}
static const struct x86_cpu_id module_cpu_ids[] = {
	X86_MATCH_FEATURE(X86_FEATURE_AVX2, NULL),
	X86_MATCH_FEATURE(X86_FEATURE_AVX, NULL),
	X86_MATCH_FEATURE(X86_FEATURE_SSSE3, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, module_cpu_ids);

static void unregister_sha512_avx2(void)
{
	if (avx2_usable())
		crypto_unregister_shashes(sha512_avx2_algs,
			ARRAY_SIZE(sha512_avx2_algs));
}

static int __init sha512_ssse3_mod_init(void)
{
	if (!x86_match_cpu(module_cpu_ids))
		return -ENODEV;

	if (register_sha512_ssse3())
		goto fail;

	if (register_sha512_avx()) {
		unregister_sha512_ssse3();
		goto fail;
	}

	if (register_sha512_avx2()) {
		unregister_sha512_avx();
		unregister_sha512_ssse3();
		goto fail;
	}

	return 0;
fail:
	return -ENODEV;
}

static void __exit sha512_ssse3_mod_fini(void)
{
	unregister_sha512_avx2();
	unregister_sha512_avx();
	unregister_sha512_ssse3();
}

module_init(sha512_ssse3_mod_init);
module_exit(sha512_ssse3_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA512 Secure Hash Algorithm, Supplemental SSE3 accelerated");

MODULE_ALIAS_CRYPTO("sha512");
MODULE_ALIAS_CRYPTO("sha512-ssse3");
MODULE_ALIAS_CRYPTO("sha512-avx");
MODULE_ALIAS_CRYPTO("sha512-avx2");
MODULE_ALIAS_CRYPTO("sha384");
MODULE_ALIAS_CRYPTO("sha384-ssse3");
MODULE_ALIAS_CRYPTO("sha384-avx");
MODULE_ALIAS_CRYPTO("sha384-avx2");
