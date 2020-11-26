// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include <crypto/algapi.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/poly1305.h>
#include <crypto/internal/simd.h>
#include <linux/crypto.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <asm/intel-family.h>
#include <asm/simd.h>

asmlinkage void poly1305_init_x86_64(void *ctx,
				     const u8 key[POLY1305_KEY_SIZE]);
asmlinkage void poly1305_blocks_x86_64(void *ctx, const u8 *inp,
				       const size_t len, const u32 padbit);
asmlinkage void poly1305_emit_x86_64(void *ctx, u8 mac[POLY1305_DIGEST_SIZE],
				     const u32 nonce[4]);
asmlinkage void poly1305_emit_avx(void *ctx, u8 mac[POLY1305_DIGEST_SIZE],
				  const u32 nonce[4]);
asmlinkage void poly1305_blocks_avx(void *ctx, const u8 *inp, const size_t len,
				    const u32 padbit);
asmlinkage void poly1305_blocks_avx2(void *ctx, const u8 *inp, const size_t len,
				     const u32 padbit);
asmlinkage void poly1305_blocks_avx512(void *ctx, const u8 *inp,
				       const size_t len, const u32 padbit);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(poly1305_use_avx);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(poly1305_use_avx2);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(poly1305_use_avx512);

struct poly1305_arch_internal {
	union {
		struct {
			u32 h[5];
			u32 is_base2_26;
		};
		u64 hs[3];
	};
	u64 r[2];
	u64 pad;
	struct { u32 r2, r1, r4, r3; } rn[9];
};

/* The AVX code uses base 2^26, while the scalar code uses base 2^64. If we hit
 * the unfortunate situation of using AVX and then having to go back to scalar
 * -- because the user is silly and has called the update function from two
 * separate contexts -- then we need to convert back to the original base before
 * proceeding. It is possible to reason that the initial reduction below is
 * sufficient given the implementation invariants. However, for an avoidance of
 * doubt and because this is not performance critical, we do the full reduction
 * anyway. Z3 proof of below function: https://xn--4db.cc/ltPtHCKN/py
 */
static void convert_to_base2_64(void *ctx)
{
	struct poly1305_arch_internal *state = ctx;
	u32 cy;

	if (!state->is_base2_26)
		return;

	cy = state->h[0] >> 26; state->h[0] &= 0x3ffffff; state->h[1] += cy;
	cy = state->h[1] >> 26; state->h[1] &= 0x3ffffff; state->h[2] += cy;
	cy = state->h[2] >> 26; state->h[2] &= 0x3ffffff; state->h[3] += cy;
	cy = state->h[3] >> 26; state->h[3] &= 0x3ffffff; state->h[4] += cy;
	state->hs[0] = ((u64)state->h[2] << 52) | ((u64)state->h[1] << 26) | state->h[0];
	state->hs[1] = ((u64)state->h[4] << 40) | ((u64)state->h[3] << 14) | (state->h[2] >> 12);
	state->hs[2] = state->h[4] >> 24;
#define ULT(a, b) ((a ^ ((a ^ b) | ((a - b) ^ b))) >> (sizeof(a) * 8 - 1))
	cy = (state->hs[2] >> 2) + (state->hs[2] & ~3ULL);
	state->hs[2] &= 3;
	state->hs[0] += cy;
	state->hs[1] += (cy = ULT(state->hs[0], cy));
	state->hs[2] += ULT(state->hs[1], cy);
#undef ULT
	state->is_base2_26 = 0;
}

static void poly1305_simd_init(void *ctx, const u8 key[POLY1305_KEY_SIZE])
{
	poly1305_init_x86_64(ctx, key);
}

static void poly1305_simd_blocks(void *ctx, const u8 *inp, size_t len,
				 const u32 padbit)
{
	struct poly1305_arch_internal *state = ctx;

	/* SIMD disables preemption, so relax after processing each page. */
	BUILD_BUG_ON(SZ_4K < POLY1305_BLOCK_SIZE ||
		     SZ_4K % POLY1305_BLOCK_SIZE);

	if (!static_branch_likely(&poly1305_use_avx) ||
	    (len < (POLY1305_BLOCK_SIZE * 18) && !state->is_base2_26) ||
	    !crypto_simd_usable()) {
		convert_to_base2_64(ctx);
		poly1305_blocks_x86_64(ctx, inp, len, padbit);
		return;
	}

	do {
		const size_t bytes = min_t(size_t, len, SZ_4K);

		kernel_fpu_begin();
		if (IS_ENABLED(CONFIG_AS_AVX512) && static_branch_likely(&poly1305_use_avx512))
			poly1305_blocks_avx512(ctx, inp, bytes, padbit);
		else if (static_branch_likely(&poly1305_use_avx2))
			poly1305_blocks_avx2(ctx, inp, bytes, padbit);
		else
			poly1305_blocks_avx(ctx, inp, bytes, padbit);
		kernel_fpu_end();

		len -= bytes;
		inp += bytes;
	} while (len);
}

static void poly1305_simd_emit(void *ctx, u8 mac[POLY1305_DIGEST_SIZE],
			       const u32 nonce[4])
{
	if (!static_branch_likely(&poly1305_use_avx))
		poly1305_emit_x86_64(ctx, mac, nonce);
	else
		poly1305_emit_avx(ctx, mac, nonce);
}

void poly1305_init_arch(struct poly1305_desc_ctx *dctx, const u8 *key)
{
	poly1305_simd_init(&dctx->h, key);
	dctx->s[0] = get_unaligned_le32(&key[16]);
	dctx->s[1] = get_unaligned_le32(&key[20]);
	dctx->s[2] = get_unaligned_le32(&key[24]);
	dctx->s[3] = get_unaligned_le32(&key[28]);
	dctx->buflen = 0;
	dctx->sset = true;
}
EXPORT_SYMBOL(poly1305_init_arch);

