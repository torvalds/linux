/*
 * Cryptographic API.
 *
 * Glue code for the SHA256 Secure Hash Algorithm assembler
 * implementation using supplemental SSE3 / AVX / AVX2 instructions.
 *
 * This file is based on sha256_generic.c
 *
 * Copyright (C) 2013 Intel Corporation.
 *
 * Author:
 *     Tim Chen <tim.c.chen@linux.intel.com>
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
#include <crypto/sha256_base.h>
#include <linux/string.h>
#include <asm/simd.h>

asmlinkage void sha256_transform_ssse3(u32 *digest, const char *data,
				       u64 rounds);
typedef void (sha256_transform_fn)(u32 *digest, const char *data, u64 rounds);

static int sha256_update(struct shash_desc *desc, const u8 *data,
			 unsigned int len, sha256_transform_fn *sha256_xform)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	if (!crypto_simd_usable() ||
	    (sctx->count % SHA256_BLOCK_SIZE) + len < SHA256_BLOCK_SIZE)
		return crypto_sha256_update(desc, data, len);

	/* make sure casting to sha256_block_fn() is safe */
	BUILD_BUG_ON(offsetof(struct sha256_state, state) != 0);

	kernel_fpu_begin();
	sha256_base_do_update(desc, data, len,
			      (sha256_block_fn *)sha256_xform);
	kernel_fpu_end();

	return 0;
}

static int sha256_finup(struct shash_desc *desc, const u8 *data,
	      unsigned int len, u8 *out, sha256_transform_fn *sha256_xform)
{
	if (!crypto_simd_usable())
		return crypto_sha256_finup(desc, data, len, out);

	kernel_fpu_begin();
	if (len)
		sha256_base_do_update(desc, data, len,
				      (sha256_block_fn *)sha256_xform);
	sha256_base_do_finalize(desc, (sha256_block_fn *)sha256_xform);
	kernel_fpu_end();

	return sha256_base_finish(desc, out);
}

static int sha256_ssse3_update(struct shash_desc *desc, const u8 *data,
			 unsigned int len)
{
	return sha256_update(desc, data, len, sha256_transform_ssse3);
}

static int sha256_ssse3_finup(struct shash_desc *desc, const u8 *data,
	      unsigned int len, u8 *out)
{
	return sha256_finup(desc, data, len, out, sha256_transform_ssse3);
}

/* Add padding and return the message digest. */
static int sha256_ssse3_final(struct shash_desc *desc, u8 *out)
{
	return sha256_ssse3_finup(desc, NULL, 0, out);
}

static struct shash_alg sha256_ssse3_algs[] = { {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_base_init,
	.update		=	sha256_ssse3_update,
	.final		=	sha256_ssse3_final,
	.finup		=	sha256_ssse3_finup,
	.descsize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name =	"sha256-ssse3",
		.cra_priority	=	150,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	sha224_base_init,
	.update		=	sha256_ssse3_update,
	.final		=	sha256_ssse3_final,
	.finup		=	sha256_ssse3_finup,
	.descsize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name =	"sha224-ssse3",
		.cra_priority	=	150,
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

static int register_sha256_ssse3(void)
{
	if (boot_cpu_has(X86_FEATURE_SSSE3))
		return crypto_register_shashes(sha256_ssse3_algs,
				ARRAY_SIZE(sha256_ssse3_algs));
	return 0;
}

static void unregister_sha256_ssse3(void)
{
	if (boot_cpu_has(X86_FEATURE_SSSE3))
		crypto_unregister_shashes(sha256_ssse3_algs,
				ARRAY_SIZE(sha256_ssse3_algs));
}

#ifdef CONFIG_AS_AVX
asmlinkage void sha256_transform_avx(u32 *digest, const char *data,
				     u64 rounds);

static int sha256_avx_update(struct shash_desc *desc, const u8 *data,
			 unsigned int len)
{
	return sha256_update(desc, data, len, sha256_transform_avx);
}

static int sha256_avx_finup(struct shash_desc *desc, const u8 *data,
		      unsigned int len, u8 *out)
{
	return sha256_finup(desc, data, len, out, sha256_transform_avx);
}

static int sha256_avx_final(struct shash_desc *desc, u8 *out)
{
	return sha256_avx_finup(desc, NULL, 0, out);
}

static struct shash_alg sha256_avx_algs[] = { {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_base_init,
	.update		=	sha256_avx_update,
	.final		=	sha256_avx_final,
	.finup		=	sha256_avx_finup,
	.descsize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name =	"sha256-avx",
		.cra_priority	=	160,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	sha224_base_init,
	.update		=	sha256_avx_update,
	.final		=	sha256_avx_final,
	.finup		=	sha256_avx_finup,
	.descsize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name =	"sha224-avx",
		.cra_priority	=	160,
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

static bool avx_usable(void)
{
	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL)) {
		if (boot_cpu_has(X86_FEATURE_AVX))
			pr_info("AVX detected but unusable.\n");
		return false;
	}

