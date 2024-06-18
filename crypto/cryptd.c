// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <crypto/internal/hash.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/cryptd.h>
#include <linux/refcount.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

static unsigned int cryptd_max_cpu_qlen = 1000;
module_param(cryptd_max_cpu_qlen, uint, 0);
MODULE_PARM_DESC(cryptd_max_cpu_qlen, "Set cryptd Max queue depth");

static struct workqueue_struct *cryptd_wq;

struct cryptd_cpu_queue {
	struct crypto_queue queue;
	struct work_struct work;
};

struct cryptd_queue {
	/*
	 * Protected by disabling BH to allow enqueueing from softinterrupt and
	 * dequeuing from kworker (cryptd_queue_worker()).
	 */
	struct cryptd_cpu_queue __percpu *cpu_queue;
};

struct cryptd_instance_ctx {
	struct crypto_spawn spawn;
	struct cryptd_queue *queue;
};

struct skcipherd_instance_ctx {
	struct crypto_skcipher_spawn spawn;
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

struct cryptd_skcipher_ctx {
	refcount_t refcnt;
	struct crypto_skcipher *child;
};

struct cryptd_skcipher_request_ctx {
	struct skcipher_request req;
};

struct cryptd_hash_ctx {
	refcount_t refcnt;
	struct crypto_shash *child;
};

struct cryptd_hash_request_ctx {
	crypto_completion_t complete;
	void *data;
	struct shash_desc desc;
};

struct cryptd_aead_ctx {
	refcount_t refcnt;
	struct crypto_aead *child;
};

struct cryptd_aead_request_ctx {
	struct aead_request req;
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
	pr_info("cryptd: max_cpu_qlen set to %d\n", max_cpu_qlen);
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
	int err;
	struct cryptd_cpu_queue *cpu_queue;
	refcount_t *refcnt;

	local_bh_disable();
	cpu_queue = this_cpu_ptr(queue->cpu_queue);
	err = crypto_enqueue_request(&cpu_queue->queue, request);

	refcnt = crypto_tfm_ctx(request->tfm);

	if (err == -ENOSPC)
		goto out;

	queue_work_on(smp_processor_id(), cryptd_wq, &cpu_queue->work);

	if (!refcount_read(refcnt))
		goto out;

	refcount_inc(refcnt);

out:
	local_bh_enable();

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
	 */
	local_bh_disable();
	backlog = crypto_get_backlog(&cpu_queue->queue);
	req = crypto_dequeue_request(&cpu_queue->queue);
	local_bh_enable();

	if (!req)
		return;

	if (backlog)
		crypto_request_complete(backlog, -EINPROGRESS);
	crypto_request_complete(req, 0);

	if (cpu_queue->queue.qlen)
		queue_work(cryptd_wq, &cpu_queue->work);
}

static inline struct cryptd_queue *cryptd_get_queue(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct cryptd_instance_ctx *ictx = crypto_instance_ctx(inst);
	return ictx->queue;
}

static void cryptd_type_and_mask(struct crypto_attr_type *algt,
				 u32 *type, u32 *mask)
{
	/*
	 * cryptd is allowed to wrap internal algorithms, but in that case the
	 * resulting cryptd instance will be marked as internal as well.
	 */
	*type = algt->type & CRYPTO_ALG_INTERNAL;
	*mask = algt->mask & CRYPTO_ALG_INTERNAL;

	/* No point in cryptd wrapping an algorithm that's already async. */
	*mask |= CRYPTO_ALG_ASYNC;

	*mask |= crypto_algt_inherited_mask(algt);
}

static int cryptd_init_instance(struct crypto_instance *inst,
				struct crypto_alg *alg)
{
	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "cryptd(%s)",
		     alg->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	memcpy(inst->alg.cra_name, alg->cra_name, CRYPTO_MAX_ALG_NAME);

	inst->alg.cra_priority = alg->cra_priority + 50;
	inst->alg.cra_blocksize = alg->cra_blocksize;
	inst->alg.cra_alignmask = alg->cra_alignmask;

