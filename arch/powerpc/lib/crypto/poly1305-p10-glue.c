// SPDX-License-Identifier: GPL-2.0
/*
 * Poly1305 authenticator algorithm, RFC7539.
 *
 * Copyright 2023- IBM Corp. All rights reserved.
 */
#include <asm/switch_to.h>
#include <crypto/internal/poly1305.h>
#include <linux/cpufeature.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>

asmlinkage void poly1305_p10le_4blocks(struct poly1305_block_state *state, const u8 *m, u32 mlen);
asmlinkage void poly1305_64s(struct poly1305_block_state *state, const u8 *m, u32 mlen, int highbit);
asmlinkage void poly1305_emit_arch(const struct poly1305_state *state,
				   u8 digest[POLY1305_DIGEST_SIZE],
				   const u32 nonce[4]);

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

void poly1305_block_init_arch(struct poly1305_block_state *dctx,
			      const u8 raw_key[POLY1305_BLOCK_SIZE])
{
	if (!static_key_enabled(&have_p10))
		return poly1305_block_init_generic(dctx, raw_key);

	dctx->h = (struct poly1305_state){};
	dctx->core_r.key.r64[0] = get_unaligned_le64(raw_key + 0);
	dctx->core_r.key.r64[1] = get_unaligned_le64(raw_key + 8);
}
EXPORT_SYMBOL_GPL(poly1305_block_init_arch);

void poly1305_init_arch(struct poly1305_desc_ctx *dctx, const u8 key[POLY1305_KEY_SIZE])
{
	dctx->s[0] = get_unaligned_le32(key + 16);
	dctx->s[1] = get_unaligned_le32(key + 20);
	dctx->s[2] = get_unaligned_le32(key + 24);
	dctx->s[3] = get_unaligned_le32(key + 28);
	dctx->buflen = 0;
	poly1305_block_init_arch(&dctx->state, key);
}
EXPORT_SYMBOL(poly1305_init_arch);

void poly1305_blocks_arch(struct poly1305_block_state *state, const u8 *src,
			  unsigned int len, u32 padbit)
{
	if (!static_key_enabled(&have_p10))
		return poly1305_blocks_generic(state, src, len, padbit);
	vsx_begin();
	if (len >= POLY1305_BLOCK_SIZE * 4) {
		poly1305_p10le_4blocks(state, src, len);
		src += len - (len % (POLY1305_BLOCK_SIZE * 4));
		len %= POLY1305_BLOCK_SIZE * 4;
	}
	while (len >= POLY1305_BLOCK_SIZE) {
		poly1305_64s(state, src, POLY1305_BLOCK_SIZE, padbit);
		len -= POLY1305_BLOCK_SIZE;
		src += POLY1305_BLOCK_SIZE;
	}
	vsx_end();
}
EXPORT_SYMBOL_GPL(poly1305_blocks_arch);

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
		poly1305_blocks_arch(&dctx->state, dctx->buf,
				     POLY1305_BLOCK_SIZE, 1);
		dctx->buflen = 0;
	}

	if (likely(srclen >= POLY1305_BLOCK_SIZE)) {
		poly1305_blocks_arch(&dctx->state, src, srclen, 1);
		src += srclen - (srclen % POLY1305_BLOCK_SIZE);
		srclen %= POLY1305_BLOCK_SIZE;
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
		poly1305_blocks_arch(&dctx->state, dctx->buf,
				     POLY1305_BLOCK_SIZE, 0);
	}

	poly1305_emit_arch(&dctx->h, dst, dctx->s);
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
