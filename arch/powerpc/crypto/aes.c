// SPDX-License-Identifier: GPL-2.0-only
/*
 * AES routines supporting VMX instructions on the Power 8
 *
 * Copyright (C) 2015 International Business Machines Inc.
 *
 * Author: Marcelo Henrique Cerri <mhcerri@br.ibm.com>
 */

#include <asm/simd.h>
#include <asm/switch_to.h>
#include <crypto/aes.h>
#include <crypto/internal/cipher.h>
#include <crypto/internal/simd.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "aesp8-ppc.h"

struct p8_aes_ctx {
	struct crypto_cipher *fallback;
	struct aes_key enc_key;
	struct aes_key dec_key;
};

static int p8_aes_init(struct crypto_tfm *tfm)
{
	const char *alg = crypto_tfm_alg_name(tfm);
	struct crypto_cipher *fallback;
	struct p8_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	fallback = crypto_alloc_cipher(alg, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(fallback)) {
		printk(KERN_ERR
		       "Failed to allocate transformation for '%s': %ld\n",
		       alg, PTR_ERR(fallback));
		return PTR_ERR(fallback);
	}

	crypto_cipher_set_flags(fallback,
				crypto_cipher_get_flags((struct
							 crypto_cipher *)
							tfm));
	ctx->fallback = fallback;

	return 0;
}

static void p8_aes_exit(struct crypto_tfm *tfm)
{
	struct p8_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	if (ctx->fallback) {
		crypto_free_cipher(ctx->fallback);
		ctx->fallback = NULL;
	}
}

static int p8_aes_setkey(struct crypto_tfm *tfm, const u8 *key,
			 unsigned int keylen)
{
	int ret;
	struct p8_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	preempt_disable();
	pagefault_disable();
	enable_kernel_vsx();
	ret = aes_p8_set_encrypt_key(key, keylen * 8, &ctx->enc_key);
	ret |= aes_p8_set_decrypt_key(key, keylen * 8, &ctx->dec_key);
	disable_kernel_vsx();
	pagefault_enable();
	preempt_enable();

	ret |= crypto_cipher_setkey(ctx->fallback, key, keylen);

	return ret ? -EINVAL : 0;
}

static void p8_aes_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct p8_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	if (!crypto_simd_usable()) {
		crypto_cipher_encrypt_one(ctx->fallback, dst, src);
	} else {
		preempt_disable();
		pagefault_disable();
		enable_kernel_vsx();
		aes_p8_encrypt(src, dst, &ctx->enc_key);
		disable_kernel_vsx();
		pagefault_enable();
		preempt_enable();
	}
}

static void p8_aes_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct p8_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	if (!crypto_simd_usable()) {
		crypto_cipher_decrypt_one(ctx->fallback, dst, src);
	} else {
		preempt_disable();
		pagefault_disable();
		enable_kernel_vsx();
		aes_p8_decrypt(src, dst, &ctx->dec_key);
		disable_kernel_vsx();
		pagefault_enable();
		preempt_enable();
	}
}

struct crypto_alg p8_aes_alg = {
	.cra_name = "aes",
	.cra_driver_name = "p8_aes",
	.cra_module = THIS_MODULE,
	.cra_priority = 1000,
	.cra_type = NULL,
	.cra_flags = CRYPTO_ALG_TYPE_CIPHER | CRYPTO_ALG_NEED_FALLBACK,
	.cra_alignmask = 0,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct p8_aes_ctx),
	.cra_init = p8_aes_init,
	.cra_exit = p8_aes_exit,
	.cra_cipher = {
		       .cia_min_keysize = AES_MIN_KEY_SIZE,
		       .cia_max_keysize = AES_MAX_KEY_SIZE,
		       .cia_setkey = p8_aes_setkey,
		       .cia_encrypt = p8_aes_encrypt,
		       .cia_decrypt = p8_aes_decrypt,
	},
};