	return 0;
}

static int cryptd_skcipher_setkey(struct crypto_skcipher *parent,
				  const u8 *key, unsigned int keylen)
{
	struct cryptd_skcipher_ctx *ctx = crypto_skcipher_ctx(parent);
	struct crypto_skcipher *child = ctx->child;

	crypto_skcipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(child,
				  crypto_skcipher_get_flags(parent) &
				  CRYPTO_TFM_REQ_MASK);
	return crypto_skcipher_setkey(child, key, keylen);
}

static struct skcipher_request *cryptd_skcipher_prepare(
	struct skcipher_request *req, int err)
{
	struct cryptd_skcipher_request_ctx *rctx = skcipher_request_ctx(req);
	struct skcipher_request *subreq = &rctx->req;
	struct cryptd_skcipher_ctx *ctx;
	struct crypto_skcipher *child;

	req->base.complete = subreq->base.complete;
	req->base.data = subreq->base.data;

	if (unlikely(err == -EINPROGRESS))
		return NULL;

	ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	child = ctx->child;

	skcipher_request_set_tfm(subreq, child);
	skcipher_request_set_callback(subreq, CRYPTO_TFM_REQ_MAY_SLEEP,
				      NULL, NULL);
	skcipher_request_set_crypt(subreq, req->src, req->dst, req->cryptlen,
				   req->iv);

	return subreq;
}

static void cryptd_skcipher_complete(struct skcipher_request *req, int err,
				     crypto_completion_t complete)
{
	struct cryptd_skcipher_request_ctx *rctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct cryptd_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_request *subreq = &rctx->req;
	int refcnt = refcount_read(&ctx->refcnt);

	local_bh_disable();
	skcipher_request_complete(req, err);
	local_bh_enable();

	if (unlikely(err == -EINPROGRESS)) {
		subreq->base.complete = req->base.complete;
		subreq->base.data = req->base.data;
		req->base.complete = complete;
		req->base.data = req;
	} else if (refcnt && refcount_dec_and_test(&ctx->refcnt))
		crypto_free_skcipher(tfm);
}

static void cryptd_skcipher_encrypt(void *data, int err)
{
	struct skcipher_request *req = data;
	struct skcipher_request *subreq;

	subreq = cryptd_skcipher_prepare(req, err);
	if (likely(subreq))
		err = crypto_skcipher_encrypt(subreq);

	cryptd_skcipher_complete(req, err, cryptd_skcipher_encrypt);
}

static void cryptd_skcipher_decrypt(void *data, int err)
{
	struct skcipher_request *req = data;
	struct skcipher_request *subreq;

	subreq = cryptd_skcipher_prepare(req, err);
	if (likely(subreq))
		err = crypto_skcipher_decrypt(subreq);

	cryptd_skcipher_complete(req, err, cryptd_skcipher_decrypt);
}

static int cryptd_skcipher_enqueue(struct skcipher_request *req,
				   crypto_completion_t compl)
{
	struct cryptd_skcipher_request_ctx *rctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct skcipher_request *subreq = &rctx->req;
	struct cryptd_queue *queue;

	queue = cryptd_get_queue(crypto_skcipher_tfm(tfm));
	subreq->base.complete = req->base.complete;
	subreq->base.data = req->base.data;
	req->base.complete = compl;
	req->base.data = req;

	return cryptd_enqueue_request(queue, &req->base);
}

static int cryptd_skcipher_encrypt_enqueue(struct skcipher_request *req)
{
	return cryptd_skcipher_enqueue(req, cryptd_skcipher_encrypt);
}

static int cryptd_skcipher_decrypt_enqueue(struct skcipher_request *req)
{
	return cryptd_skcipher_enqueue(req, cryptd_skcipher_decrypt);
}

static int cryptd_skcipher_init_tfm(struct crypto_skcipher *tfm)
{
	struct skcipher_instance *inst = skcipher_alg_instance(tfm);
	struct skcipherd_instance_ctx *ictx = skcipher_instance_ctx(inst);
	struct crypto_skcipher_spawn *spawn = &ictx->spawn;
	struct cryptd_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *cipher;

	cipher = crypto_spawn_skcipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;
	crypto_skcipher_set_reqsize(
		tfm, sizeof(struct cryptd_skcipher_request_ctx) +
		     crypto_skcipher_reqsize(cipher));
	return 0;
}

static void cryptd_skcipher_exit_tfm(struct crypto_skcipher *tfm)
{
	struct cryptd_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(ctx->child);
}

static void cryptd_skcipher_free(struct skcipher_instance *inst)
{
	struct skcipherd_instance_ctx *ctx = skcipher_instance_ctx(inst);

	crypto_drop_skcipher(&ctx->spawn);
	kfree(inst);
}

static int cryptd_create_skcipher(struct crypto_template *tmpl,
				  struct rtattr **tb,
				  struct crypto_attr_type *algt,
				  struct cryptd_queue *queue)
{
	struct skcipherd_instance_ctx *ctx;
	struct skcipher_instance *inst;
	struct skcipher_alg_common *alg;
	u32 type;
	u32 mask;
	int err;

