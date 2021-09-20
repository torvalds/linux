/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ASM_X86_SM4_AVX_H
#define ASM_X86_SM4_AVX_H

#include <linux/types.h>
#include <crypto/sm4.h>

typedef void (*sm4_crypt_func)(const u32 *rk, u8 *dst, const u8 *src, u8 *iv);

int sm4_avx_ecb_encrypt(struct skcipher_request *req);
int sm4_avx_ecb_decrypt(struct skcipher_request *req);

int sm4_cbc_encrypt(struct skcipher_request *req);
int sm4_avx_cbc_decrypt(struct skcipher_request *req,
			unsigned int bsize, sm4_crypt_func func);

int sm4_cfb_encrypt(struct skcipher_request *req);
int sm4_avx_cfb_decrypt(struct skcipher_request *req,
			unsigned int bsize, sm4_crypt_func func);

int sm4_avx_ctr_crypt(struct skcipher_request *req,
			unsigned int bsize, sm4_crypt_func func);

#endif
