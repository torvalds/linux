/*
 * Software async crypto daemon.
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * Added AEAD support to cryptd.
 *    Authors: Tadeusz Struk (tadeusz.struk@intel.com)
 *             Adrian Hoban <adrian.hoban@intel.com>
 *             Gabriele Paoloni <gabriele.paoloni@intel.com>
 *             Aidan O'Mahony (aidan.o.mahony@intel.com)
 *    Copyright (c) 2010, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/algapi.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/aead.h>
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
	struct cryptd_cpu_queue __percpu *cpu_queue;
};

struct cryptd_instance_ctx {
	struct crypto_spawn spawn;
	struct cryptd_queue *queue;
};

struct hashd_instance_ctx {
	struct crypto_shash_spawn spawn;
	struct cryptd_queue *queue;
};

struct aead_instance_ctx {
	struct crypto_aead_spawn aead_spawn;
	struct cryptd_queue *queue;
};

struct cryptd_blkcipher_ctx {
	struct crypto_blkcipher *child;
};

struct cryptd_blkcipher_request_ctx {
	crypto_completion_t complete;
};

struct cryptd_hash_ctx {
	struct crypto_shash *child;
};

struct cryptd_hash_request_ctx {
	crypto_completion_t complete;
	struct shash_desc desc;
};

struct cryptd_aead_ctx {
	struct crypto_aead *child;
};

struct cryptd_aead_request_ctx {
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
	cpu_queue = this_cpu_ptr(queue->cpu_queue);
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
	/*
	 * Only handle one request at a time to avoid hogging crypto workqueue.
	 * preempt_disable/enable is used to prevent being preempted by
	 * cryptd_enqueue_request(). local_bh_disable/enable is used to prevent
	 * cryptd_enqueue_request() being accessed from software interrupts.
	 */
	local_bh_disable();
	preempt_disable();
	backlog = crypto_get_backlog(&cpu_queue->queue);
	req = crypto_dequeue_request(&cpu_queue->queue);
	preempt_enable();
	local_bh_enable();

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
				    crypto_completion_t compl)
{
	struct cryptd_blkcipher_request_ctx *rctx = ablkcipher_request_ctx(req);
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct cryptd_queue *queue;

	queue = cryptd_get_queue(crypto_ablkcipher_tfm(tfm));
	rctx->complete = req->base.complete;
	req->base.complete = compl;

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

static void *cryptd_alloc_instance(struct crypto_alg *alg, unsigned int head,
				   unsigned int tail)
{
	char *p;
	struct crypto_instance *inst;
	int err;

	p = kzalloc(head + sizeof(*inst) + tail, GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	inst = (void *)(p + head);

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "cryptd(%s)", alg->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto out_free_inst;

	memcpy(inst->alg.cra_name, alg->cra_name, CRYPTO_MAX_ALG_NAME);

	inst->alg.cra_priority = alg->cra_priority + 50;
	inst->alg.cra_blocksize = alg->cra_blocksize;
	inst->alg.cra_alignmask = alg->cra_alignmask;

out:
	return p;

out_free_inst:
	kfree(p);
	p = ERR_PTR(err);
	goto out;
}

static int cryptd_create_blkcipher(struct crypto_template *tmpl,
				   struct rtattr **tb,
				   struct cryptd_queue *queue)
{
	struct cryptd_instance_ctx *ctx;
	struct crypto_instance *inst;
	struct crypto_alg *alg;
	int err;

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_BLKCIPHER,
				  CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(alg))
		return PTR_ERR(alg);

	inst = cryptd_alloc_instance(alg, 0, sizeof(*ctx));
	err = PTR_ERR(inst);
	if (IS_ERR(inst))
		goto out_put_alg;

	ctx = crypto_instance_ctx(inst);
	ctx->queue = queue;

	err = crypto_init_spawn(&ctx->spawn, alg, inst,
				CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_ASYNC);
	if (err)
		goto out_free_inst;

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

	err = crypto_register_instance(tmpl, inst);
	if (err) {
		crypto_drop_spawn(&ctx->spawn);
out_free_inst:
		kfree(inst);
	}

out_put_alg:
	crypto_mod_put(alg);
	return err;
}

static int cryptd_hash_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct hashd_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct crypto_shash_spawn *spawn = &ictx->spawn;
	struct cryptd_hash_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_shash *hash;

	hash = crypto_spawn_shash(spawn);
	if (IS_ERR(hash))
		return PTR_ERR(hash);

	ctx->child = hash;
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct cryptd_hash_request_ctx) +
				 crypto_shash_descsize(hash));
	return 0;
}