	cryptd_type_and_mask(algt, &type, &mask);

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	ctx = skcipher_instance_ctx(inst);
	ctx->queue = queue;

	err = crypto_grab_skcipher(&ctx->spawn, skcipher_crypto_instance(inst),
				   crypto_attr_alg_name(tb[1]), type, mask);
	if (err)
		goto err_free_inst;

	alg = crypto_spawn_skcipher_alg_common(&ctx->spawn);
	err = cryptd_init_instance(skcipher_crypto_instance(inst), &alg->base);
	if (err)
		goto err_free_inst;

	inst->alg.base.cra_flags |= CRYPTO_ALG_ASYNC |
		(alg->base.cra_flags & CRYPTO_ALG_INTERNAL);
	inst->alg.ivsize = alg->ivsize;
	inst->alg.chunksize = alg->chunksize;
	inst->alg.min_keysize = alg->min_keysize;
	inst->alg.max_keysize = alg->max_keysize;

	inst->alg.base.cra_ctxsize = sizeof(struct cryptd_skcipher_ctx);

	inst->alg.init = cryptd_skcipher_init_tfm;
	inst->alg.exit = cryptd_skcipher_exit_tfm;

	inst->alg.setkey = cryptd_skcipher_setkey;
	inst->alg.encrypt = cryptd_skcipher_encrypt_enqueue;
	inst->alg.decrypt = cryptd_skcipher_decrypt_enqueue;

	inst->free = cryptd_skcipher_free;

	err = skcipher_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		cryptd_skcipher_free(inst);
	}
	return err;
}

static int cryptd_hash_init_tfm(struct crypto_ahash *tfm)
{
	struct ahash_instance *inst = ahash_alg_instance(tfm);
	struct hashd_instance_ctx *ictx = ahash_instance_ctx(inst);
	struct crypto_shash_spawn *spawn = &ictx->spawn;
	struct cryptd_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct crypto_shash *hash;

	hash = crypto_spawn_shash(spawn);
	if (IS_ERR(hash))
		return PTR_ERR(hash);

	ctx->child = hash;
	crypto_ahash_set_reqsize(tfm,
				 sizeof(struct cryptd_hash_request_ctx) +
				 crypto_shash_descsize(hash));
	return 0;
}

static int cryptd_hash_clone_tfm(struct crypto_ahash *ntfm,
				 struct crypto_ahash *tfm)
{
	struct cryptd_hash_ctx *nctx = crypto_ahash_ctx(ntfm);
	struct cryptd_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct crypto_shash *hash;

	hash = crypto_clone_shash(ctx->child);
	if (IS_ERR(hash))
		return PTR_ERR(hash);

	nctx->child = hash;
	return 0;
}

static void cryptd_hash_exit_tfm(struct crypto_ahash *tfm)
{
	struct cryptd_hash_ctx *ctx = crypto_ahash_ctx(tfm);

	crypto_free_shash(ctx->child);
}

static int cryptd_hash_setkey(struct crypto_ahash *parent,
				   const u8 *key, unsigned int keylen)
{
	struct cryptd_hash_ctx *ctx   = crypto_ahash_ctx(parent);
	struct crypto_shash *child = ctx->child;

	crypto_shash_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_shash_set_flags(child, crypto_ahash_get_flags(parent) &
				      CRYPTO_TFM_REQ_MASK);
	return crypto_shash_setkey(child, key, keylen);
}

static int cryptd_hash_enqueue(struct ahash_request *req,
				crypto_completion_t compl)
{
	struct cryptd_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cryptd_queue *queue =
		cryptd_get_queue(crypto_ahash_tfm(tfm));

	rctx->complete = req->base.complete;
	rctx->data = req->base.data;
	req->base.complete = compl;
	req->base.data = req;

	return cryptd_enqueue_request(queue, &req->base);
}

static struct shash_desc *cryptd_hash_prepare(struct ahash_request *req,
					      int err)
{
	struct cryptd_hash_request_ctx *rctx = ahash_request_ctx(req);

	req->base.complete = rctx->complete;
	req->base.data = rctx->data;

	if (unlikely(err == -EINPROGRESS))
		return NULL;

	return &rctx->desc;
}

