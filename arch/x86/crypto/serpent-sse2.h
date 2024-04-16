/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_X86_SERPENT_SSE2_H
#define ASM_X86_SERPENT_SSE2_H

#include <linux/crypto.h>
#include <crypto/serpent.h>

#ifdef CONFIG_X86_32

#define SERPENT_PARALLEL_BLOCKS 4

asmlinkage void __serpent_enc_blk_4way(const struct serpent_ctx *ctx, u8 *dst,
				       const u8 *src, bool xor);
asmlinkage void serpent_dec_blk_4way(const struct serpent_ctx *ctx, u8 *dst,
				     const u8 *src);

static inline void serpent_enc_blk_xway(const void *ctx, u8 *dst, const u8 *src)
{
	__serpent_enc_blk_4way(ctx, dst, src, false);
}

static inline void serpent_enc_blk_xway_xor(const struct serpent_ctx *ctx,
					    u8 *dst, const u8 *src)
{
	__serpent_enc_blk_4way(ctx, dst, src, true);
}

static inline void serpent_dec_blk_xway(const void *ctx, u8 *dst, const u8 *src)
{
	serpent_dec_blk_4way(ctx, dst, src);
}

#else

#define SERPENT_PARALLEL_BLOCKS 8

asmlinkage void __serpent_enc_blk_8way(const struct serpent_ctx *ctx, u8 *dst,
				       const u8 *src, bool xor);
asmlinkage void serpent_dec_blk_8way(const struct serpent_ctx *ctx, u8 *dst,
				     const u8 *src);

static inline void serpent_enc_blk_xway(const void *ctx, u8 *dst, const u8 *src)
{
	__serpent_enc_blk_8way(ctx, dst, src, false);
}

static inline void serpent_enc_blk_xway_xor(const struct serpent_ctx *ctx,
					    u8 *dst, const u8 *src)
{
	__serpent_enc_blk_8way(ctx, dst, src, true);
}

static inline void serpent_dec_blk_xway(const void *ctx, u8 *dst, const u8 *src)
{
	serpent_dec_blk_8way(ctx, dst, src);
}

#endif

#endif