static unsigned int crypto_poly1305_setdctxkey(struct poly1305_desc_ctx *dctx,
					       const u8 *inp, unsigned int len)
{
	unsigned int acc = 0;
	if (unlikely(!dctx->sset)) {
		if (!dctx->rset && len >= POLY1305_BLOCK_SIZE) {
			poly1305_simd_init(&dctx->h, inp);
			inp += POLY1305_BLOCK_SIZE;
			len -= POLY1305_BLOCK_SIZE;
			acc += POLY1305_BLOCK_SIZE;
			dctx->rset = 1;
		}
		if (len >= POLY1305_BLOCK_SIZE) {
			dctx->s[0] = get_unaligned_le32(&inp[0]);
			dctx->s[1] = get_unaligned_le32(&inp[4]);
			dctx->s[2] = get_unaligned_le32(&inp[8]);
			dctx->s[3] = get_unaligned_le32(&inp[12]);
			acc += POLY1305_BLOCK_SIZE;
			dctx->sset = true;
		}
	}
	return acc;
}

void poly1305_update_arch(struct poly1305_desc_ctx *dctx, const u8 *src,
			  unsigned int srclen)
{
	unsigned int bytes, used;

	if (unlikely(dctx->buflen)) {
		bytes = min(srclen, POLY1305_BLOCK_SIZE - dctx->buflen);
		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		srclen -= bytes;
		dctx->buflen += bytes;

		if (dctx->buflen == POLY1305_BLOCK_SIZE) {
			if (likely(!crypto_poly1305_setdctxkey(dctx, dctx->buf, POLY1305_BLOCK_SIZE)))
				poly1305_simd_blocks(&dctx->h, dctx->buf, POLY1305_BLOCK_SIZE, 1);
			dctx->buflen = 0;
		}
	}

	if (likely(srclen >= POLY1305_BLOCK_SIZE)) {
		bytes = round_down(srclen, POLY1305_BLOCK_SIZE);
		srclen -= bytes;
		used = crypto_poly1305_setdctxkey(dctx, src, bytes);
		if (likely(bytes - used))
			poly1305_simd_blocks(&dctx->h, src + used, bytes - used, 1);
		src += bytes;
	}

	if (unlikely(srclen)) {
		dctx->buflen = srclen;
		memcpy(dctx->buf, src, srclen);
	}
}
EXPORT_SYMBOL(poly1305_update_arch);

void poly1305_final_arch(struct poly1305_desc_ctx *dctx, u8 *dst)
{
	if (unlikely(dctx->buflen)) {
		dctx->buf[dctx->buflen++] = 1;
		memset(dctx->buf + dctx->buflen, 0,
		       POLY1305_BLOCK_SIZE - dctx->buflen);
		poly1305_simd_blocks(&dctx->h, dctx->buf, POLY1305_BLOCK_SIZE, 0);
	}

	poly1305_simd_emit(&dctx->h, dst, dctx->s);
	*dctx = (struct poly1305_desc_ctx){};
}
EXPORT_SYMBOL(poly1305_final_arch);

static int crypto_poly1305_init(struct shash_desc *desc)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	*dctx = (struct poly1305_desc_ctx){};
	return 0;
}

static int crypto_poly1305_update(struct shash_desc *desc,
				  const u8 *src, unsigned int srclen)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	poly1305_update_arch(dctx, src, srclen);
	return 0;
}

static int crypto_poly1305_final(struct shash_desc *desc, u8 *dst)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	if (unlikely(!dctx->sset))
		return -ENOKEY;

	poly1305_final_arch(dctx, dst);
	return 0;
}

static struct shash_alg alg = {
	.digestsize	= POLY1305_DIGEST_SIZE,
	.init		= crypto_poly1305_init,
	.update		= crypto_poly1305_update,
	.final		= crypto_poly1305_final,
	.descsize	= sizeof(struct poly1305_desc_ctx),
	.base		= {
		.cra_name		= "poly1305",
		.cra_driver_name	= "poly1305-simd",
		.cra_priority		= 300,
		.cra_blocksize		= POLY1305_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	},
};

static int __init poly1305_simd_mod_init(void)
{
	if (boot_cpu_has(X86_FEATURE_AVX) &&
	    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL))
		static_branch_enable(&poly1305_use_avx);
	if (boot_cpu_has(X86_FEATURE_AVX) && boot_cpu_has(X86_FEATURE_AVX2) &&
	    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL))
		static_branch_enable(&poly1305_use_avx2);
	if (IS_ENABLED(CONFIG_AS_AVX512) && boot_cpu_has(X86_FEATURE_AVX) &&
	    boot_cpu_has(X86_FEATURE_AVX2) && boot_cpu_has(X86_FEATURE_AVX512F) &&
	    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM | XFEATURE_MASK_AVX512, NULL) &&
	    /* Skylake downclocks unacceptably much when using zmm, but later generations are fast. */
	    boot_cpu_data.x86_model != INTEL_FAM6_SKYLAKE_X)
		static_branch_enable(&poly1305_use_avx512);
	return IS_REACHABLE(CONFIG_CRYPTO_HASH) ? crypto_register_shash(&alg) : 0;
}

static void __exit poly1305_simd_mod_exit(void)
{
	if (IS_REACHABLE(CONFIG_CRYPTO_HASH))
		crypto_unregister_shash(&alg);
}

module_init(poly1305_simd_mod_init);
module_exit(poly1305_simd_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
MODULE_DESCRIPTION("Poly1305 authenticator");
MODULE_ALIAS_CRYPTO("poly1305");
MODULE_ALIAS_CRYPTO("poly1305-simd");
