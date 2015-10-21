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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <crypto/sha.h>
#include <crypto/sha256_base.h>
#include <asm/fpu/api.h>
#include <linux/string.h>

asmlinkage void sha256_transform_ssse3(u32 *digest, const char *data,
				       u64 rounds);
#ifdef CONFIG_AS_AVX
asmlinkage void sha256_transform_avx(u32 *digest, const char *data,
				     u64 rounds);
#endif
#ifdef CONFIG_AS_AVX2
asmlinkage void sha256_transform_rorx(u32 *digest, const char *data,
				      u64 rounds);
#endif

static void (*sha256_transform_asm)(u32 *, const char *, u64);

static int sha256_ssse3_update(struct shash_desc *desc, const u8 *data,
			     unsigned int len)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	if (!irq_fpu_usable() ||
	    (sctx->count % SHA256_BLOCK_SIZE) + len < SHA256_BLOCK_SIZE)
		return crypto_sha256_update(desc, data, len);

	/* make sure casting to sha256_block_fn() is safe */
	BUILD_BUG_ON(offsetof(struct sha256_state, state) != 0);

	kernel_fpu_begin();
	sha256_base_do_update(desc, data, len,
			      (sha256_block_fn *)sha256_transform_asm);
	kernel_fpu_end();

	return 0;
}

static int sha256_ssse3_finup(struct shash_desc *desc, const u8 *data,
			      unsigned int len, u8 *out)
{
	if (!irq_fpu_usable())
		return crypto_sha256_finup(desc, data, len, out);

	kernel_fpu_begin();
	if (len)
		sha256_base_do_update(desc, data, len,
				      (sha256_block_fn *)sha256_transform_asm);
	sha256_base_do_finalize(desc, (sha256_block_fn *)sha256_transform_asm);
	kernel_fpu_end();

	return sha256_base_finish(desc, out);
}

/* Add padding and return the message digest. */
static int sha256_ssse3_final(struct shash_desc *desc, u8 *out)
{
	return sha256_ssse3_finup(desc, NULL, 0, out);
}

static struct shash_alg algs[] = { {
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
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
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
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

#ifdef CONFIG_AS_AVX
static bool __init avx_usable(void)
{
	if (!cpu_has_xfeatures(XSTATE_SSE | XSTATE_YMM, NULL)) {
		if (cpu_has_avx)
			pr_info("AVX detected but unusable.\n");
		return false;
	}

	return true;
}
#endif

static int __init sha256_ssse3_mod_init(void)
{
	/* test for SSSE3 first */
	if (cpu_has_ssse3)
		sha256_transform_asm = sha256_transform_ssse3;

#ifdef CONFIG_AS_AVX
	/* allow AVX to override SSSE3, it's a little faster */
	if (avx_usable()) {
#ifdef CONFIG_AS_AVX2
		if (boot_cpu_has(X86_FEATURE_AVX2) && boot_cpu_has(X86_FEATURE_BMI2))
			sha256_transform_asm = sha256_transform_rorx;
		else
#endif
			sha256_transform_asm = sha256_transform_avx;
	}
#endif

	if (sha256_transform_asm) {
#ifdef CONFIG_AS_AVX
		if (sha256_transform_asm == sha256_transform_avx)
			pr_info("Using AVX optimized SHA-256 implementation\n");
#ifdef CONFIG_AS_AVX2
		else if (sha256_transform_asm == sha256_transform_rorx)
			pr_info("Using AVX2 optimized SHA-256 implementation\n");
#endif
		else
#endif
			pr_info("Using SSSE3 optimized SHA-256 implementation\n");
		return crypto_register_shashes(algs, ARRAY_SIZE(algs));
	}
	pr_info("Neither AVX nor SSSE3 is available/usable.\n");

	return -ENODEV;
}

static void __exit sha256_ssse3_mod_fini(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}

module_init(sha256_ssse3_mod_init);
module_exit(sha256_ssse3_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA256 Secure Hash Algorithm, Supplemental SSE3 accelerated");

MODULE_ALIAS_CRYPTO("sha256");
MODULE_ALIAS_CRYPTO("sha224");