static void cryptd_hash_complete(struct ahash_request *req, int err,
				 crypto_completion_t complete)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cryptd_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	int refcnt = refcount_read(&ctx->refcnt);

	local_bh_disable();
	ahash_request_complete(req, err);
	local_bh_enable();

	if (err == -EINPROGRESS) {
		req->base.complete = complete;
		req->base.data = req;
	} else if (refcnt && refcount_dec_and_test(&ctx->refcnt))
		crypto_free_ahash(tfm);
}

static void cryptd_hash_init(void *data, int err)
{
	struct ahash_request *req = data;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cryptd_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct crypto_shash *child = ctx->child;
	struct shash_desc *desc;

	desc = cryptd_hash_prepare(req, err);
	if (unlikely(!desc))
		goto out;

	desc->tfm = child;

	err = crypto_shash_init(desc);

out:
	cryptd_hash_complete(req, err, cryptd_hash_init);
}

static int cryptd_hash_init_enqueue(struct ahash_request *req)
{
	return cryptd_hash_enqueue(req, cryptd_hash_init);
}

static void cryptd_hash_update(void *data, int err)
{
	struct ahash_request *req = data;
	struct shash_desc *desc;

	desc = cryptd_hash_prepare(req, err);
	if (likely(desc))
		err = shash_ahash_update(req, desc);

	cryptd_hash_complete(req, err, cryptd_hash_update);
}

static int cryptd_hash_update_enqueue(struct ahash_request *req)
{
	return cryptd_hash_enqueue(req, cryptd_hash_update);
}

static void cryptd_hash_final(void *data, int err)
{
	struct ahash_request *req = data;
	struct shash_desc *desc;

	desc = cryptd_hash_prepare(req, err);
	if (likely(desc))
		err = crypto_shash_final(desc, req->result);

	cryptd_hash_complete(req, err, cryptd_hash_final);
}

static int cryptd_hash_final_enqueue(struct ahash_request *req)
{
	return cryptd_hash_enqueue(req, cryptd_hash_final);
}

static void cryptd_hash_finup(void *data, int err)
{
	struct ahash_request *req = data;
	struct shash_desc *desc;

	desc = cryptd_hash_prepare(req, err);
	if (likely(desc))
		err = shash_ahash_finup(req, desc);

	cryptd_hash_complete(req, err, cryptd_hash_finup);
}

static int cryptd_hash_finup_enqueue(struct ahash_request *req)
{
	return cryptd_hash_enqueue(req, cryptd_hash_finup);
}

static void cryptd_hash_digest(void *data, int err)
{
	struct ahash_request *req = data;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cryptd_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct crypto_shash *child = ctx->child;
	struct shash_desc *desc;

	desc = cryptd_hash_prepare(req, err);
	if (unlikely(!desc))
		goto out;

	desc->tfm = child;

	err = shash_ahash_digest(req, desc);

out:
	cryptd_hash_complete(req, err, cryptd_hash_digest);
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
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cryptd_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct shash_desc *desc = cryptd_shash_desc(req);

	desc->tfm = ctx->child;

	return crypto_shash_import(desc, in);
}

static void cryptd_hash_free(struct ahash_instance *inst)
{
	struct hashd_instance_ctx *ctx = ahash_instance_ctx(inst);

	crypto_drop_shash(&ctx->spawn);
	kfree(inst);
}

static int cryptd_create_hash(struct crypto_template *tmpl, struct rtattr **tb,
			      struct crypto_attr_type *algt,
			      struct cryptd_queue *queue)
{
	struct hashd_instance_ctx *ctx;
	struct ahash_instance *inst;
	struct shash_alg *alg;
	u32 type;
	u32 mask;
	int err;

	cryptd_type_and_mask(algt, &type, &mask);

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	ctx = ahash_instance_ctx(inst);
	ctx->queue = queue;

	err = crypto_grab_shash(&ctx->spawn, ahash_crypto_instance(inst),
				crypto_attr_alg_name(tb[1]), type, mask);
	if (err)
		goto err_free_inst;
	alg = crypto_spawn_shash_alg(&ctx->spawn);

	err = cryptd_init_instance(ahash_crypto_instance(inst), &alg->base);
	if (err)
		goto err_free_inst;

