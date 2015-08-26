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

static struct crypto_alg aes_alg = {
	.cra_name		= "aes",
	.cra_driver_name	= "aes-ce",
	.cra_priority		= 299,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct crypto_aes_ctx),
	.cra_module		= THIS_MODULE,
	.cra_cipher = {
		.cia_min_keysize	= AES_MIN_KEY_SIZE,
		.cia_max_keysize	= AES_MAX_KEY_SIZE,
		.cia_setkey		= crypto_aes_set_key,
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
