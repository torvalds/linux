// SPDX-License-Identifier: GPL-2.0
/*
 * Poly1305 authenticator algorithm, RFC7539.
 *
 * Copyright 2023- IBM Corp. All rights reserved.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jump_label.h>
#include <crypto/internal/simd.h>
#include <crypto/poly1305.h>
#include <linux/cpufeature.h>
#include <linux/unaligned.h>
#include <asm/simd.h>
#include <asm/switch_to.h>

asmlinkage void poly1305_p10le_4blocks(void *h, const u8 *m, u32 mlen);
asmlinkage void poly1305_64s(void *h, const u8 *m, u32 mlen, int highbit);
asmlinkage void poly1305_emit_64(void *h, void *s, u8 *dst);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_p10);

static void vsx_begin(void)
{
	preempt_disable();
	enable_kernel_vsx();
}

static void vsx_end(void)
{
	disable_kernel_vsx();
	preempt_enable();
}

void poly1305_init_arch(struct poly1305_desc_ctx *dctx, const u8 key[POLY1305_KEY_SIZE])
{
	if (!static_key_enabled(&have_p10))
		return poly1305_init_generic(dctx, key);

	dctx->h = (struct poly1305_state){};
	dctx->core_r.key.r64[0] = get_unaligned_le64(key + 0);
	dctx->core_r.key.r64[1] = get_unaligned_le64(key + 8);
	dctx->s[0] = get_unaligned_le32(key + 16);
	dctx->s[1] = get_unaligned_le32(key + 20);
	dctx->s[2] = get_unaligned_le32(key + 24);
	dctx->s[3] = get_unaligned_le32(key + 28);
	dctx->buflen = 0;
}
EXPORT_SYMBOL(poly1305_init_arch);

void poly1305_update_arch(struct poly1305_desc_ctx *dctx,
			  const u8 *src, unsigned int srclen)
{
	unsigned int bytes;

	if (!static_key_enabled(&have_p10))
		return poly1305_update_generic(dctx, src, srclen);

	if (unlikely(dctx->buflen)) {
		bytes = min(srclen, POLY1305_BLOCK_SIZE - dctx->buflen);
		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		srclen -= bytes;
		dctx->buflen += bytes;
		if (dctx->buflen < POLY1305_BLOCK_SIZE)
			return;
		vsx_begin();
		poly1305_64s(&dctx->h, dctx->buf, POLY1305_BLOCK_SIZE, 1);
		vsx_end();
		dctx->buflen = 0;
	}

	if (likely(srclen >= POLY1305_BLOCK_SIZE)) {
		bytes = round_down(srclen, POLY1305_BLOCK_SIZE);
		if (crypto_simd_usable() && (srclen >= POLY1305_BLOCK_SIZE*4)) {
			vsx_begin();
			poly1305_p10le_4blocks(&dctx->h, src, srclen);
			vsx_end();
			src += srclen - (srclen % (POLY1305_BLOCK_SIZE * 4));
			srclen %= POLY1305_BLOCK_SIZE * 4;
		}
		while (srclen >= POLY1305_BLOCK_SIZE) {
			vsx_begin();
			poly1305_64s(&dctx->h, src, POLY1305_BLOCK_SIZE, 1);
			vsx_end();
			srclen -= POLY1305_BLOCK_SIZE;
			src += POLY1305_BLOCK_SIZE;
		}
	}

	if (unlikely(srclen)) {
		dctx->buflen = srclen;
		memcpy(dctx->buf, src, srclen);
	}
}
EXPORT_SYMBOL(poly1305_update_arch);

void poly1305_final_arch(struct poly1305_desc_ctx *dctx, u8 *dst)
{
	if (!static_key_enabled(&have_p10))
		return poly1305_final_generic(dctx, dst);

	if (dctx->buflen) {
		dctx->buf[dctx->buflen++] = 1;
		memset(dctx->buf + dctx->buflen, 0,
		       POLY1305_BLOCK_SIZE - dctx->buflen);
		vsx_begin();
		poly1305_64s(&dctx->h, dctx->buf, POLY1305_BLOCK_SIZE, 0);
		vsx_end();
	}

	poly1305_emit_64(&dctx->h, &dctx->s, dst);
}
EXPORT_SYMBOL(poly1305_final_arch);

bool poly1305_is_arch_optimized(void)
{
	return static_key_enabled(&have_p10);
}
EXPORT_SYMBOL(poly1305_is_arch_optimized);

static int __init poly1305_p10_init(void)
{
	if (cpu_has_feature(CPU_FTR_ARCH_31))
		static_branch_enable(&have_p10);
	return 0;
}
arch_initcall(poly1305_p10_init);

static void __exit poly1305_p10_exit(void)
{
}
module_exit(poly1305_p10_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Danny Tsen <dtsen@linux.ibm.com>");
MODULE_DESCRIPTION("Optimized Poly1305 for P10");