	return true;
}

static int register_sha256_avx(void)
{
	if (avx_usable())
		return crypto_register_shashes(sha256_avx_algs,
				ARRAY_SIZE(sha256_avx_algs));
	return 0;
}

static void unregister_sha256_avx(void)
{
	if (avx_usable())
		crypto_unregister_shashes(sha256_avx_algs,
				ARRAY_SIZE(sha256_avx_algs));
}

#else
static inline int register_sha256_avx(void) { return 0; }
static inline void unregister_sha256_avx(void) { }
#endif

#if defined(CONFIG_AS_AVX2) && defined(CONFIG_AS_AVX)
asmlinkage void sha256_transform_rorx(u32 *digest, const char *data,
				      u64 rounds);

static int sha256_avx2_update(struct shash_desc *desc, const u8 *data,
			 unsigned int len)
{
	return sha256_update(desc, data, len, sha256_transform_rorx);
}

static int sha256_avx2_finup(struct shash_desc *desc, const u8 *data,
		      unsigned int len, u8 *out)
{
	return sha256_finup(desc, data, len, out, sha256_transform_rorx);
}

static int sha256_avx2_final(struct shash_desc *desc, u8 *out)
{
	return sha256_avx2_finup(desc, NULL, 0, out);
}

static struct shash_alg sha256_avx2_algs[] = { {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_base_init,
	.update		=	sha256_avx2_update,
	.final		=	sha256_avx2_final,
	.finup		=	sha256_avx2_finup,
	.descsize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name =	"sha256-avx2",
		.cra_priority	=	170,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	sha224_base_init,
	.update		=	sha256_avx2_update,
	.final		=	sha256_avx2_final,
	.finup		=	sha256_avx2_finup,
	.descsize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name =	"sha224-avx2",
		.cra_priority	=	170,
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
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

static int register_sha256_avx2(void)
{
	if (avx2_usable())
		return crypto_register_shashes(sha256_avx2_algs,
				ARRAY_SIZE(sha256_avx2_algs));
	return 0;
}

static void unregister_sha256_avx2(void)
{
	if (avx2_usable())
		crypto_unregister_shashes(sha256_avx2_algs,
				ARRAY_SIZE(sha256_avx2_algs));
}

#else
static inline int register_sha256_avx2(void) { return 0; }
static inline void unregister_sha256_avx2(void) { }
#endif

#ifdef CONFIG_AS_SHA256_NI
asmlinkage void sha256_ni_transform(u32 *digest, const char *data,
				   u64 rounds); /*unsigned int rounds);*/

static int sha256_ni_update(struct shash_desc *desc, const u8 *data,
			 unsigned int len)
{
	return sha256_update(desc, data, len, sha256_ni_transform);
}

static int sha256_ni_finup(struct shash_desc *desc, const u8 *data,
		      unsigned int len, u8 *out)
{
	return sha256_finup(desc, data, len, out, sha256_ni_transform);
}

static int sha256_ni_final(struct shash_desc *desc, u8 *out)
{
	return sha256_ni_finup(desc, NULL, 0, out);
}

static struct shash_alg sha256_ni_algs[] = { {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_base_init,
	.update		=	sha256_ni_update,
	.final		=	sha256_ni_final,
	.finup		=	sha256_ni_finup,
	.descsize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name =	"sha256-ni",
		.cra_priority	=	250,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	sha224_base_init,
	.update		=	sha256_ni_update,
	.final		=	sha256_ni_final,
	.finup		=	sha256_ni_finup,
	.descsize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name =	"sha224-ni",
		.cra_priority	=	250,
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

static int register_sha256_ni(void)
{
	if (boot_cpu_has(X86_FEATURE_SHA_NI))
		return crypto_register_shashes(sha256_ni_algs,
				ARRAY_SIZE(sha256_ni_algs));
	return 0;
}

static void unregister_sha256_ni(void)
{
	if (boot_cpu_has(X86_FEATURE_SHA_NI))
		crypto_unregister_shashes(sha256_ni_algs,
				ARRAY_SIZE(sha256_ni_algs));
}

#else
static inline int register_sha256_ni(void) { return 0; }
static inline void unregister_sha256_ni(void) { }
#endif

static int __init sha256_ssse3_mod_init(void)
{
	if (register_sha256_ssse3())
		goto fail;

	if (register_sha256_avx()) {
		unregister_sha256_ssse3();
		goto fail;
	}

	if (register_sha256_avx2()) {
		unregister_sha256_avx();
		unregister_sha256_ssse3();
		goto fail;
	}

	if (register_sha256_ni()) {
		unregister_sha256_avx2();
		unregister_sha256_avx();
		unregister_sha256_ssse3();
		goto fail;
	}

	return 0;
fail:
	return -ENODEV;
}

static void __exit sha256_ssse3_mod_fini(void)
{
	unregister_sha256_ni();
	unregister_sha256_avx2();
	unregister_sha256_avx();
	unregister_sha256_ssse3();
}

module_init(sha256_ssse3_mod_init);
module_exit(sha256_ssse3_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA256 Secure Hash Algorithm, Supplemental SSE3 accelerated");

MODULE_ALIAS_CRYPTO("sha256");
MODULE_ALIAS_CRYPTO("sha256-ssse3");
MODULE_ALIAS_CRYPTO("sha256-avx");
MODULE_ALIAS_CRYPTO("sha256-avx2");
MODULE_ALIAS_CRYPTO("sha224");
MODULE_ALIAS_CRYPTO("sha224-ssse3");
MODULE_ALIAS_CRYPTO("sha224-avx");
MODULE_ALIAS_CRYPTO("sha224-avx2");
#ifdef CONFIG_AS_SHA256_NI
MODULE_ALIAS_CRYPTO("sha256-ni");
MODULE_ALIAS_CRYPTO("sha224-ni");
#endif
