// SPDX-License-Identifier: GPL-2.0-only
/*
 * AES CTR routines supporting VMX instructions on the Power 8
 *
 * Copyright (C) 2015 International Business Machines Inc.
 *
 * Author: Marcelo Henrique Cerri <mhcerri@br.ibm.com>
 */

#include <asm/simd.h>
#include <asm/switch_to.h>
#include <crypto/aes.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>

#include "aesp8-ppc.h"

struct p8_aes_ctr_ctx {
	struct crypto_skcipher *fallback;
	struct aes_key enc_key;
};

static int p8_aes_ctr_init(struct crypto_skcipher *tfm)
{
	struct p8_aes_ctr_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *fallback;

	fallback = crypto_alloc_skcipher("ctr(aes)", 0,
					 CRYPTO_ALG_NEED_FALLBACK |
					 CRYPTO_ALG_ASYNC);
	if (IS_ERR(fallback)) {
		pr_err("Failed to allocate ctr(aes) fallback: %ld\n",
		       PTR_ERR(fallback));
		return PTR_ERR(fallback);
	}

	crypto_skcipher_set_reqsize(tfm, sizeof(struct skcipher_request) +
				    crypto_skcipher_reqsize(fallback));
	ctx->fallback = fallback;
	return 0;
}

static void p8_aes_ctr_exit(struct crypto_skcipher *tfm)
{
	struct p8_aes_ctr_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(ctx->fallback);
}

static int p8_aes_ctr_setkey(struct crypto_skcipher *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct p8_aes_ctr_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	preempt_disable();
	pagefault_disable();
	enable_kernel_vsx();
	ret = aes_p8_set_encrypt_key(key, keylen * 8, &ctx->enc_key);
	disable_kernel_vsx();
	pagefault_enable();
	preempt_enable();

	ret |= crypto_skcipher_setkey(ctx->fallback, key, keylen);

	return ret ? -EINVAL : 0;
}

static void p8_aes_ctr_final(const struct p8_aes_ctr_ctx *ctx,
			     struct skcipher_walk *walk)
{
	u8 *ctrblk = walk->iv;
	u8 keystream[AES_BLOCK_SIZE];
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;

	preempt_disable();
	pagefault_disable();
	enable_kernel_vsx();
	aes_p8_encrypt(ctrblk, keystream, &ctx->enc_key);
	disable_kernel_vsx();
	pagefault_enable();
	preempt_enable();

	crypto_xor_cpy(dst, keystream, src, nbytes);
	crypto_inc(ctrblk, AES_BLOCK_SIZE);
}

static int p8_aes_ctr_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct p8_aes_ctr_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int ret;

	if (!crypto_simd_usable()) {
		struct skcipher_request *subreq = skcipher_request_ctx(req);

		*subreq = *req;
		skcipher_request_set_tfm(subreq, ctx->fallback);
		return crypto_skcipher_encrypt(subreq);
	}

	ret = skcipher_walk_virt(&walk, req, false);
	while ((nbytes = walk.nbytes) >= AES_BLOCK_SIZE) {
		preempt_disable();
		pagefault_disable();
		enable_kernel_vsx();
		aes_p8_ctr32_encrypt_blocks(walk.src.virt.addr,
					    walk.dst.virt.addr,
					    nbytes / AES_BLOCK_SIZE,
					    &ctx->enc_key, walk.iv);
		disable_kernel_vsx();
		pagefault_enable();
		preempt_enable();

		do {
			crypto_inc(walk.iv, AES_BLOCK_SIZE);
		} while ((nbytes -= AES_BLOCK_SIZE) >= AES_BLOCK_SIZE);

		ret = skcipher_walk_done(&walk, nbytes);
	}
	if (nbytes) {
		p8_aes_ctr_final(ctx, &walk);
		ret = skcipher_walk_done(&walk, 0);
	}
	return ret;
}

struct skcipher_alg p8_aes_ctr_alg = {
	.base.cra_name = "ctr(aes)",
	.base.cra_driver_name = "p8_aes_ctr",
	.base.cra_module = THIS_MODULE,
	.base.cra_priority = 2000,
	.base.cra_flags = CRYPTO_ALG_NEED_FALLBACK,
	.base.cra_blocksize = 1,
	.base.cra_ctxsize = sizeof(struct p8_aes_ctr_ctx),
	.setkey = p8_aes_ctr_setkey,
	.encrypt = p8_aes_ctr_crypt,
	.decrypt = p8_aes_ctr_crypt,
	.init = p8_aes_ctr_init,
	.exit = p8_aes_ctr_exit,
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
	.chunksize = AES_BLOCK_SIZE,
};