	inst->alg.halg.base.cra_flags |= CRYPTO_ALG_ASYNC |
		(alg->base.cra_flags & (CRYPTO_ALG_INTERNAL|
					CRYPTO_ALG_OPTIONAL_KEY));
	inst->alg.halg.digestsize = alg->digestsize;
	inst->alg.halg.statesize = alg->statesize;
	inst->alg.halg.base.cra_ctxsize = sizeof(struct cryptd_hash_ctx);

	inst->alg.init_tfm = cryptd_hash_init_tfm;
	inst->alg.clone_tfm = cryptd_hash_clone_tfm;
	inst->alg.exit_tfm = cryptd_hash_exit_tfm;

	inst->alg.init   = cryptd_hash_init_enqueue;
	inst->alg.update = cryptd_hash_update_enqueue;
	inst->alg.final  = cryptd_hash_final_enqueue;
	inst->alg.finup  = cryptd_hash_finup_enqueue;
	inst->alg.export = cryptd_hash_export;
	inst->alg.import = cryptd_hash_import;
	if (crypto_shash_alg_has_setkey(alg))
		inst->alg.setkey = cryptd_hash_setkey;
	inst->alg.digest = cryptd_hash_digest_enqueue;

	inst->free = cryptd_hash_free;

	err = ahash_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		cryptd_hash_free(inst);
	}
	return err;
}

static int cryptd_aead_setkey(struct crypto_aead *parent,
			      const u8 *key, unsigned int keylen)
{
	struct cryptd_aead_ctx *ctx = crypto_aead_ctx(parent);
	struct crypto_aead *child = ctx->child;

	return crypto_aead_setkey(child, key, keylen);
}

static int cryptd_aead_setauthsize(struct crypto_aead *parent,
				   unsigned int authsize)
{
	struct cryptd_aead_ctx *ctx = crypto_aead_ctx(parent);
	struct crypto_aead *child = ctx->child;

	return crypto_aead_setauthsize(child, authsize);
}

static void cryptd_aead_crypt(struct aead_request *req,
			      struct crypto_aead *child, int err,
			      int (*crypt)(struct aead_request *req),
			      crypto_completion_t compl)
{
	struct cryptd_aead_request_ctx *rctx;
	struct aead_request *subreq;
	struct cryptd_aead_ctx *ctx;
	struct crypto_aead *tfm;
	int refcnt;

	rctx = aead_request_ctx(req);
	subreq = &rctx->req;
	req->base.complete = subreq->base.complete;
	req->base.data = subreq->base.data;

	tfm = crypto_aead_reqtfm(req);

	if (unlikely(err == -EINPROGRESS))
		goto out;

	aead_request_set_tfm(subreq, child);
	aead_request_set_callback(subreq, CRYPTO_TFM_REQ_MAY_SLEEP,
				  NULL, NULL);
	aead_request_set_crypt(subreq, req->src, req->dst, req->cryptlen,
			       req->iv);
	aead_request_set_ad(subreq, req->assoclen);

	err = crypt(subreq);

out:
	ctx = crypto_aead_ctx(tfm);
	refcnt = refcount_read(&ctx->refcnt);

	local_bh_disable();
	aead_request_complete(req, err);
	local_bh_enable();

	if (err == -EINPROGRESS) {
		subreq->base.complete = req->base.complete;
		subreq->base.data = req->base.data;
		req->base.complete = compl;
		req->base.data = req;
	} else if (refcnt && refcount_dec_and_test(&ctx->refcnt))
		crypto_free_aead(tfm);
}

static void cryptd_aead_encrypt(void *data, int err)
{
	struct aead_request *req = data;
	struct cryptd_aead_ctx *ctx;
	struct crypto_aead *child;

	ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	child = ctx->child;
	cryptd_aead_crypt(req, child, err, crypto_aead_alg(child)->encrypt,
			  cryptd_aead_encrypt);
}

static void cryptd_aead_decrypt(void *data, int err)
{
	struct aead_request *req = data;
	struct cryptd_aead_ctx *ctx;
	struct crypto_aead *child;

	ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	child = ctx->child;
	cryptd_aead_crypt(req, child, err, crypto_aead_alg(child)->decrypt,
			  cryptd_aead_decrypt);
}

