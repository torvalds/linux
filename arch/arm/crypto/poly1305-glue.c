// SPDX-License-Identifier: GPL-2.0
/*
 * OpenSSL/Cryptogams accelerated Poly1305 transform for ARM
 *
 * Copyright (C) 2019 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/poly1305.h>
#include <crypto/internal/simd.h>
#include <linux/cpufeature.h>
#include <linux/jump_label.h>
#include <linux/module.h>
#include <linux/unaligned.h>

void poly1305_init_arm(void *state, const u8 *key);
void poly1305_blocks_arm(void *state, const u8 *src, u32 len, u32 hibit);
void poly1305_blocks_neon(void *state, const u8 *src, u32 len, u32 hibit);
void poly1305_emit_arm(void *state, u8 *digest, const u32 *nonce);

void __weak poly1305_blocks_neon(void *state, const u8 *src, u32 len, u32 hibit)
{
}

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);

void poly1305_init_arch(struct poly1305_desc_ctx *dctx, const u8 key[POLY1305_KEY_SIZE])
{
	poly1305_init_arm(&dctx->h, key);
	dctx->s[0] = get_unaligned_le32(key + 16);
	dctx->s[1] = get_unaligned_le32(key + 20);
	dctx->s[2] = get_unaligned_le32(key + 24);
	dctx->s[3] = get_unaligned_le32(key + 28);
	dctx->buflen = 0;
}
EXPORT_SYMBOL(poly1305_init_arch);

void poly1305_update_arch(struct poly1305_desc_ctx *dctx, const u8 *src,
			  unsigned int nbytes)
{
	bool do_neon = IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
		       crypto_simd_usable();

	if (unlikely(dctx->buflen)) {
		u32 bytes = min(nbytes, POLY1305_BLOCK_SIZE - dctx->buflen);

		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		nbytes -= bytes;
		dctx->buflen += bytes;

		if (dctx->buflen == POLY1305_BLOCK_SIZE) {
			poly1305_blocks_arm(&dctx->h, dctx->buf,
					    POLY1305_BLOCK_SIZE, 1);
			dctx->buflen = 0;
		}
	}

	if (likely(nbytes >= POLY1305_BLOCK_SIZE)) {
		unsigned int len = round_down(nbytes, POLY1305_BLOCK_SIZE);

		if (static_branch_likely(&have_neon) && do_neon) {
			do {
				unsigned int todo = min_t(unsigned int, len, SZ_4K);

				kernel_neon_begin();
				poly1305_blocks_neon(&dctx->h, src, todo, 1);
				kernel_neon_end();

				len -= todo;
				src += todo;
			} while (len);
		} else {
			poly1305_blocks_arm(&dctx->h, src, len, 1);
			src += len;
		}
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
		poly1305_blocks_arm(&dctx->h, dctx->buf, POLY1305_BLOCK_SIZE, 0);
	}

	poly1305_emit_arm(&dctx->h, dst, dctx->s);
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
