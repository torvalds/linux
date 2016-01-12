/*
 * aes-ce-cipher.c - core AES cipher using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2013 - 2014 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/neon.h>
#include <crypto/aes.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/module.h>

#include "aes-ce-setkey.h"

MODULE_DESCRIPTION("Synchronous AES cipher using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

struct aes_block {
	u8 b[AES_BLOCK_SIZE];
};

static int num_rounds(struct crypto_aes_ctx *ctx)
{
	/*
	 * # of rounds specified by AES:
	 * 128 bit key		10 rounds
	 * 192 bit key		12 rounds
	 * 256 bit key		14 rounds
	 * => n byte key	=> 6 + (n/4) rounds
	 */
	return 6 + ctx->key_length / 4;
}

static void aes_cipher_encrypt(struct crypto_tfm *tfm, u8 dst[], u8 const src[])
{
	struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	struct aes_block *out = (struct aes_block *)dst;
	struct aes_block const *in = (struct aes_block *)src;
	void *dummy0;
	int dummy1;

	kernel_neon_begin_partial(4);

	__asm__("	ld1	{v0.16b}, %[in]			;"
		"	ld1	{v1.2d}, [%[key]], #16		;"
		"	cmp	%w[rounds], #10			;"
		"	bmi	0f				;"
		"	bne	3f				;"
		"	mov	v3.16b, v1.16b			;"
		"	b	2f				;"
		"0:	mov	v2.16b, v1.16b			;"
		"	ld1	{v3.2d}, [%[key]], #16		;"
		"1:	aese	v0.16b, v2.16b			;"
		"	aesmc	v0.16b, v0.16b			;"
		"2:	ld1	{v1.2d}, [%[key]], #16		;"
		"	aese	v0.16b, v3.16b			;"
		"	aesmc	v0.16b, v0.16b			;"
		"3:	ld1	{v2.2d}, [%[key]], #16		;"
		"	subs	%w[rounds], %w[rounds], #3	;"
		"	aese	v0.16b, v1.16b			;"
		"	aesmc	v0.16b, v0.16b			;"
		"	ld1	{v3.2d}, [%[key]], #16		;"
		"	bpl	1b				;"
		"	aese	v0.16b, v2.16b			;"
		"	eor	v0.16b, v0.16b, v3.16b		;"
		"	st1	{v0.16b}, %[out]		;"

	:	[out]		"=Q"(*out),
		[key]		"=r"(dummy0),
		[rounds]	"=r"(dummy1)
	:	[in]		"Q"(*in),
				"1"(ctx->key_enc),
				"2"(num_rounds(ctx) - 2)
	:	"cc");

	kernel_neon_end();
}

static void aes_cipher_decrypt(struct crypto_tfm *tfm, u8 dst[], u8 const src[])
{
	struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	struct aes_block *out = (struct aes_block *)dst;
	struct aes_block const *in = (struct aes_block *)src;
	void *dummy0;
	int dummy1;

	kernel_neon_begin_partial(4);

	__asm__("	ld1	{v0.16b}, %[in]			;"
		"	ld1	{v1.2d}, [%[key]], #16		;"
		"	cmp	%w[rounds], #10			;"
		"	bmi	0f				;"
		"	bne	3f				;"
		"	mov	v3.16b, v1.16b			;"
		"	b	2f				;"
		"0:	mov	v2.16b, v1.16b			;"
		"	ld1	{v3.2d}, [%[key]], #16		;"
		"1:	aesd	v0.16b, v2.16b			;"
		"	aesimc	v0.16b, v0.16b			;"
		"2:	ld1	{v1.2d}, [%[key]], #16		;"
		"	aesd	v0.16b, v3.16b			;"
		"	aesimc	v0.16b, v0.16b			;"
		"3:	ld1	{v2.2d}, [%[key]], #16		;"
		"	subs	%w[rounds], %w[rounds], #3	;"
		"	aesd	v0.16b, v1.16b			;"
		"	aesimc	v0.16b, v0.16b			;"
		"	ld1	{v3.2d}, [%[key]], #16		;"
		"	bpl	1b				;"
		"	aesd	v0.16b, v2.16b			;"
		"	eor	v0.16b, v0.16b, v3.16b		;"
		"	st1	{v0.16b}, %[out]		;"

	:	[out]		"=Q"(*out),
		[key]		"=r"(dummy0),
		[rounds]	"=r"(dummy1)
	:	[in]		"Q"(*in),
				"1"(ctx->key_dec),
				"2"(num_rounds(ctx) - 2)
	:	"cc");

	kernel_neon_end();
}