static int cryptd_aead_enqueue(struct aead_request *req,
				    crypto_completion_t compl)
{
	struct cryptd_aead_request_ctx *rctx = aead_request_ctx(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct cryptd_queue *queue = cryptd_get_queue(crypto_aead_tfm(tfm));
	struct aead_request *subreq = &rctx->req;

	subreq->base.complete = req->base.complete;
	subreq->base.data = req->base.data;
	req->base.complete = compl;
	req->base.data = req;
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

static int cryptd_aead_init_tfm(struct crypto_aead *tfm)
{
	struct aead_instance *inst = aead_alg_instance(tfm);
	struct aead_instance_ctx *ictx = aead_instance_ctx(inst);
	struct crypto_aead_spawn *spawn = &ictx->aead_spawn;
	struct cryptd_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_aead *cipher;

	cipher = crypto_spawn_aead(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;
	crypto_aead_set_reqsize(
		tfm, sizeof(struct cryptd_aead_request_ctx) +
		     crypto_aead_reqsize(cipher));
	return 0;
}

static void cryptd_aead_exit_tfm(struct crypto_aead *tfm)
{
	struct cryptd_aead_ctx *ctx = crypto_aead_ctx(tfm);
	crypto_free_aead(ctx->child);
}

static void cryptd_aead_free(struct aead_instance *inst)
{
	struct aead_instance_ctx *ctx = aead_instance_ctx(inst);

	crypto_drop_aead(&ctx->aead_spawn);
	kfree(inst);
}

static int cryptd_create_aead(struct crypto_template *tmpl,
		              struct rtattr **tb,
			      struct crypto_attr_type *algt,
			      struct cryptd_queue *queue)
{
	struct aead_instance_ctx *ctx;
	struct aead_instance *inst;
	struct aead_alg *alg;
	u32 type;
	u32 mask;
	int err;

	cryptd_type_and_mask(algt, &type, &mask);

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	ctx = aead_instance_ctx(inst);
	ctx->queue = queue;

	err = crypto_grab_aead(&ctx->aead_spawn, aead_crypto_instance(inst),
			       crypto_attr_alg_name(tb[1]), type, mask);
	if (err)
		goto err_free_inst;

	alg = crypto_spawn_aead_alg(&ctx->aead_spawn);
	err = cryptd_init_instance(aead_crypto_instance(inst), &alg->base);
	if (err)
		goto err_free_inst;

	inst->alg.base.cra_flags |= CRYPTO_ALG_ASYNC |
		(alg->base.cra_flags & CRYPTO_ALG_INTERNAL);
	inst->alg.base.cra_ctxsize = sizeof(struct cryptd_aead_ctx);

	inst->alg.ivsize = crypto_aead_alg_ivsize(alg);
	inst->alg.maxauthsize = crypto_aead_alg_maxauthsize(alg);

	inst->alg.init = cryptd_aead_init_tfm;
	inst->alg.exit = cryptd_aead_exit_tfm;
	inst->alg.setkey = cryptd_aead_setkey;
	inst->alg.setauthsize = cryptd_aead_setauthsize;
	inst->alg.encrypt = cryptd_aead_encrypt_enqueue;
	inst->alg.decrypt = cryptd_aead_decrypt_enqueue;

	inst->free = cryptd_aead_free;

	err = aead_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		cryptd_aead_free(inst);
	}
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
	case CRYPTO_ALG_TYPE_LSKCIPHER:
		return cryptd_create_skcipher(tmpl, tb, algt, &queue);
	case CRYPTO_ALG_TYPE_HASH:
		return cryptd_create_hash(tmpl, tb, algt, &queue);
	case CRYPTO_ALG_TYPE_AEAD:
		return cryptd_create_aead(tmpl, tb, algt, &queue);
	}

	return -EINVAL;
}

static struct crypto_template cryptd_tmpl = {
	.name = "cryptd",
	.create = cryptd_create,
	.module = THIS_MODULE,
};

struct cryptd_skcipher *cryptd_alloc_skcipher(const char *alg_name,
					      u32 type, u32 mask)
{
	char cryptd_alg_name[CRYPTO_MAX_ALG_NAME];
	struct cryptd_skcipher_ctx *ctx;
	struct crypto_skcipher *tfm;

	if (snprintf(cryptd_alg_name, CRYPTO_MAX_ALG_NAME,
		     "cryptd(%s)", alg_name) >= CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-EINVAL);

	tfm = crypto_alloc_skcipher(cryptd_alg_name, type, mask);
	if (IS_ERR(tfm))
		return ERR_CAST(tfm);

	if (tfm->base.__crt_alg->cra_module != THIS_MODULE) {
		crypto_free_skcipher(tfm);
		return ERR_PTR(-EINVAL);
	}

	ctx = crypto_skcipher_ctx(tfm);
	refcount_set(&ctx->refcnt, 1);

	return container_of(tfm, struct cryptd_skcipher, base);
}
EXPORT_SYMBOL_GPL(cryptd_alloc_skcipher);

struct crypto_skcipher *cryptd_skcipher_child(struct cryptd_skcipher *tfm)
{
	struct cryptd_skcipher_ctx *ctx = crypto_skcipher_ctx(&tfm->base);

	return ctx->child;
}
EXPORT_SYMBOL_GPL(cryptd_skcipher_child);

bool cryptd_skcipher_queued(struct cryptd_skcipher *tfm)
{
	struct cryptd_skcipher_ctx *ctx = crypto_skcipher_ctx(&tfm->base);

	return refcount_read(&ctx->refcnt) - 1;
}
EXPORT_SYMBOL_GPL(cryptd_skcipher_queued);

void cryptd_free_skcipher(struct cryptd_skcipher *tfm)
{
	struct cryptd_skcipher_ctx *ctx = crypto_skcipher_ctx(&tfm->base);

	if (refcount_dec_and_test(&ctx->refcnt))
		crypto_free_skcipher(&tfm->base);
}
EXPORT_SYMBOL_GPL(cryptd_free_skcipher);

struct cryptd_ahash *cryptd_alloc_ahash(const char *alg_name,
					u32 type, u32 mask)
{
	char cryptd_alg_name[CRYPTO_MAX_ALG_NAME];
	struct cryptd_hash_ctx *ctx;
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

	ctx = crypto_ahash_ctx(tfm);
	refcount_set(&ctx->refcnt, 1);

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

bool cryptd_ahash_queued(struct cryptd_ahash *tfm)
{
	struct cryptd_hash_ctx *ctx = crypto_ahash_ctx(&tfm->base);

	return refcount_read(&ctx->refcnt) - 1;
}
EXPORT_SYMBOL_GPL(cryptd_ahash_queued);

void cryptd_free_ahash(struct cryptd_ahash *tfm)
{
	struct cryptd_hash_ctx *ctx = crypto_ahash_ctx(&tfm->base);

	if (refcount_dec_and_test(&ctx->refcnt))
		crypto_free_ahash(&tfm->base);
}
EXPORT_SYMBOL_GPL(cryptd_free_ahash);

struct cryptd_aead *cryptd_alloc_aead(const char *alg_name,
						  u32 type, u32 mask)
{
	char cryptd_alg_name[CRYPTO_MAX_ALG_NAME];
	struct cryptd_aead_ctx *ctx;
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

	ctx = crypto_aead_ctx(tfm);
	refcount_set(&ctx->refcnt, 1);

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

bool cryptd_aead_queued(struct cryptd_aead *tfm)
{
	struct cryptd_aead_ctx *ctx = crypto_aead_ctx(&tfm->base);

	return refcount_read(&ctx->refcnt) - 1;
}
EXPORT_SYMBOL_GPL(cryptd_aead_queued);

void cryptd_free_aead(struct cryptd_aead *tfm)
{
	struct cryptd_aead_ctx *ctx = crypto_aead_ctx(&tfm->base);

	if (refcount_dec_and_test(&ctx->refcnt))
		crypto_free_aead(&tfm->base);
}
EXPORT_SYMBOL_GPL(cryptd_free_aead);

static int __init cryptd_init(void)
{
	int err;

	cryptd_wq = alloc_workqueue("cryptd", WQ_MEM_RECLAIM | WQ_CPU_INTENSIVE,
				    1);
	if (!cryptd_wq)
		return -ENOMEM;

	err = cryptd_init_queue(&queue, cryptd_max_cpu_qlen);
	if (err)
		goto err_destroy_wq;

	err = crypto_register_template(&cryptd_tmpl);
	if (err)
		goto err_fini_queue;

	return 0;

err_fini_queue:
	cryptd_fini_queue(&queue);
err_destroy_wq:
	destroy_workqueue(cryptd_wq);
	return err;
}

static void __exit cryptd_exit(void)
{
	destroy_workqueue(cryptd_wq);
	cryptd_fini_queue(&queue);
	crypto_unregister_template(&cryptd_tmpl);
}

subsys_initcall(cryptd_init);
module_exit(cryptd_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Software async crypto daemon");
MODULE_ALIAS_CRYPTO("cryptd");
