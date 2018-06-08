/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_X86_TWOFISH_H
#define ASM_X86_TWOFISH_H

#include <linux/crypto.h>
#include <crypto/twofish.h>
#include <crypto/b128ops.h>

/* regular block cipher functions from twofish_x86_64 module */
asmlinkage void twofish_enc_blk(struct twofish_ctx *ctx, u8 *dst,
				const u8 *src);
asmlinkage void twofish_dec_blk(struct twofish_ctx *ctx, u8 *dst,
				const u8 *src);

/* 3-way parallel cipher functions */
asmlinkage void __twofish_enc_blk_3way(struct twofish_ctx *ctx, u8 *dst,
				       const u8 *src, bool xor);
asmlinkage void twofish_dec_blk_3way(struct twofish_ctx *ctx, u8 *dst,
				     const u8 *src);

/* helpers from twofish_x86_64-3way module */
extern void twofish_dec_blk_cbc_3way(void *ctx, u128 *dst, const u128 *src);
extern void twofish_enc_blk_ctr(void *ctx, u128 *dst, const u128 *src,
				le128 *iv);
extern void twofish_enc_blk_ctr_3way(void *ctx, u128 *dst, const u128 *src,
				     le128 *iv);

#endif /* ASM_X86_TWOFISH_H */