static void cryptd_hash_exit_tfm(struct crypto_tfm *tfm)
{
	struct cryptd_hash_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_shash(ctx->child);
}

static int cryptd_hash_setkey(struct crypto_ahash *parent,
				   const u8 *key, unsigned int keylen)
{
	struct cryptd_hash_ctx *ctx   = crypto_ahash_ctx(parent);
	struct crypto_shash *child = ctx->child;
	int err;

	crypto_shash_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_shash_set_flags(child, crypto_ahash_get_flags(parent) &
				      CRYPTO_TFM_REQ_MASK);
	err = crypto_shash_setkey(child, key, keylen);
	crypto_ahash_set_flags(parent, crypto_shash_get_flags(child) &
				       CRYPTO_TFM_RES_MASK);
	return err;
}

static int cryptd_hash_enqueue(struct ahash_request *req,
				crypto_completion_t compl)
{
	struct cryptd_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cryptd_queue *queue =
		cryptd_get_queue(crypto_ahash_tfm(tfm));

	rctx->complete = req->base.complete;
	req->base.complete = compl;

	return cryptd_enqueue_request(queue, &req->base);
}

static void cryptd_hash_init(struct crypto_async_request *req_async, int err)
{
	struct cryptd_hash_ctx *ctx = crypto_tfm_ctx(req_async->tfm);
	struct crypto_shash *child = ctx->child;
	struct ahash_request *req = ahash_request_cast(req_async);
	struct cryptd_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct shash_desc *desc = &rctx->desc;

	if (unlikely(err == -EINPROGRESS))
		goto out;

	desc->tfm = child;
	desc->flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	err = crypto_shash_init(desc);

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
	struct ahash_request *req = ahash_request_cast(req_async);
	struct cryptd_hash_request_ctx *rctx;

	rctx = ahash_request_ctx(req);

	if (unlikely(err == -EINPROGRESS))
		goto out;

	err = shash_ahash_update(req, &rctx->desc);

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
	struct ahash_request *req = ahash_request_cast(req_async);
	struct cryptd_hash_request_ctx *rctx = ahash_request_ctx(req);

	if (unlikely(err == -EINPROGRESS))
		goto out;

	err = crypto_shash_final(&rctx->desc, req->result);

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

static void cryptd_hash_finup(struct crypto_async_request *req_async, int err)
{
	struct ahash_request *req = ahash_request_cast(req_async);
	struct cryptd_hash_request_ctx *rctx = ahash_request_ctx(req);

	if (unlikely(err == -EINPROGRESS))
		goto out;

	err = shash_ahash_finup(req, &rctx->desc);

	req->base.complete = rctx->complete;

out:
	local_bh_disable();
	rctx->complete(&req->base, err);
	local_bh_enable();
}

static int cryptd_hash_finup_enqueue(struct ahash_request *req)
{
	return cryptd_hash_enqueue(req, cryptd_hash_finup);
}

static void cryptd_hash_digest(struct crypto_async_request *req_async, int err)
{
	struct cryptd_hash_ctx *ctx = crypto_tfm_ctx(req_async->tfm);
	struct crypto_shash *child = ctx->child;
	struct ahash_request *req = ahash_request_cast(req_async);
	struct cryptd_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct shash_desc *desc = &rctx->desc;

	if (unlikely(err == -EINPROGRESS))
		goto out;

	desc->tfm = child;
	desc->flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	err = shash_ahash_digest(req, desc);

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

static int cryptd_hash_export(struct ahash_request *req, void *out)
{
	struct cryptd_hash_request_ctx *rctx = ahash_request_ctx(req);

	return crypto_shash_export(&rctx->desc, out);
}

static int cryptd_hash_import(struct ahash_request *req, const void *in)
{
	struct cryptd_hash_request_ctx *rctx = ahash_request_ctx(req);

	return crypto_shash_import(&rctx->desc, in);
}

static int cryptd_create_hash(struct crypto_template *tmpl, struct rtattr **tb,
			      struct cryptd_queue *queue)
{
	struct hashd_instance_ctx *ctx;
	struct ahash_instance *inst;
	struct shash_alg *salg;
	struct crypto_alg *alg;
	int err;

	salg = shash_attr_alg(tb[1], 0, 0);
	if (IS_ERR(salg))
		return PTR_ERR(salg);

	alg = &salg->base;
	inst = cryptd_alloc_instance(alg, ahash_instance_headroom(),
				     sizeof(*ctx));
	err = PTR_ERR(inst);
	if (IS_ERR(inst))
		goto out_put_alg;

	ctx = ahash_instance_ctx(inst);
	ctx->queue = queue;

	err = crypto_init_shash_spawn(&ctx->spawn, salg,
				      ahash_crypto_instance(inst));
	if (err)
		goto out_free_inst;

	inst->alg.halg.base.cra_flags = CRYPTO_ALG_ASYNC;

	inst->alg.halg.digestsize = salg->digestsize;
	inst->alg.halg.base.cra_ctxsize = sizeof(struct cryptd_hash_ctx);

	inst->alg.halg.base.cra_init = cryptd_hash_init_tfm;
	inst->alg.halg.base.cra_exit = cryptd_hash_exit_tfm;

	inst->alg.init   = cryptd_hash_init_enqueue;
	inst->alg.update = cryptd_hash_update_enqueue;
	inst->alg.final  = cryptd_hash_final_enqueue;
	inst->alg.finup  = cryptd_hash_finup_enqueue;
	inst->alg.export = cryptd_hash_export;
	inst->alg.import = cryptd_hash_import;
	inst->alg.setkey = cryptd_hash_setkey;
	inst->alg.digest = cryptd_hash_digest_enqueue;

	err = ahash_register_instance(tmpl, inst);
	if (err) {
		crypto_drop_shash(&ctx->spawn);
out_free_inst:
		kfree(inst);
	}

out_put_alg:
	crypto_mod_put(alg);
	return err;
}

static void cryptd_aead_crypt(struct aead_request *req,
			struct crypto_aead *child,
			int err,
			int (*crypt)(struct aead_request *req))
{
	struct cryptd_aead_request_ctx *rctx;
	rctx = aead_request_ctx(req);

	if (unlikely(err == -EINPROGRESS))
		goto out;
	aead_request_set_tfm(req, child);
	err = crypt( req );
	req->base.complete = rctx->complete;
out:
	local_bh_disable();
	rctx->complete(&req->base, err);
	local_bh_enable();
}

static void cryptd_aead_encrypt(struct crypto_async_request *areq, int err)
{
	struct cryptd_aead_ctx *ctx = crypto_tfm_ctx(areq->tfm);
	struct crypto_aead *child = ctx->child;
	struct aead_request *req;

	req = container_of(areq, struct aead_request, base);
	cryptd_aead_crypt(req, child, err, crypto_aead_crt(child)->encrypt);
}

static void cryptd_aead_decrypt(struct crypto_async_request *areq, int err)
{
	struct cryptd_aead_ctx *ctx = crypto_tfm_ctx(areq->tfm);
	struct crypto_aead *child = ctx->child;
	struct aead_request *req;

	req = container_of(areq, struct aead_request, base);
	cryptd_aead_crypt(req, child, err, crypto_aead_crt(child)->decrypt);
}

static int cryptd_aead_enqueue(struct aead_request *req,
				    crypto_completion_t compl)
{
	struct cryptd_aead_request_ctx *rctx = aead_request_ctx(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct cryptd_queue *queue = cryptd_get_queue(crypto_aead_tfm(tfm));

	rctx->complete = req->base.complete;
	req->base.complete = compl;
	return cryptd_enqueue_request(queue, &req->base);
}

static int cryptd_aead_encrypt_enqueue(struct aead_request *req)
{
	return cryptd_aead_enqueue(req, cryptd_aead_encrypt );
}

static int cryptd_aead_decrypt_enqueue(struct aead_request *req)
{
	return cryptd_aead_enqueue(req, cryptd_aead_decrypt );
}

static int cryptd_aead_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct aead_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct crypto_aead_spawn *spawn = &ictx->aead_spawn;
	struct cryptd_aead_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_aead *cipher;

	cipher = crypto_spawn_aead(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	crypto_aead_set_flags(cipher, CRYPTO_TFM_REQ_MAY_SLEEP);
	ctx->child = cipher;
	tfm->crt_aead.reqsize = sizeof(struct cryptd_aead_request_ctx);
	return 0;
}

static void cryptd_aead_exit_tfm(struct crypto_tfm *tfm)
{
	struct cryptd_aead_ctx *ctx = crypto_tfm_ctx(tfm);
	crypto_free_aead(ctx->child);
}

static int cryptd_create_aead(struct crypto_template *tmpl,
		              struct rtattr **tb,
			      struct cryptd_queue *queue)
{
	struct aead_instance_ctx *ctx;
	struct crypto_instance *inst;
	struct crypto_alg *alg;
	int err;

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_AEAD,
				CRYPTO_ALG_TYPE_MASK);
        if (IS_ERR(alg))
		return PTR_ERR(alg);

	inst = cryptd_alloc_instance(alg, 0, sizeof(*ctx));
	err = PTR_ERR(inst);
	if (IS_ERR(inst))
		goto out_put_alg;

	ctx = crypto_instance_ctx(inst);
	ctx->queue = queue;

	err = crypto_init_spawn(&ctx->aead_spawn.base, alg, inst,
			CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_ASYNC);
	if (err)
		goto out_free_inst;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC;
	inst->alg.cra_type = alg->cra_type;
	inst->alg.cra_ctxsize = sizeof(struct cryptd_aead_ctx);
	inst->alg.cra_init = cryptd_aead_init_tfm;
	inst->alg.cra_exit = cryptd_aead_exit_tfm;
	inst->alg.cra_aead.setkey      = alg->cra_aead.setkey;
	inst->alg.cra_aead.setauthsize = alg->cra_aead.setauthsize;
	inst->alg.cra_aead.geniv       = alg->cra_aead.geniv;
	inst->alg.cra_aead.ivsize      = alg->cra_aead.ivsize;
	inst->alg.cra_aead.maxauthsize = alg->cra_aead.maxauthsize;
	inst->alg.cra_aead.encrypt     = cryptd_aead_encrypt_enqueue;
	inst->alg.cra_aead.decrypt     = cryptd_aead_decrypt_enqueue;
	inst->alg.cra_aead.givencrypt  = alg->cra_aead.givencrypt;
	inst->alg.cra_aead.givdecrypt  = alg->cra_aead.givdecrypt;

	err = crypto_register_instance(tmpl, inst);
	if (err) {
		crypto_drop_spawn(&ctx->aead_spawn.base);
out_free_inst:
		kfree(inst);
	}
out_put_alg:
	crypto_mod_put(alg);
	return err;
}

static struct cryptd_queue queue;

static int cryptd_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct crypto_attr_type *algt;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	switch (algt->type & algt->mask & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_BLKCIPHER:
		return cryptd_create_blkcipher(tmpl, tb, &queue);
	case CRYPTO_ALG_TYPE_DIGEST:
		return cryptd_create_hash(tmpl, tb, &queue);
	case CRYPTO_ALG_TYPE_AEAD:
		return cryptd_create_aead(tmpl, tb, &queue);
	}

	return -EINVAL;
}

static void cryptd_free(struct crypto_instance *inst)
{
	struct cryptd_instance_ctx *ctx = crypto_instance_ctx(inst);
	struct hashd_instance_ctx *hctx = crypto_instance_ctx(inst);
	struct aead_instance_ctx *aead_ctx = crypto_instance_ctx(inst);

	switch (inst->alg.cra_flags & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_AHASH:
		crypto_drop_shash(&hctx->spawn);
		kfree(ahash_instance(inst));
		return;
	case CRYPTO_ALG_TYPE_AEAD:
		crypto_drop_spawn(&aead_ctx->aead_spawn.base);
		kfree(inst);
		return;
	default:
		crypto_drop_spawn(&ctx->spawn);
		kfree(inst);
	}
}

static struct crypto_template cryptd_tmpl = {
	.name = "cryptd",
	.create = cryptd_create,
	.free = cryptd_free,
	.module = THIS_MODULE,
};

struct cryptd_ablkcipher *cryptd_alloc_ablkcipher(const char *alg_name,
						  u32 type, u32 mask)
{
	char cryptd_alg_name[CRYPTO_MAX_ALG_NAME];
	struct crypto_tfm *tfm;

	if (snprintf(cryptd_alg_name, CRYPTO_MAX_ALG_NAME,
		     "cryptd(%s)", alg_name) >= CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-EINVAL);
	type &= ~(CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_GENIV);
	type |= CRYPTO_ALG_TYPE_BLKCIPHER;
	mask &= ~CRYPTO_ALG_TYPE_MASK;
	mask |= (CRYPTO_ALG_GENIV | CRYPTO_ALG_TYPE_BLKCIPHER_MASK);
	tfm = crypto_alloc_base(cryptd_alg_name, type, mask);
	if (IS_ERR(tfm))
		return ERR_CAST(tfm);
	if (tfm->__crt_alg->cra_module != THIS_MODULE) {
		crypto_free_tfm(tfm);
		return ERR_PTR(-EINVAL);
	}

	return __cryptd_ablkcipher_cast(__crypto_ablkcipher_cast(tfm));
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

struct cryptd_ahash *cryptd_alloc_ahash(const char *alg_name,
					u32 type, u32 mask)
{
	char cryptd_alg_name[CRYPTO_MAX_ALG_NAME];
	struct crypto_ahash *tfm;

	if (snprintf(cryptd_alg_name, CRYPTO_MAX_ALG_NAME,
		     "cryptd(%s)", alg_name) >= CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-EINVAL);
	tfm = crypto_alloc_ahash(cryptd_alg_name, type, mask);
	if (IS_ERR(tfm))
		return ERR_CAST(tfm);
	if (tfm->base.__crt_alg->cra_module != THIS_MODULE) {
		crypto_free_ahash(tfm);
		return ERR_PTR(-EINVAL);
	}

	return __cryptd_ahash_cast(tfm);
}
EXPORT_SYMBOL_GPL(cryptd_alloc_ahash);

struct crypto_shash *cryptd_ahash_child(struct cryptd_ahash *tfm)
{
	struct cryptd_hash_ctx *ctx = crypto_ahash_ctx(&tfm->base);

	return ctx->child;
}
EXPORT_SYMBOL_GPL(cryptd_ahash_child);

struct shash_desc *cryptd_shash_desc(struct ahash_request *req)
{
	struct cryptd_hash_request_ctx *rctx = ahash_request_ctx(req);
	return &rctx->desc;
}
EXPORT_SYMBOL_GPL(cryptd_shash_desc);

void cryptd_free_ahash(struct cryptd_ahash *tfm)
{
	crypto_free_ahash(&tfm->base);
}
EXPORT_SYMBOL_GPL(cryptd_free_ahash);

struct cryptd_aead *cryptd_alloc_aead(const char *alg_name,
						  u32 type, u32 mask)
{
	char cryptd_alg_name[CRYPTO_MAX_ALG_NAME];
	struct crypto_aead *tfm;

	if (snprintf(cryptd_alg_name, CRYPTO_MAX_ALG_NAME,
		     "cryptd(%s)", alg_name) >= CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-EINVAL);
	tfm = crypto_alloc_aead(cryptd_alg_name, type, mask);
	if (IS_ERR(tfm))
		return ERR_CAST(tfm);
	if (tfm->base.__crt_alg->cra_module != THIS_MODULE) {
		crypto_free_aead(tfm);
		return ERR_PTR(-EINVAL);
	}
	return __cryptd_aead_cast(tfm);
}
EXPORT_SYMBOL_GPL(cryptd_alloc_aead);

struct crypto_aead *cryptd_aead_child(struct cryptd_aead *tfm)
{
	struct cryptd_aead_ctx *ctx;
	ctx = crypto_aead_ctx(&tfm->base);
	return ctx->child;
}
EXPORT_SYMBOL_GPL(cryptd_aead_child);

void cryptd_free_aead(struct cryptd_aead *tfm)
{
	crypto_free_aead(&tfm->base);
}
EXPORT_SYMBOL_GPL(cryptd_free_aead);

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

subsys_initcall(cryptd_init);
module_exit(cryptd_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Software async crypto daemon");
