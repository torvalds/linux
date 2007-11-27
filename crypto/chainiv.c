/*
 * chainiv: Chain IV Generator
 *
 * Generate IVs simply be using the last block of the previous encryption.
 * This is mainly useful for CBC with a synchronous algorithm.
 *
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/string.h>

struct chainiv_ctx {
	spinlock_t lock;
	char iv[];
};

static int chainiv_givencrypt(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct chainiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);
	struct ablkcipher_request *subreq = skcipher_givcrypt_reqctx(req);
	unsigned int ivsize;
	int err;

	ablkcipher_request_set_tfm(subreq, skcipher_geniv_cipher(geniv));
	ablkcipher_request_set_callback(subreq, req->creq.base.flags &
						~CRYPTO_TFM_REQ_MAY_SLEEP,
					req->creq.base.complete,
					req->creq.base.data);
	ablkcipher_request_set_crypt(subreq, req->creq.src, req->creq.dst,
				     req->creq.nbytes, req->creq.info);

	spin_lock_bh(&ctx->lock);

	ivsize = crypto_ablkcipher_ivsize(geniv);

	memcpy(req->giv, ctx->iv, ivsize);
	memcpy(subreq->info, ctx->iv, ivsize);

	err = crypto_ablkcipher_encrypt(subreq);
	if (err)
		goto unlock;

	memcpy(ctx->iv, subreq->info, ivsize);

unlock:
	spin_unlock_bh(&ctx->lock);

	return err;
}

static int chainiv_givencrypt_first(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct chainiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);

	spin_lock_bh(&ctx->lock);
	if (crypto_ablkcipher_crt(geniv)->givencrypt !=
	    chainiv_givencrypt_first)
		goto unlock;

	crypto_ablkcipher_crt(geniv)->givencrypt = chainiv_givencrypt;
	get_random_bytes(ctx->iv, crypto_ablkcipher_ivsize(geniv));

unlock:
	spin_unlock_bh(&ctx->lock);

	return chainiv_givencrypt(req);
}

static int chainiv_init(struct crypto_tfm *tfm)
{
	struct crypto_ablkcipher *geniv = __crypto_ablkcipher_cast(tfm);
	struct chainiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);

	spin_lock_init(&ctx->lock);

	tfm->crt_ablkcipher.reqsize = sizeof(struct ablkcipher_request);

	return skcipher_geniv_init(tfm);
}

static struct crypto_template chainiv_tmpl;

static struct crypto_instance *chainiv_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;

	inst = skcipher_geniv_alloc(&chainiv_tmpl, tb, 0,
				    CRYPTO_ALG_ASYNC);
	if (IS_ERR(inst))
		goto out;

	inst->alg.cra_ablkcipher.givencrypt = chainiv_givencrypt_first;

	inst->alg.cra_init = chainiv_init;
	inst->alg.cra_exit = skcipher_geniv_exit;

	inst->alg.cra_ctxsize = sizeof(struct chainiv_ctx) +
				inst->alg.cra_ablkcipher.ivsize;

out:
	return inst;
}

static struct crypto_template chainiv_tmpl = {
	.name = "chainiv",
	.alloc = chainiv_alloc,
	.free = skcipher_geniv_free,
	.module = THIS_MODULE,
};

static int __init chainiv_module_init(void)
{
	return crypto_register_template(&chainiv_tmpl);
}

static void __exit chainiv_module_exit(void)
{
	crypto_unregister_template(&chainiv_tmpl);
}

module_init(chainiv_module_init);
module_exit(chainiv_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Chain IV Generator");
