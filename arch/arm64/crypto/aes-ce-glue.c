// SPDX-License-Identifier: GPL-2.0-only
/*
 * aes-ce-cipher.c - core AES cipher using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2013 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <asm/simd.h>
#include <asm/unaligned.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/internal/simd.h>
#include <linux/cpufeature.h>
#include <linux/module.h>

#include "aes-ce-setkey.h"

MODULE_DESCRIPTION("Synchronous AES cipher using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

struct aes_block {
	u8 b[AES_BLOCK_SIZE];
};

asmlinkage void __aes_ce_encrypt(u32 *rk, u8 *out, const u8 *in, int rounds);
asmlinkage void __aes_ce_decrypt(u32 *rk, u8 *out, const u8 *in, int rounds);

asmlinkage u32 __aes_ce_sub(u32 l);
asmlinkage void __aes_ce_invert(struct aes_block *out,
				const struct aes_block *in);

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

	if (!crypto_simd_usable()) {
		aes_encrypt(ctx, dst, src);
		return;
	}

	kernel_neon_begin();
	__aes_ce_encrypt(ctx->key_enc, dst, src, num_rounds(ctx));
	kernel_neon_end();
}

static void aes_cipher_decrypt(struct crypto_tfm *tfm, u8 dst[], u8 const src[])
{
	struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	if (!crypto_simd_usable()) {
		aes_decrypt(ctx, dst, src);
		return;
	}

	kernel_neon_begin();
	__aes_ce_decrypt(ctx->key_dec, dst, src, num_rounds(ctx));
	kernel_neon_end();
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

	ctx->key_length = key_len;
	for (i = 0; i < kwords; i++)
		ctx->key_enc[i] = get_unaligned_le32(in_key + i * sizeof(u32));

	kernel_neon_begin();
	for (i = 0; i < sizeof(rcon); i++) {
		u32 *rki = ctx->key_enc + (i * kwords);
		u32 *rko = rki + kwords;

		rko[0] = ror32(__aes_ce_sub(rki[kwords - 1]), 8) ^ rcon[i] ^ rki[0];
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
			rko[4] = __aes_ce_sub(rko[3]) ^ rki[4];
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
		__aes_ce_invert(key_dec + i, key_enc + j);
	key_dec[i] = key_enc[0];

	kernel_neon_end();
	return 0;
}
EXPORT_SYMBOL(ce_aes_expandkey);

int ce_aes_setkey(struct crypto_tfm *tfm, const u8 *in_key,
		  unsigned int key_len)
{
	struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	return ce_aes_expandkey(ctx, in_key, key_len);
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
