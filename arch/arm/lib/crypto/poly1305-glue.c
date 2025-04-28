// SPDX-License-Identifier: GPL-2.0
/*
 * OpenSSL/Cryptogams accelerated Poly1305 transform for ARM
 *
 * Copyright (C) 2019 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <crypto/internal/poly1305.h>
#include <linux/cpufeature.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>

asmlinkage void poly1305_block_init_arch(
	struct poly1305_block_state *state,
	const u8 raw_key[POLY1305_BLOCK_SIZE]);
EXPORT_SYMBOL_GPL(poly1305_block_init_arch);
asmlinkage void poly1305_blocks_arm(struct poly1305_block_state *state,
				    const u8 *src, u32 len, u32 hibit);
asmlinkage void poly1305_blocks_neon(struct poly1305_block_state *state,
				     const u8 *src, u32 len, u32 hibit);
asmlinkage void poly1305_emit_arch(const struct poly1305_state *state,
				   u8 digest[POLY1305_DIGEST_SIZE],
				   const u32 nonce[4]);
EXPORT_SYMBOL_GPL(poly1305_emit_arch);

void __weak poly1305_blocks_neon(struct poly1305_block_state *state,
				 const u8 *src, u32 len, u32 hibit)
{
}

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);

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
	len = round_down(len, POLY1305_BLOCK_SIZE);
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    static_branch_likely(&have_neon)) {
		do {
			unsigned int todo = min_t(unsigned int, len, SZ_4K);

			kernel_neon_begin();
			poly1305_blocks_neon(state, src, todo, padbit);
			kernel_neon_end();

			len -= todo;
			src += todo;
		} while (len);
	} else
		poly1305_blocks_arm(state, src, len, padbit);
}
EXPORT_SYMBOL_GPL(poly1305_blocks_arch);

void poly1305_update_arch(struct poly1305_desc_ctx *dctx, const u8 *src,
			  unsigned int nbytes)
{
	if (unlikely(dctx->buflen)) {
		u32 bytes = min(nbytes, POLY1305_BLOCK_SIZE - dctx->buflen);

		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		nbytes -= bytes;
		dctx->buflen += bytes;

		if (dctx->buflen == POLY1305_BLOCK_SIZE) {
			poly1305_blocks_arch(&dctx->state, dctx->buf,
					     POLY1305_BLOCK_SIZE, 1);
			dctx->buflen = 0;
		}
	}

	if (likely(nbytes >= POLY1305_BLOCK_SIZE)) {
		poly1305_blocks_arch(&dctx->state, src, nbytes, 1);
		src += round_down(nbytes, POLY1305_BLOCK_SIZE);
		nbytes %= POLY1305_BLOCK_SIZE;
	}

	if (unlikely(nbytes)) {
		dctx->buflen = nbytes;
		memcpy(dctx->buf, src, nbytes);
	}
}
EXPORT_SYMBOL(poly1305_update_arch);

void poly1305_final_arch(struct poly1305_desc_ctx *dctx, u8 *dst)
{
	if (unlikely(dctx->buflen)) {
		dctx->buf[dctx->buflen++] = 1;
		memset(dctx->buf + dctx->buflen, 0,
		       POLY1305_BLOCK_SIZE - dctx->buflen);
		poly1305_blocks_arch(&dctx->state, dctx->buf, POLY1305_BLOCK_SIZE, 0);
	}

	poly1305_emit_arch(&dctx->h, dst, dctx->s);
	*dctx = (struct poly1305_desc_ctx){};
}
EXPORT_SYMBOL(poly1305_final_arch);

bool poly1305_is_arch_optimized(void)
{
	/* We always can use at least the ARM scalar implementation. */
	return true;
}
EXPORT_SYMBOL(poly1305_is_arch_optimized);

static int __init arm_poly1305_mod_init(void)
{
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    (elf_hwcap & HWCAP_NEON))
		static_branch_enable(&have_neon);
	return 0;
}
arch_initcall(arm_poly1305_mod_init);

static void __exit arm_poly1305_mod_exit(void)
{
}
module_exit(arm_poly1305_mod_exit);

MODULE_DESCRIPTION("Accelerated Poly1305 transform for ARM");
MODULE_LICENSE("GPL v2");
