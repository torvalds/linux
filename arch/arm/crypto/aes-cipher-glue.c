// SPDX-License-Identifier: GPL-2.0-only
/*
 * Scalar AES core transform
 *
 * Copyright (C) 2017 Linaro Ltd.
 * Author: Ard Biesheuvel <ard.biesheuvel@linaro.org>
 */

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <linux/module.h>
#include "aes-cipher.h"

EXPORT_SYMBOL_GPL(__aes_arm_encrypt);
EXPORT_SYMBOL_GPL(__aes_arm_decrypt);

static void aes_arm_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	int rounds = 6 + ctx->key_length / 4;

	__aes_arm_encrypt(ctx->key_enc, rounds, in, out);
}

static void aes_arm_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	int rounds = 6 + ctx->key_length / 4;

	__aes_arm_decrypt(ctx->key_dec, rounds, in, out);
}

static struct crypto_alg aes_alg = {
	.cra_name			= "aes",
	.cra_driver_name		= "aes-arm",
	.cra_priority			= 200,
	.cra_flags			= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize			= AES_BLOCK_SIZE,
	.cra_ctxsize			= sizeof(struct crypto_aes_ctx),
	.cra_module			= THIS_MODULE,

	.cra_cipher.cia_min_keysize	= AES_MIN_KEY_SIZE,
	.cra_cipher.cia_max_keysize	= AES_MAX_KEY_SIZE,
	.cra_cipher.cia_setkey		= crypto_aes_set_key,
	.cra_cipher.cia_encrypt		= aes_arm_encrypt,
	.cra_cipher.cia_decrypt		= aes_arm_decrypt,

#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	.cra_alignmask			= 3,
#endif
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

MODULE_DESCRIPTION("Scalar AES cipher for ARM");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("aes");