/*
 * aes_sub() - use the aese instruction to perform the AES sbox substitution
 *             on each byte in 'input'
 */
static u32 aes_sub(u32 input)
{
	u32 ret;

	__asm__("dup	v1.4s, %w[in]		;"
		"movi	v0.16b, #0		;"
		"aese	v0.16b, v1.16b		;"
		"umov	%w[out], v0.4s[0]	;"

	:	[out]	"=r"(ret)
	:	[in]	"r"(input)
	:		"v0","v1");

	return ret;
}

int ce_aes_expandkey(struct crypto_aes_ctx *ctx, const u8 *in_key,
		     unsigned int key_len)
{
	/*
	 * The AES key schedule round constants
	 */
	static u8 const rcon[] = {
		0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36,
	};

	u32 kwords = key_len / sizeof(u32);
	struct aes_block *key_enc, *key_dec;
	int i, j;

	if (key_len != AES_KEYSIZE_128 &&
	    key_len != AES_KEYSIZE_192 &&
	    key_len != AES_KEYSIZE_256)
		return -EINVAL;

	memcpy(ctx->key_enc, in_key, key_len);
	ctx->key_length = key_len;

	kernel_neon_begin_partial(2);
	for (i = 0; i < sizeof(rcon); i++) {
		u32 *rki = ctx->key_enc + (i * kwords);
		u32 *rko = rki + kwords;

		rko[0] = ror32(aes_sub(rki[kwords - 1]), 8) ^ rcon[i] ^ rki[0];
		rko[1] = rko[0] ^ rki[1];
		rko[2] = rko[1] ^ rki[2];
		rko[3] = rko[2] ^ rki[3];

		if (key_len == AES_KEYSIZE_192) {
			if (i >= 7)
				break;
			rko[4] = rko[3] ^ rki[4];
			rko[5] = rko[4] ^ rki[5];
		} else if (key_len == AES_KEYSIZE_256) {
			if (i >= 6)
				break;
			rko[4] = aes_sub(rko[3]) ^ rki[4];
			rko[5] = rko[4] ^ rki[5];
			rko[6] = rko[5] ^ rki[6];
			rko[7] = rko[6] ^ rki[7];
		}
	}

	/*
	 * Generate the decryption keys for the Equivalent Inverse Cipher.
	 * This involves reversing the order of the round keys, and applying
	 * the Inverse Mix Columns transformation on all but the first and
	 * the last one.
	 */
	key_enc = (struct aes_block *)ctx->key_enc;
	key_dec = (struct aes_block *)ctx->key_dec;
	j = num_rounds(ctx);

	key_dec[0] = key_enc[j];
	for (i = 1, j--; j > 0; i++, j--)
		__asm__("ld1	{v0.16b}, %[in]		;"
			"aesimc	v1.16b, v0.16b		;"
			"st1	{v1.16b}, %[out]	;"

		:	[out]	"=Q"(key_dec[i])
		:	[in]	"Q"(key_enc[j])
		:		"v0","v1");
	key_dec[i] = key_enc[0];

	kernel_neon_end();
	return 0;
}
EXPORT_SYMBOL(ce_aes_expandkey);

int ce_aes_setkey(struct crypto_tfm *tfm, const u8 *in_key,
		  unsigned int key_len)
{
	struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	int ret;

	ret = ce_aes_expandkey(ctx, in_key, key_len);
	if (!ret)
		return 0;

	tfm->crt_flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
	return -EINVAL;
}
EXPORT_SYMBOL(ce_aes_setkey);

static struct crypto_alg aes_alg = {
	.cra_name		= "aes",
	.cra_driver_name	= "aes-ce",
	.cra_priority		= 250,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct crypto_aes_ctx),
	.cra_module		= THIS_MODULE,
	.cra_cipher = {
		.cia_min_keysize	= AES_MIN_KEY_SIZE,
		.cia_max_keysize	= AES_MAX_KEY_SIZE,
		.cia_setkey		= ce_aes_setkey,
		.cia_encrypt		= aes_cipher_encrypt,
		.cia_decrypt		= aes_cipher_decrypt
	}
};

static int __init aes_mod_init(void)
{
	return crypto_register_alg(&aes_alg);
}

static void __exit aes_mod_exit(void)
{
	crypto_unregister_alg(&aes_alg);
}

module_cpu_feature_match(AES, aes_mod_init);
module_exit(aes_mod_exit);
