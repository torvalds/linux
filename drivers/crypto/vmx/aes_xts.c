// SPDX-License-Identifier: GPL-2.0-only
/**
 * AES XTS routines supporting VMX In-core instructions on Power 8
 *
 * Copyright (C) 2015 International Business Machines Inc.
 *
 * Author: Leonidas S. Barbosa <leosilva@linux.vnet.ibm.com>
 */

#include <asm/simd.h>
#include <asm/switch_to.h>
#include <crypto/aes.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <crypto/xts.h>

#include "aesp8-ppc.h"

struct p8_aes_xts_ctx {
	struct crypto_skcipher *fallback;
	struct aes_key enc_key;
	struct aes_key dec_key;
	struct aes_key tweak_key;
};

static int p8_aes_xts_init(struct crypto_skcipher *tfm)
{
	struct p8_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *fallback;

	fallback = crypto_alloc_skcipher("xts(aes)", 0,
					 CRYPTO_ALG_NEED_FALLBACK |
					 CRYPTO_ALG_ASYNC);
	if (IS_ERR(fallback)) {
		pr_err("Failed to allocate xts(aes) fallback: %ld\n",
		       PTR_ERR(fallback));
		return PTR_ERR(fallback);
	}

	crypto_skcipher_set_reqsize(tfm, sizeof(struct skcipher_request) +
				    crypto_skcipher_reqsize(fallback));
	ctx->fallback = fallback;
	return 0;
}

static void p8_aes_xts_exit(struct crypto_skcipher *tfm)
{
	struct p8_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(ctx->fallback);
}

static int p8_aes_xts_setkey(struct crypto_skcipher *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct p8_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	ret = xts_verify_key(tfm, key, keylen);
	if (ret)
		return ret;

	preempt_disable();
	pagefault_disable();
	enable_kernel_vsx();
	ret = aes_p8_set_encrypt_key(key + keylen/2, (keylen/2) * 8, &ctx->tweak_key);
	ret |= aes_p8_set_encrypt_key(key, (keylen/2) * 8, &ctx->enc_key);
	ret |= aes_p8_set_decrypt_key(key, (keylen/2) * 8, &ctx->dec_key);
	disable_kernel_vsx();
	pagefault_enable();
	preempt_enable();

	ret |= crypto_skcipher_setkey(ctx->fallback, key, keylen);

	return ret ? -EINVAL : 0;
}

static int p8_aes_xts_crypt(struct skcipher_request *req, int enc)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct p8_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	u8 tweak[AES_BLOCK_SIZE];
	int ret;

	if (!crypto_simd_usable() || (req->cryptlen % XTS_BLOCK_SIZE) != 0) {
		struct skcipher_request *subreq = skcipher_request_ctx(req);

		*subreq = *req;
		skcipher_request_set_tfm(subreq, ctx->fallback);
		return enc ? crypto_skcipher_encrypt(subreq) :
			     crypto_skcipher_decrypt(subreq);
	}

	ret = skcipher_walk_virt(&walk, req, false);
	if (ret)
		return ret;

	preempt_disable();
	pagefault_disable();
	enable_kernel_vsx();

	aes_p8_encrypt(walk.iv, tweak, &ctx->tweak_key);

	disable_kernel_vsx();
	pagefault_enable();
	preempt_enable();

	while ((nbytes = walk.nbytes) != 0) {
		preempt_disable();
		pagefault_disable();
		enable_kernel_vsx();
		if (enc)
			aes_p8_xts_encrypt(walk.src.virt.addr,
					   walk.dst.virt.addr,
					   round_down(nbytes, AES_BLOCK_SIZE),
					   &ctx->enc_key, NULL, tweak);
		else
			aes_p8_xts_decrypt(walk.src.virt.addr,
					   walk.dst.virt.addr,
					   round_down(nbytes, AES_BLOCK_SIZE),
					   &ctx->dec_key, NULL, tweak);
		disable_kernel_vsx();
		pagefault_enable();
		preempt_enable();

		ret = skcipher_walk_done(&walk, nbytes % AES_BLOCK_SIZE);
	}
	return ret;
}

static int p8_aes_xts_encrypt(struct skcipher_request *req)
{
	return p8_aes_xts_crypt(req, 1);
}

static int p8_aes_xts_decrypt(struct skcipher_request *req)
{
	return p8_aes_xts_crypt(req, 0);
}

struct skcipher_alg p8_aes_xts_alg = {
	.base.cra_name = "xts(aes)",
	.base.cra_driver_name = "p8_aes_xts",
	.base.cra_module = THIS_MODULE,
	.base.cra_priority = 2000,
	.base.cra_flags = CRYPTO_ALG_NEED_FALLBACK,
	.base.cra_blocksize = AES_BLOCK_SIZE,
	.base.cra_ctxsize = sizeof(struct p8_aes_xts_ctx),
	.setkey = p8_aes_xts_setkey,
	.encrypt = p8_aes_xts_encrypt,
	.decrypt = p8_aes_xts_decrypt,
	.init = p8_aes_xts_init,
	.exit = p8_aes_xts_exit,
	.min_keysize = 2 * AES_MIN_KEY_SIZE,
	.max_keysize = 2 * AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
};
