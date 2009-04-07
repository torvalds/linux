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
#include <crypto/internal/hash.h>
#include <crypto/cryptd.h>
#include <crypto/crypto_wq.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>

#define CRYPTD_MAX_CPU_QLEN 100

struct cryptd_cpu_queue {
	struct crypto_queue queue;
	struct work_struct work;
};

struct cryptd_queue {
	struct cryptd_cpu_queue *cpu_queue;
};

struct cryptd_instance_ctx {
	struct crypto_spawn spawn;
	struct cryptd_queue *queue;
};

struct cryptd_blkcipher_ctx {
	struct crypto_blkcipher *child;
};

struct cryptd_blkcipher_request_ctx {
	crypto_completion_t complete;
};

struct cryptd_hash_ctx {
	struct crypto_hash *child;
};

struct cryptd_hash_request_ctx {
	crypto_completion_t complete;
};

static void cryptd_queue_worker(struct work_struct *work);

static int cryptd_init_queue(struct cryptd_queue *queue,
			     unsigned int max_cpu_qlen)
{
	int cpu;
	struct cryptd_cpu_queue *cpu_queue;

	queue->cpu_queue = alloc_percpu(struct cryptd_cpu_queue);
	if (!queue->cpu_queue)
		return -ENOMEM;
	for_each_possible_cpu(cpu) {
		cpu_queue = per_cpu_ptr(queue->cpu_queue, cpu);
		crypto_init_queue(&cpu_queue->queue, max_cpu_qlen);
		INIT_WORK(&cpu_queue->work, cryptd_queue_worker);
	}
	return 0;
}

static void cryptd_fini_queue(struct cryptd_queue *queue)
{
	int cpu;
	struct cryptd_cpu_queue *cpu_queue;

	for_each_possible_cpu(cpu) {
		cpu_queue = per_cpu_ptr(queue->cpu_queue, cpu);
		BUG_ON(cpu_queue->queue.qlen);
	}
	free_percpu(queue->cpu_queue);
}

static int cryptd_enqueue_request(struct cryptd_queue *queue,
				  struct crypto_async_request *request)
{
	int cpu, err;
	struct cryptd_cpu_queue *cpu_queue;

	cpu = get_cpu();
	cpu_queue = per_cpu_ptr(queue->cpu_queue, cpu);
	err = crypto_enqueue_request(&cpu_queue->queue, request);
	queue_work_on(cpu, kcrypto_wq, &cpu_queue->work);
	put_cpu();

	return err;
}

/* Called in workqueue context, do one real cryption work (via
 * req->complete) and reschedule itself if there are more work to
 * do. */
static void cryptd_queue_worker(struct work_struct *work)
{
	struct cryptd_cpu_queue *cpu_queue;
	struct crypto_async_request *req, *backlog;

	cpu_queue = container_of(work, struct cryptd_cpu_queue, work);
	/* Only handle one request at a time to avoid hogging crypto
	 * workqueue. preempt_disable/enable is used to prevent
	 * being preempted by cryptd_enqueue_request() */
	preempt_disable();
	backlog = crypto_get_backlog(&cpu_queue->queue);
	req = crypto_dequeue_request(&cpu_queue->queue);
	preempt_enable();

	if (!req)
		return;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);
	req->complete(req, 0);

	if (cpu_queue->queue.qlen)
		queue_work(kcrypto_wq, &cpu_queue->work);
}

static inline struct cryptd_queue *cryptd_get_queue(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct cryptd_instance_ctx *ictx = crypto_instance_ctx(inst);
	return ictx->queue;
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

	if (unlikely(err == -EINPROGRESS))
		goto out;

	desc.tfm = child;
	desc.info = req->info;
	desc.flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	err = crypt(&desc, req->dst, req->src, req->nbytes);

	req->base.complete = rctx->complete;

out:
	local_bh_disable();
	rctx->complete(&req->base, err);
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
	struct cryptd_queue *queue;

	queue = cryptd_get_queue(crypto_ablkcipher_tfm(tfm));
	rctx->complete = req->base.complete;
	req->base.complete = complete;

	return cryptd_enqueue_request(queue, &req->base);
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

	crypto_free_blkcipher(ctx->child);
}

