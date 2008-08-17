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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/workqueue.h>

enum {
	CHAINIV_STATE_INUSE = 0,
};

struct chainiv_ctx {
	spinlock_t lock;
	char iv[];
};

struct async_chainiv_ctx {
	unsigned long state;

	spinlock_t lock;
	int err;

	struct crypto_queue queue;
	struct work_struct postponed;

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

static int chainiv_init_common(struct crypto_tfm *tfm)
{
	tfm->crt_ablkcipher.reqsize = sizeof(struct ablkcipher_request);

	return skcipher_geniv_init(tfm);
}

static int chainiv_init(struct crypto_tfm *tfm)
{
	struct chainiv_ctx *ctx = crypto_tfm_ctx(tfm);

	spin_lock_init(&ctx->lock);

	return chainiv_init_common(tfm);
}

static int async_chainiv_schedule_work(struct async_chainiv_ctx *ctx)
{
	int queued;
	int err = ctx->err;

	if (!ctx->queue.qlen) {
		smp_mb__before_clear_bit();
		clear_bit(CHAINIV_STATE_INUSE, &ctx->state);

		if (!ctx->queue.qlen ||
		    test_and_set_bit(CHAINIV_STATE_INUSE, &ctx->state))
			goto out;
	}

	queued = schedule_work(&ctx->postponed);
	BUG_ON(!queued);

out:
	return err;
}

static int async_chainiv_postpone_request(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct async_chainiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);
	int err;

	spin_lock_bh(&ctx->lock);
	err = skcipher_enqueue_givcrypt(&ctx->queue, req);
	spin_unlock_bh(&ctx->lock);

	if (test_and_set_bit(CHAINIV_STATE_INUSE, &ctx->state))
		return err;

	ctx->err = err;
	return async_chainiv_schedule_work(ctx);
}

static int async_chainiv_givencrypt_tail(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct async_chainiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);
	struct ablkcipher_request *subreq = skcipher_givcrypt_reqctx(req);
	unsigned int ivsize = crypto_ablkcipher_ivsize(geniv);

	memcpy(req->giv, ctx->iv, ivsize);
	memcpy(subreq->info, ctx->iv, ivsize);

	ctx->err = crypto_ablkcipher_encrypt(subreq);
	if (ctx->err)
		goto out;

	memcpy(ctx->iv, subreq->info, ivsize);

out:
	return async_chainiv_schedule_work(ctx);
}

static int async_chainiv_givencrypt(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct async_chainiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);
	struct ablkcipher_request *subreq = skcipher_givcrypt_reqctx(req);

	ablkcipher_request_set_tfm(subreq, skcipher_geniv_cipher(geniv));
	ablkcipher_request_set_callback(subreq, req->creq.base.flags,
					req->creq.base.complete,
					req->creq.base.data);
	ablkcipher_request_set_crypt(subreq, req->creq.src, req->creq.dst,
				     req->creq.nbytes, req->creq.info);

	if (test_and_set_bit(CHAINIV_STATE_INUSE, &ctx->state))
		goto postpone;

	if (ctx->queue.qlen) {
		clear_bit(CHAINIV_STATE_INUSE, &ctx->state);
		goto postpone;
	}

	return async_chainiv_givencrypt_tail(req);

postpone:
	return async_chainiv_postpone_request(req);
}

static int async_chainiv_givencrypt_first(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct async_chainiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);

	if (test_and_set_bit(CHAINIV_STATE_INUSE, &ctx->state))
		goto out;

	if (crypto_ablkcipher_crt(geniv)->givencrypt !=
	    async_chainiv_givencrypt_first)
		goto unlock;

	crypto_ablkcipher_crt(geniv)->givencrypt = async_chainiv_givencrypt;
	get_random_bytes(ctx->iv, crypto_ablkcipher_ivsize(geniv));

unlock:
	clear_bit(CHAINIV_STATE_INUSE, &ctx->state);

out:
	return async_chainiv_givencrypt(req);
}

static void async_chainiv_do_postponed(struct work_struct *work)
{
	struct async_chainiv_ctx *ctx = container_of(work,
						     struct async_chainiv_ctx,
						     postponed);
	struct skcipher_givcrypt_request *req;
	struct ablkcipher_request *subreq;
	int err;

	/* Only handle one request at a time to avoid hogging keventd. */
	spin_lock_bh(&ctx->lock);
	req = skcipher_dequeue_givcrypt(&ctx->queue);
	spin_unlock_bh(&ctx->lock);

	if (!req) {
		async_chainiv_schedule_work(ctx);
		return;
	}

	subreq = skcipher_givcrypt_reqctx(req);
	subreq->base.flags |= CRYPTO_TFM_REQ_MAY_SLEEP;

	err = async_chainiv_givencrypt_tail(req);

	local_bh_disable();
	skcipher_givcrypt_complete(req, err);
	local_bh_enable();
}

static int async_chainiv_init(struct crypto_tfm *tfm)
{
	struct async_chainiv_ctx *ctx = crypto_tfm_ctx(tfm);

	spin_lock_init(&ctx->lock);

	crypto_init_queue(&ctx->queue, 100);
	INIT_WORK(&ctx->postponed, async_chainiv_do_postponed);

	return chainiv_init_common(tfm);
}

static void async_chainiv_exit(struct crypto_tfm *tfm)
{
	struct async_chainiv_ctx *ctx = crypto_tfm_ctx(tfm);

	BUG_ON(test_bit(CHAINIV_STATE_INUSE, &ctx->state) || ctx->queue.qlen);

	skcipher_geniv_exit(tfm);
}

static struct crypto_template chainiv_tmpl;

static struct crypto_instance *chainiv_alloc(struct rtattr **tb)
{
	struct crypto_attr_type *algt;
	struct crypto_instance *inst;
	int err;

	algt = crypto_get_attr_type(tb);
	err = PTR_ERR(algt);
	if (IS_ERR(algt))
		return ERR_PTR(err);

	inst = skcipher_geniv_alloc(&chainiv_tmpl, tb, 0, 0);
	if (IS_ERR(inst))
		goto out;

	inst->alg.cra_ablkcipher.givencrypt = chainiv_givencrypt_first;

	inst->alg.cra_init = chainiv_init;
	inst->alg.cra_exit = skcipher_geniv_exit;

	inst->alg.cra_ctxsize = sizeof(struct chainiv_ctx);

	if (!crypto_requires_sync(algt->type, algt->mask)) {
		inst->alg.cra_flags |= CRYPTO_ALG_ASYNC;

		inst->alg.cra_ablkcipher.givencrypt =
			async_chainiv_givencrypt_first;

		inst->alg.cra_init = async_chainiv_init;
		inst->alg.cra_exit = async_chainiv_exit;

		inst->alg.cra_ctxsize = sizeof(struct async_chainiv_ctx);
	}

	inst->alg.cra_ctxsize += inst->alg.cra_ablkcipher.ivsize;

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

static void chainiv_module_exit(void)
{
	crypto_unregister_template(&chainiv_tmpl);
}

module_init(chainiv_module_init);
module_exit(chainiv_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Chain IV Generator");
