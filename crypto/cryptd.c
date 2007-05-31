/*
 * Software async crypto daemon.
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/algapi.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define CRYPTD_MAX_QLEN 100

struct cryptd_state {
	spinlock_t lock;
	struct mutex mutex;
	struct crypto_queue queue;
	struct task_struct *task;
};

struct cryptd_instance_ctx {
	struct crypto_spawn spawn;
	struct cryptd_state *state;
};

struct cryptd_blkcipher_ctx {
	struct crypto_blkcipher *child;
};

struct cryptd_blkcipher_request_ctx {
	crypto_completion_t complete;
};


static inline struct cryptd_state *cryptd_get_state(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct cryptd_instance_ctx *ictx = crypto_instance_ctx(inst);
	return ictx->state;
}

static int cryptd_blkcipher_setkey(struct crypto_ablkcipher *parent,
				   const u8 *key, unsigned int keylen)
{
	struct cryptd_blkcipher_ctx *ctx = crypto_ablkcipher_ctx(parent);
	struct crypto_blkcipher *child = ctx->child;
	int err;

	crypto_blkcipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_blkcipher_set_flags(child, crypto_ablkcipher_get_flags(parent) &
					  CRYPTO_TFM_REQ_MASK);
	err = crypto_blkcipher_setkey(child, key, keylen);
	crypto_ablkcipher_set_flags(parent, crypto_blkcipher_get_flags(child) &
					    CRYPTO_TFM_RES_MASK);
	return err;
}

static void cryptd_blkcipher_crypt(struct ablkcipher_request *req,
				   struct crypto_blkcipher *child,
				   int err,
				   int (*crypt)(struct blkcipher_desc *desc,
						struct scatterlist *dst,
						struct scatterlist *src,
						unsigned int len))
{
	struct cryptd_blkcipher_request_ctx *rctx;
	struct blkcipher_desc desc;

	rctx = ablkcipher_request_ctx(req);

	if (unlikely(err == -EINPROGRESS)) {
		rctx->complete(&req->base, err);
		return;
	}

	desc.tfm = child;
	desc.info = req->info;
	desc.flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	err = crypt(&desc, req->dst, req->src, req->nbytes);

	req->base.complete = rctx->complete;

	local_bh_disable();
	req->base.complete(&req->base, err);
	local_bh_enable();
}

static void cryptd_blkcipher_encrypt(struct crypto_async_request *req, int err)
{
	struct cryptd_blkcipher_ctx *ctx = crypto_tfm_ctx(req->tfm);
	struct crypto_blkcipher *child = ctx->child;

	cryptd_blkcipher_crypt(ablkcipher_request_cast(req), child, err,
			       crypto_blkcipher_crt(child)->encrypt);
}

static void cryptd_blkcipher_decrypt(struct crypto_async_request *req, int err)
{
	struct cryptd_blkcipher_ctx *ctx = crypto_tfm_ctx(req->tfm);
	struct crypto_blkcipher *child = ctx->child;

	cryptd_blkcipher_crypt(ablkcipher_request_cast(req), child, err,
			       crypto_blkcipher_crt(child)->decrypt);
}

static int cryptd_blkcipher_enqueue(struct ablkcipher_request *req,
				    crypto_completion_t complete)
{
	struct cryptd_blkcipher_request_ctx *rctx = ablkcipher_request_ctx(req);
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct cryptd_state *state =
		cryptd_get_state(crypto_ablkcipher_tfm(tfm));
	int err;

	rctx->complete = req->base.complete;
	req->base.complete = complete;

	spin_lock_bh(&state->lock);
	err = ablkcipher_enqueue_request(crypto_ablkcipher_alg(tfm), req);
	spin_unlock_bh(&state->lock);

	wake_up_process(state->task);
	return err;
}

static int cryptd_blkcipher_encrypt_enqueue(struct ablkcipher_request *req)
{
	return cryptd_blkcipher_enqueue(req, cryptd_blkcipher_encrypt);
}

static int cryptd_blkcipher_decrypt_enqueue(struct ablkcipher_request *req)
{
	return cryptd_blkcipher_enqueue(req, cryptd_blkcipher_decrypt);
}

static int cryptd_blkcipher_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct cryptd_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct crypto_spawn *spawn = &ictx->spawn;
	struct cryptd_blkcipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_blkcipher *cipher;

	cipher = crypto_spawn_blkcipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;
	tfm->crt_ablkcipher.reqsize =
		sizeof(struct cryptd_blkcipher_request_ctx);
	return 0;
}

static void cryptd_blkcipher_exit_tfm(struct crypto_tfm *tfm)
{
	struct cryptd_blkcipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct cryptd_state *state = cryptd_get_state(tfm);
	int active;

	mutex_lock(&state->mutex);
	active = ablkcipher_tfm_in_queue(__crypto_ablkcipher_cast(tfm));
	mutex_unlock(&state->mutex);

	BUG_ON(active);

	crypto_free_blkcipher(ctx->child);
}

static struct crypto_instance *cryptd_alloc_instance(struct crypto_alg *alg,
						     struct cryptd_state *state)
{
	struct crypto_instance *inst;
	struct cryptd_instance_ctx *ctx;
	int err;

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (IS_ERR(inst))
		goto out;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "cryptd(%s)", alg->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto out_free_inst;

	ctx = crypto_instance_ctx(inst);
	err = crypto_init_spawn(&ctx->spawn, alg, inst,
				CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_ASYNC);
	if (err)
		goto out_free_inst;

	ctx->state = state;

	memcpy(inst->alg.cra_name, alg->cra_name, CRYPTO_MAX_ALG_NAME);

	inst->alg.cra_priority = alg->cra_priority + 50;
	inst->alg.cra_blocksize = alg->cra_blocksize;
	inst->alg.cra_alignmask = alg->cra_alignmask;

out:
	return inst;

out_free_inst:
	kfree(inst);
	inst = ERR_PTR(err);
	goto out;
}

static struct crypto_instance *cryptd_alloc_blkcipher(
	struct rtattr **tb, struct cryptd_state *state)
{
	struct crypto_instance *inst;
	struct crypto_alg *alg;

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_BLKCIPHER,
				  CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_ASYNC);
	if (IS_ERR(alg))
		return ERR_PTR(PTR_ERR(alg));

	inst = cryptd_alloc_instance(alg, state);
	if (IS_ERR(inst))
		goto out_put_alg;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_BLKCIPHER | CRYPTO_ALG_ASYNC;
	inst->alg.cra_type = &crypto_ablkcipher_type;

	inst->alg.cra_ablkcipher.ivsize = alg->cra_blkcipher.ivsize;
	inst->alg.cra_ablkcipher.min_keysize = alg->cra_blkcipher.min_keysize;
	inst->alg.cra_ablkcipher.max_keysize = alg->cra_blkcipher.max_keysize;

	inst->alg.cra_ctxsize = sizeof(struct cryptd_blkcipher_ctx);

	inst->alg.cra_init = cryptd_blkcipher_init_tfm;
	inst->alg.cra_exit = cryptd_blkcipher_exit_tfm;

	inst->alg.cra_ablkcipher.setkey = cryptd_blkcipher_setkey;
	inst->alg.cra_ablkcipher.encrypt = cryptd_blkcipher_encrypt_enqueue;
	inst->alg.cra_ablkcipher.decrypt = cryptd_blkcipher_decrypt_enqueue;

	inst->alg.cra_ablkcipher.queue = &state->queue;

out_put_alg:
	crypto_mod_put(alg);
	return inst;
}

static struct cryptd_state state;

static struct crypto_instance *cryptd_alloc(struct rtattr **tb)
{
	struct crypto_attr_type *algt;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return ERR_PTR(PTR_ERR(algt));

	switch (algt->type & algt->mask & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_BLKCIPHER:
		return cryptd_alloc_blkcipher(tb, &state);
	}

	return ERR_PTR(-EINVAL);
}

static void cryptd_free(struct crypto_instance *inst)
{
	struct cryptd_instance_ctx *ctx = crypto_instance_ctx(inst);

	crypto_drop_spawn(&ctx->spawn);
	kfree(inst);
}

static struct crypto_template cryptd_tmpl = {
	.name = "cryptd",
	.alloc = cryptd_alloc,
	.free = cryptd_free,
	.module = THIS_MODULE,
};

static inline int cryptd_create_thread(struct cryptd_state *state,
				       int (*fn)(void *data), const char *name)
{
	spin_lock_init(&state->lock);
	mutex_init(&state->mutex);
	crypto_init_queue(&state->queue, CRYPTD_MAX_QLEN);

	state->task = kthread_run(fn, state, name);
	if (IS_ERR(state->task))
		return PTR_ERR(state->task);

	return 0;
}

static inline void cryptd_stop_thread(struct cryptd_state *state)
{
	BUG_ON(state->queue.qlen);
	kthread_stop(state->task);
}

static int cryptd_thread(void *data)
{
	struct cryptd_state *state = data;
	int stop;

	current->flags |= PF_NOFREEZE;

	do {
		struct crypto_async_request *req, *backlog;

		mutex_lock(&state->mutex);
		__set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_bh(&state->lock);
		backlog = crypto_get_backlog(&state->queue);
		req = crypto_dequeue_request(&state->queue);
		spin_unlock_bh(&state->lock);

		stop = kthread_should_stop();

		if (stop || req) {
			__set_current_state(TASK_RUNNING);
			if (req) {
				if (backlog)
					backlog->complete(backlog,
							  -EINPROGRESS);
				req->complete(req, 0);
			}
		}

		mutex_unlock(&state->mutex);

		schedule();
	} while (!stop);

	return 0;
}

static int __init cryptd_init(void)
{
	int err;

	err = cryptd_create_thread(&state, cryptd_thread, "cryptd");
	if (err)
		return err;

	err = crypto_register_template(&cryptd_tmpl);
	if (err)
		kthread_stop(state.task);

	return err;
}

static void __exit cryptd_exit(void)
{
	cryptd_stop_thread(&state);
	crypto_unregister_template(&cryptd_tmpl);
}

module_init(cryptd_init);
module_exit(cryptd_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Software async crypto daemon");
