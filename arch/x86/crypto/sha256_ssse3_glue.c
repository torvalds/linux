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
#include <asm/byteorder.h>
#include <asm/i387.h>
#include <asm/xcr.h>
#include <asm/xsave.h>
#include <linux/string.h>

asmlinkage void sha256_transform_ssse3(const char *data, u32 *digest,
				     u64 rounds);
#ifdef CONFIG_AS_AVX
asmlinkage void sha256_transform_avx(const char *data, u32 *digest,
				     u64 rounds);
#endif
#ifdef CONFIG_AS_AVX2
asmlinkage void sha256_transform_rorx(const char *data, u32 *digest,
				     u64 rounds);
#endif

static asmlinkage void (*sha256_transform_asm)(const char *, u32 *, u64);


static int sha256_ssse3_init(struct shash_desc *desc)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA256_H0;
	sctx->state[1] = SHA256_H1;
	sctx->state[2] = SHA256_H2;
	sctx->state[3] = SHA256_H3;
	sctx->state[4] = SHA256_H4;
	sctx->state[5] = SHA256_H5;
	sctx->state[6] = SHA256_H6;
	sctx->state[7] = SHA256_H7;
	sctx->count = 0;

	return 0;
}

static int __sha256_ssse3_update(struct shash_desc *desc, const u8 *data,
			       unsigned int len, unsigned int partial)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	unsigned int done = 0;

	sctx->count += len;

	if (partial) {
		done = SHA256_BLOCK_SIZE - partial;
		memcpy(sctx->buf + partial, data, done);
		sha256_transform_asm(sctx->buf, sctx->state, 1);
	}

	if (len - done >= SHA256_BLOCK_SIZE) {
		const unsigned int rounds = (len - done) / SHA256_BLOCK_SIZE;

		sha256_transform_asm(data + done, sctx->state, (u64) rounds);

		done += rounds * SHA256_BLOCK_SIZE;
	}

	memcpy(sctx->buf, data + done, len - done);

	return 0;
}

static int sha256_ssse3_update(struct shash_desc *desc, const u8 *data,
			     unsigned int len)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	unsigned int partial = sctx->count % SHA256_BLOCK_SIZE;
	int res;

	/* Handle the fast case right here */
	if (partial + len < SHA256_BLOCK_SIZE) {
		sctx->count += len;
		memcpy(sctx->buf + partial, data, len);

		return 0;
	}

	if (!irq_fpu_usable()) {
		res = crypto_sha256_update(desc, data, len);
	} else {
		kernel_fpu_begin();
		res = __sha256_ssse3_update(desc, data, len, partial);
		kernel_fpu_end();
	}

	return res;
}


/* Add padding and return the message digest. */
static int sha256_ssse3_final(struct shash_desc *desc, u8 *out)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	unsigned int i, index, padlen;
	__be32 *dst = (__be32 *)out;
	__be64 bits;
	static const u8 padding[SHA256_BLOCK_SIZE] = { 0x80, };

	bits = cpu_to_be64(sctx->count << 3);

	/* Pad out to 56 mod 64 and append length */
	index = sctx->count % SHA256_BLOCK_SIZE;
	padlen = (index < 56) ? (56 - index) : ((SHA256_BLOCK_SIZE+56)-index);

	if (!irq_fpu_usable()) {
		crypto_sha256_update(desc, padding, padlen);
		crypto_sha256_update(desc, (const u8 *)&bits, sizeof(bits));
	} else {
		kernel_fpu_begin();
		/* We need to fill a whole block for __sha256_ssse3_update() */
		if (padlen <= 56) {
			sctx->count += padlen;
			memcpy(sctx->buf + index, padding, padlen);
		} else {
			__sha256_ssse3_update(desc, padding, padlen, index);
		}
		__sha256_ssse3_update(desc, (const u8 *)&bits,
					sizeof(bits), 56);
		kernel_fpu_end();
	}

	/* Store state in digest */
	for (i = 0; i < 8; i++)
		dst[i] = cpu_to_be32(sctx->state[i]);

	/* Wipe context */
	memset(sctx, 0, sizeof(*sctx));

	return 0;
}

static int sha256_ssse3_export(struct shash_desc *desc, void *out)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	memcpy(out, sctx, sizeof(*sctx));

	return 0;
}

static int sha256_ssse3_import(struct shash_desc *desc, const void *in)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	memcpy(sctx, in, sizeof(*sctx));

	return 0;
}

static int sha224_ssse3_init(struct shash_desc *desc)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA224_H0;
	sctx->state[1] = SHA224_H1;
	sctx->state[2] = SHA224_H2;
	sctx->state[3] = SHA224_H3;
	sctx->state[4] = SHA224_H4;
	sctx->state[5] = SHA224_H5;
	sctx->state[6] = SHA224_H6;
	sctx->state[7] = SHA224_H7;
	sctx->count = 0;

	return 0;
}

static int sha224_ssse3_final(struct shash_desc *desc, u8 *hash)
{
	u8 D[SHA256_DIGEST_SIZE];

	sha256_ssse3_final(desc, D);

	memcpy(hash, D, SHA224_DIGEST_SIZE);
	memset(D, 0, SHA256_DIGEST_SIZE);

	return 0;
}

static struct shash_alg algs[] = { {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_ssse3_init,
	.update		=	sha256_ssse3_update,
	.final		=	sha256_ssse3_final,
	.export		=	sha256_ssse3_export,
	.import		=	sha256_ssse3_import,
	.descsize	=	sizeof(struct sha256_state),
	.statesize	=	sizeof(struct sha256_state),
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
	.init		=	sha224_ssse3_init,
	.update		=	sha256_ssse3_update,
	.final		=	sha224_ssse3_final,
	.export		=	sha256_ssse3_export,
	.import		=	sha256_ssse3_import,
	.descsize	=	sizeof(struct sha256_state),
	.statesize	=	sizeof(struct sha256_state),
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
		if (boot_cpu_has(X86_FEATURE_AVX2))
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

MODULE_ALIAS("sha256");
MODULE_ALIAS("sha384");
