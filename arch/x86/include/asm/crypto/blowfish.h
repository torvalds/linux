#ifndef ASM_X86_BLOWFISH_H
#define ASM_X86_BLOWFISH_H

#include <linux/crypto.h>
#include <crypto/blowfish.h>

#define BF_PARALLEL_BLOCKS 4

/* regular block cipher functions */
asmlinkage void __blowfish_enc_blk(struct bf_ctx *ctx, u8 *dst, const u8 *src,
				   bool xor);
asmlinkage void blowfish_dec_blk(struct bf_ctx *ctx, u8 *dst, const u8 *src);

/* 4-way parallel cipher functions */
asmlinkage void __blowfish_enc_blk_4way(struct bf_ctx *ctx, u8 *dst,
					const u8 *src, bool xor);
asmlinkage void blowfish_dec_blk_4way(struct bf_ctx *ctx, u8 *dst,
				      const u8 *src);

static inline void blowfish_enc_blk(struct bf_ctx *ctx, u8 *dst, const u8 *src)
{
	__blowfish_enc_blk(ctx, dst, src, false);
}

static inline void blowfish_enc_blk_xor(struct bf_ctx *ctx, u8 *dst,
					const u8 *src)
{
	__blowfish_enc_blk(ctx, dst, src, true);
}

static inline void blowfish_enc_blk_4way(struct bf_ctx *ctx, u8 *dst,
					 const u8 *src)
{
	__blowfish_enc_blk_4way(ctx, dst, src, false);
}

static inline void blowfish_enc_blk_xor_4way(struct bf_ctx *ctx, u8 *dst,
				      const u8 *src)
{
	__blowfish_enc_blk_4way(ctx, dst, src, true);
}

#endif