static struct crypto_instance *cryptd_alloc_instance(struct crypto_alg *alg,
						     struct cryptd_queue *queue)
{
	struct crypto_instance *inst;
	struct cryptd_instance_ctx *ctx;
	int err;

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst) {
		inst = ERR_PTR(-ENOMEM);
		goto out;
	}

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "cryptd(%s)", alg->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto out_free_inst;

	ctx = crypto_instance_ctx(inst);
	err = crypto_init_spawn(&ctx->spawn, alg, inst,
				CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_ASYNC);
	if (err)
		goto out_free_inst;

	ctx->queue = queue;

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
	struct rtattr **tb, struct cryptd_queue *queue)
{
	struct crypto_instance *inst;
	struct crypto_alg *alg;

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_BLKCIPHER,
				  CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(alg))
		return ERR_CAST(alg);

	inst = cryptd_alloc_instance(alg, queue);
	if (IS_ERR(inst))
		goto out_put_alg;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC;
	inst->alg.cra_type = &crypto_ablkcipher_type;

	inst->alg.cra_ablkcipher.ivsize = alg->cra_blkcipher.ivsize;
	inst->alg.cra_ablkcipher.min_keysize = alg->cra_blkcipher.min_keysize;
	inst->alg.cra_ablkcipher.max_keysize = alg->cra_blkcipher.max_keysize;

	inst->alg.cra_ablkcipher.geniv = alg->cra_blkcipher.geniv;

	inst->alg.cra_ctxsize = sizeof(struct cryptd_blkcipher_ctx);

	inst->alg.cra_init = cryptd_blkcipher_init_tfm;
	inst->alg.cra_exit = cryptd_blkcipher_exit_tfm;

	inst->alg.cra_ablkcipher.setkey = cryptd_blkcipher_setkey;
	inst->alg.cra_ablkcipher.encrypt = cryptd_blkcipher_encrypt_enqueue;
	inst->alg.cra_ablkcipher.decrypt = cryptd_blkcipher_decrypt_enqueue;

out_put_alg:
	crypto_mod_put(alg);
	return inst;
}

static int cryptd_hash_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct cryptd_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct crypto_spawn *spawn = &ictx->spawn;
	struct cryptd_hash_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_hash *cipher;

	cipher = crypto_spawn_hash(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;
	tfm->crt_ahash.reqsize =
		sizeof(struct cryptd_hash_request_ctx);
	return 0;
}

static void cryptd_hash_exit_tfm(struct crypto_tfm *tfm)
{
	struct cryptd_hash_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_hash(ctx->child);
}

static int cryptd_hash_setkey(struct crypto_ahash *parent,
				   const u8 *key, unsigned int keylen)
{
	struct cryptd_hash_ctx *ctx   = crypto_ahash_ctx(parent);
	struct crypto_hash     *child = ctx->child;
	int err;

	crypto_hash_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_hash_set_flags(child, crypto_ahash_get_flags(parent) &
					  CRYPTO_TFM_REQ_MASK);
	err = crypto_hash_setkey(child, key, keylen);
	crypto_ahash_set_flags(parent, crypto_hash_get_flags(child) &
					    CRYPTO_TFM_RES_MASK);
	return err;
}

static int cryptd_hash_enqueue(struct ahash_request *req,
				crypto_completion_t complete)
{
	struct cryptd_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cryptd_queue *queue =
		cryptd_get_queue(crypto_ahash_tfm(tfm));

	rctx->complete = req->base.complete;
	req->base.complete = complete;

	return cryptd_enqueue_request(queue, &req->base);
}

static void cryptd_hash_init(struct crypto_async_request *req_async, int err)
{
	struct cryptd_hash_ctx *ctx   = crypto_tfm_ctx(req_async->tfm);
	struct crypto_hash     *child = ctx->child;
	struct ahash_request    *req = ahash_request_cast(req_async);
	struct cryptd_hash_request_ctx *rctx;
	struct hash_desc desc;

	rctx = ahash_request_ctx(req);

	if (unlikely(err == -EINPROGRESS))
		goto out;

	desc.tfm = child;
	desc.flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	err = crypto_hash_crt(child)->init(&desc);

	req->base.complete = rctx->complete;

out:
	local_bh_disable();
	rctx->complete(&req->base, err);
	local_bh_enable();
}

static int cryptd_hash_init_enqueue(struct ahash_request *req)
{
	return cryptd_hash_enqueue(req, cryptd_hash_init);
}

static void cryptd_hash_update(struct crypto_async_request *req_async, int err)
{
	struct cryptd_hash_ctx *ctx   = crypto_tfm_ctx(req_async->tfm);
	struct crypto_hash     *child = ctx->child;
	struct ahash_request    *req = ahash_request_cast(req_async);
	struct cryptd_hash_request_ctx *rctx;
	struct hash_desc desc;

	rctx = ahash_request_ctx(req);

	if (unlikely(err == -EINPROGRESS))
		goto out;

	desc.tfm = child;
	desc.flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	err = crypto_hash_crt(child)->update(&desc,
						req->src,
						req->nbytes);

	req->base.complete = rctx->complete;

out:
	local_bh_disable();
	rctx->complete(&req->base, err);
	local_bh_enable();
}

static int cryptd_hash_update_enqueue(struct ahash_request *req)
{
	return cryptd_hash_enqueue(req, cryptd_hash_update);
}

static void cryptd_hash_final(struct crypto_async_request *req_async, int err)
{
	struct cryptd_hash_ctx *ctx   = crypto_tfm_ctx(req_async->tfm);
	struct crypto_hash     *child = ctx->child;
	struct ahash_request    *req = ahash_request_cast(req_async);
	struct cryptd_hash_request_ctx *rctx;
	struct hash_desc desc;

	rctx = ahash_request_ctx(req);

	if (unlikely(err == -EINPROGRESS))
		goto out;

	desc.tfm = child;
	desc.flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	err = crypto_hash_crt(child)->final(&desc, req->result);

	req->base.complete = rctx->complete;

out:
	local_bh_disable();
	rctx->complete(&req->base, err);
	local_bh_enable();
}

