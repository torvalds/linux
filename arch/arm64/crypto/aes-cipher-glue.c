// SPDX-License-Identifier: GPL-2.0-only
/*
 * Scalar AES core transform
 *
 * Copyright (C) 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <linux/module.h>

asmlinkage void __aes_arm64_encrypt(u32 *rk, u8 *out, const u8 *in, int rounds);
asmlinkage void __aes_arm64_decrypt(u32 *rk, u8 *out, const u8 *in, int rounds);

static void aes_arm64_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	int rounds = 6 + ctx->key_length / 4;

	__aes_arm64_encrypt(ctx->key_enc, out, in, rounds);
}

static void aes_arm64_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	int rounds = 6 + ctx->key_length / 4;

	__aes_arm64_decrypt(ctx->key_dec, out, in, rounds);
}

static struct crypto_alg aes_alg = {
	.cra_name			= "aes",
	.cra_driver_name		= "aes-arm64",
	.cra_priority			= 200,
	.cra_flags			= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize			= AES_BLOCK_SIZE,
	.cra_ctxsize			= sizeof(struct crypto_aes_ctx),
	.cra_module			= THIS_MODULE,

	.cra_cipher.cia_min_keysize	= AES_MIN_KEY_SIZE,
	.cra_cipher.cia_max_keysize	= AES_MAX_KEY_SIZE,
	.cra_cipher.cia_setkey		= crypto_aes_set_key,
	.cra_cipher.cia_encrypt		= aes_arm64_encrypt,
	.cra_cipher.cia_decrypt		= aes_arm64_decrypt
};

static int __init aes_init(void)
{
	return crypto_register_alg(&aes_alg);
}

static void __exit aes_fini(void)
{
	crypto_unregister_alg(&aes_alg);
}

module_init(aes_init);
module_exit(aes_fini);

MODULE_DESCRIPTION("Scalar AES cipher for arm64");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("aes");
