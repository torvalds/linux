// SPDX-License-Identifier: GPL-2.0-only
/*
 * Scalar fixed time AES core transform
 *
 * Copyright (C) 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <linux/module.h>

static int aesti_set_key(struct crypto_tfm *tfm, const u8 *in_key,
			 unsigned int key_len)
{
	struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	return aes_expandkey(ctx, in_key, key_len);
}

static void aesti_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	unsigned long flags;

	/*
	 * Temporarily disable interrupts to avoid races where cachelines are
	 * evicted when the CPU is interrupted to do something else.
	 */
	local_irq_save(flags);

	aes_encrypt(ctx, out, in);

	local_irq_restore(flags);
}

static void aesti_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	unsigned long flags;

	/*
	 * Temporarily disable interrupts to avoid races where cachelines are
	 * evicted when the CPU is interrupted to do something else.
	 */
	local_irq_save(flags);

	aes_decrypt(ctx, out, in);

	local_irq_restore(flags);
}

static struct crypto_alg aes_alg = {
	.cra_name			= "aes",
	.cra_driver_name		= "aes-fixed-time",
	.cra_priority			= 100 + 1,
	.cra_flags			= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize			= AES_BLOCK_SIZE,
	.cra_ctxsize			= sizeof(struct crypto_aes_ctx),
	.cra_module			= THIS_MODULE,

	.cra_cipher.cia_min_keysize	= AES_MIN_KEY_SIZE,
	.cra_cipher.cia_max_keysize	= AES_MAX_KEY_SIZE,
	.cra_cipher.cia_setkey		= aesti_set_key,
	.cra_cipher.cia_encrypt		= aesti_encrypt,
	.cra_cipher.cia_decrypt		= aesti_decrypt
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

MODULE_DESCRIPTION("Generic fixed time AES");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
