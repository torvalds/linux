/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_X86_SERPENT_AVX_H
#define ASM_X86_SERPENT_AVX_H

#include <crypto/b128ops.h>
#include <crypto/serpent.h>
#include <linux/types.h>

struct crypto_skcipher;

#define SERPENT_PARALLEL_BLOCKS 8

asmlinkage void serpent_ecb_enc_8way_avx(const void *ctx, u8 *dst,
					 const u8 *src);
asmlinkage void serpent_ecb_dec_8way_avx(const void *ctx, u8 *dst,
					 const u8 *src);

asmlinkage void serpent_cbc_dec_8way_avx(const void *ctx, u8 *dst,
					 const u8 *src);

#endif