static int cryptd_hash_final_enqueue(struct ahash_request *req)
{
	return cryptd_hash_enqueue(req, cryptd_hash_final);
}

static void cryptd_hash_digest(struct crypto_async_request *req_async, int err)
{
	struct cryptd_hash_ctx *ctx   = crypto_tfm_ctx(req_async->tfm);
	struct crypto_hash     *child = ctx->child;
	struct ahash_request    *req = ahash_request_cast(req_async);
	struct cryptd_hash_request_ctx *rctx;
	struct hash_desc desc;

	rctx = ahash_request_ctx(req);

	if (unlikely(err == -EINPROGRESS))
		goto out;

	desc.tfm = child;
	desc.flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	err = crypto_hash_crt(child)->digest(&desc,
						req->src,
						req->nbytes,
						req->result);

	req->base.complete = rctx->complete;

out:
	local_bh_disable();
	rctx->complete(&req->base, err);
	local_bh_enable();
}

static int cryptd_hash_digest_enqueue(struct ahash_request *req)
{
	return cryptd_hash_enqueue(req, cryptd_hash_digest);
}

static struct crypto_instance *cryptd_alloc_hash(
	struct rtattr **tb, struct cryptd_queue *queue)
{
	struct crypto_instance *inst;
	struct crypto_alg *alg;

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_HASH,
				  CRYPTO_ALG_TYPE_HASH_MASK);
	if (IS_ERR(alg))
		return ERR_PTR(PTR_ERR(alg));

	inst = cryptd_alloc_instance(alg, queue);
	if (IS_ERR(inst))
		goto out_put_alg;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_ASYNC;
	inst->alg.cra_type = &crypto_ahash_type;

	inst->alg.cra_ahash.digestsize = alg->cra_hash.digestsize;
	inst->alg.cra_ctxsize = sizeof(struct cryptd_hash_ctx);

	inst->alg.cra_init = cryptd_hash_init_tfm;
	inst->alg.cra_exit = cryptd_hash_exit_tfm;

	inst->alg.cra_ahash.init   = cryptd_hash_init_enqueue;
	inst->alg.cra_ahash.update = cryptd_hash_update_enqueue;
	inst->alg.cra_ahash.final  = cryptd_hash_final_enqueue;
	inst->alg.cra_ahash.setkey = cryptd_hash_setkey;
	inst->alg.cra_ahash.digest = cryptd_hash_digest_enqueue;

out_put_alg:
	crypto_mod_put(alg);
	return inst;
}

static struct cryptd_queue queue;

static struct crypto_instance *cryptd_alloc(struct rtattr **tb)
{
	struct crypto_attr_type *algt;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return ERR_CAST(algt);

	switch (algt->type & algt->mask & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_BLKCIPHER:
		return cryptd_alloc_blkcipher(tb, &queue);
	case CRYPTO_ALG_TYPE_DIGEST:
		return cryptd_alloc_hash(tb, &queue);
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

struct cryptd_ablkcipher *cryptd_alloc_ablkcipher(const char *alg_name,
						  u32 type, u32 mask)
{
	char cryptd_alg_name[CRYPTO_MAX_ALG_NAME];
	struct crypto_ablkcipher *tfm;

	if (snprintf(cryptd_alg_name, CRYPTO_MAX_ALG_NAME,
		     "cryptd(%s)", alg_name) >= CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-EINVAL);
	tfm = crypto_alloc_ablkcipher(cryptd_alg_name, type, mask);
	if (IS_ERR(tfm))
		return ERR_CAST(tfm);
	if (crypto_ablkcipher_tfm(tfm)->__crt_alg->cra_module != THIS_MODULE) {
		crypto_free_ablkcipher(tfm);
		return ERR_PTR(-EINVAL);
	}

	return __cryptd_ablkcipher_cast(tfm);
}
EXPORT_SYMBOL_GPL(cryptd_alloc_ablkcipher);

struct crypto_blkcipher *cryptd_ablkcipher_child(struct cryptd_ablkcipher *tfm)
{
	struct cryptd_blkcipher_ctx *ctx = crypto_ablkcipher_ctx(&tfm->base);
	return ctx->child;
}
EXPORT_SYMBOL_GPL(cryptd_ablkcipher_child);

void cryptd_free_ablkcipher(struct cryptd_ablkcipher *tfm)
{
	crypto_free_ablkcipher(&tfm->base);
}
EXPORT_SYMBOL_GPL(cryptd_free_ablkcipher);

static int __init cryptd_init(void)
{
	int err;

	err = cryptd_init_queue(&queue, CRYPTD_MAX_CPU_QLEN);
	if (err)
		return err;

	err = crypto_register_template(&cryptd_tmpl);
	if (err)
		cryptd_fini_queue(&queue);

	return err;
}

static void __exit cryptd_exit(void)
{
	cryptd_fini_queue(&queue);
	crypto_unregister_template(&cryptd_tmpl);
}

module_init(cryptd_init);
module_exit(cryptd_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Software async crypto daemon");
