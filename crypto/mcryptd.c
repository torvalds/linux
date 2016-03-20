/*
 * Software multibuffer async crypto daemon.
 *
 * Copyright (c) 2014 Tim Chen <tim.c.chen@linux.intel.com>
 *
 * Adapted from crypto daemon.
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
#include <crypto/mcryptd.h>
#include <crypto/crypto_wq.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/hardirq.h>

#define MCRYPTD_MAX_CPU_QLEN 100
#define MCRYPTD_BATCH 9

static void *mcryptd_alloc_instance(struct crypto_alg *alg, unsigned int head,
				   unsigned int tail);

struct mcryptd_flush_list {
	struct list_head list;
	struct mutex lock;
};

static struct mcryptd_flush_list __percpu *mcryptd_flist;

struct hashd_instance_ctx {
	struct crypto_shash_spawn spawn;
	struct mcryptd_queue *queue;
};

static void mcryptd_queue_worker(struct work_struct *work);

void mcryptd_arm_flusher(struct mcryptd_alg_cstate *cstate, unsigned long delay)
{
	struct mcryptd_flush_list *flist;

	if (!cstate->flusher_engaged) {
		/* put the flusher on the flush list */
		flist = per_cpu_ptr(mcryptd_flist, smp_processor_id());
		mutex_lock(&flist->lock);
		list_add_tail(&cstate->flush_list, &flist->list);
		cstate->flusher_engaged = true;
		cstate->next_flush = jiffies + delay;
		queue_delayed_work_on(smp_processor_id(), kcrypto_wq,
			&cstate->flush, delay);
		mutex_unlock(&flist->lock);
	}
}
EXPORT_SYMBOL(mcryptd_arm_flusher);

static int mcryptd_init_queue(struct mcryptd_queue *queue,
			     unsigned int max_cpu_qlen)
{
	int cpu;
	struct mcryptd_cpu_queue *cpu_queue;

	queue->cpu_queue = alloc_percpu(struct mcryptd_cpu_queue);
	pr_debug("mqueue:%p mcryptd_cpu_queue %p\n", queue, queue->cpu_queue);
	if (!queue->cpu_queue)
		return -ENOMEM;
	for_each_possible_cpu(cpu) {
		cpu_queue = per_cpu_ptr(queue->cpu_queue, cpu);
		pr_debug("cpu_queue #%d %p\n", cpu, queue->cpu_queue);
		crypto_init_queue(&cpu_queue->queue, max_cpu_qlen);
		INIT_WORK(&cpu_queue->work, mcryptd_queue_worker);
	}
	return 0;
}

static void mcryptd_fini_queue(struct mcryptd_queue *queue)
{
	int cpu;
	struct mcryptd_cpu_queue *cpu_queue;

	for_each_possible_cpu(cpu) {
		cpu_queue = per_cpu_ptr(queue->cpu_queue, cpu);
		BUG_ON(cpu_queue->queue.qlen);
	}
	free_percpu(queue->cpu_queue);
}

static int mcryptd_enqueue_request(struct mcryptd_queue *queue,
				  struct crypto_async_request *request,
				  struct mcryptd_hash_request_ctx *rctx)
{
	int cpu, err;
	struct mcryptd_cpu_queue *cpu_queue;

	cpu = get_cpu();
	cpu_queue = this_cpu_ptr(queue->cpu_queue);
	rctx->tag.cpu = cpu;

	err = crypto_enqueue_request(&cpu_queue->queue, request);
	pr_debug("enqueue request: cpu %d cpu_queue %p request %p\n",
		 cpu, cpu_queue, request);
	queue_work_on(cpu, kcrypto_wq, &cpu_queue->work);
	put_cpu();

	return err;
}

/*
 * Try to opportunisticlly flush the partially completed jobs if
 * crypto daemon is the only task running.
 */
static void mcryptd_opportunistic_flush(void)
{
	struct mcryptd_flush_list *flist;
	struct mcryptd_alg_cstate *cstate;

	flist = per_cpu_ptr(mcryptd_flist, smp_processor_id());
	while (single_task_running()) {
		mutex_lock(&flist->lock);
		cstate = list_first_entry_or_null(&flist->list,
				struct mcryptd_alg_cstate, flush_list);
		if (!cstate || !cstate->flusher_engaged) {
			mutex_unlock(&flist->lock);
			return;
		}
		list_del(&cstate->flush_list);
		cstate->flusher_engaged = false;
		mutex_unlock(&flist->lock);
		cstate->alg_state->flusher(cstate);
	}
}

/*
 * Called in workqueue context, do one real cryption work (via
 * req->complete) and reschedule itself if there are more work to
 * do.
 */
static void mcryptd_queue_worker(struct work_struct *work)
{
	struct mcryptd_cpu_queue *cpu_queue;
	struct crypto_async_request *req, *backlog;
	int i;

	/*
	 * Need to loop through more than once for multi-buffer to
	 * be effective.
	 */

	cpu_queue = container_of(work, struct mcryptd_cpu_queue, work);
	i = 0;
	while (i < MCRYPTD_BATCH || single_task_running()) {
		/*
		 * preempt_disable/enable is used to prevent
		 * being preempted by mcryptd_enqueue_request()
		 */
		local_bh_disable();
		preempt_disable();
		backlog = crypto_get_backlog(&cpu_queue->queue);
		req = crypto_dequeue_request(&cpu_queue->queue);
		preempt_enable();
		local_bh_enable();

		if (!req) {
			mcryptd_opportunistic_flush();
			return;
		}

		if (backlog)
			backlog->complete(backlog, -EINPROGRESS);
		req->complete(req, 0);
		if (!cpu_queue->queue.qlen)
			return;
		++i;
	}
	if (cpu_queue->queue.qlen)
		queue_work(kcrypto_wq, &cpu_queue->work);
}

void mcryptd_flusher(struct work_struct *__work)
{
	struct	mcryptd_alg_cstate	*alg_cpu_state;
	struct	mcryptd_alg_state	*alg_state;
	struct	mcryptd_flush_list	*flist;
	int	cpu;

	cpu = smp_processor_id();
	alg_cpu_state = container_of(to_delayed_work(__work),
				     struct mcryptd_alg_cstate, flush);
	alg_state = alg_cpu_state->alg_state;
	if (alg_cpu_state->cpu != cpu)
		pr_debug("mcryptd error: work on cpu %d, should be cpu %d\n",
				cpu, alg_cpu_state->cpu);

	if (alg_cpu_state->flusher_engaged) {
		flist = per_cpu_ptr(mcryptd_flist, cpu);
		mutex_lock(&flist->lock);
		list_del(&alg_cpu_state->flush_list);
		alg_cpu_state->flusher_engaged = false;
		mutex_unlock(&flist->lock);
		alg_state->flusher(alg_cpu_state);
	}
}
EXPORT_SYMBOL_GPL(mcryptd_flusher);

static inline struct mcryptd_queue *mcryptd_get_queue(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct mcryptd_instance_ctx *ictx = crypto_instance_ctx(inst);

	return ictx->queue;
}

static void *mcryptd_alloc_instance(struct crypto_alg *alg, unsigned int head,
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
		    "mcryptd(%s)", alg->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
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

static inline void mcryptd_check_internal(struct rtattr **tb, u32 *type,
					  u32 *mask)
{
	struct crypto_attr_type *algt;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return;
	if ((algt->type & CRYPTO_ALG_INTERNAL))
		*type |= CRYPTO_ALG_INTERNAL;
	if ((algt->mask & CRYPTO_ALG_INTERNAL))
		*mask |= CRYPTO_ALG_INTERNAL;
}

static int mcryptd_hash_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct hashd_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct crypto_shash_spawn *spawn = &ictx->spawn;
	struct mcryptd_hash_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_shash *hash;

	hash = crypto_spawn_shash(spawn);
	if (IS_ERR(hash))
		return PTR_ERR(hash);

	ctx->child = hash;
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct mcryptd_hash_request_ctx) +
				 crypto_shash_descsize(hash));
	return 0;
}

static void mcryptd_hash_exit_tfm(struct crypto_tfm *tfm)
{
	struct mcryptd_hash_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_shash(ctx->child);
}

static int mcryptd_hash_setkey(struct crypto_ahash *parent,
				   const u8 *key, unsigned int keylen)
{
	struct mcryptd_hash_ctx *ctx   = crypto_ahash_ctx(parent);
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

static int mcryptd_hash_enqueue(struct ahash_request *req,
				crypto_completion_t complete)
{
	int ret;

	struct mcryptd_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct mcryptd_queue *queue =
		mcryptd_get_queue(crypto_ahash_tfm(tfm));

	rctx->complete = req->base.complete;
	req->base.complete = complete;

	ret = mcryptd_enqueue_request(queue, &req->base, rctx);

	return ret;
}

static void mcryptd_hash_init(struct crypto_async_request *req_async, int err)
{
	struct mcryptd_hash_ctx *ctx = crypto_tfm_ctx(req_async->tfm);
	struct crypto_shash *child = ctx->child;
	struct ahash_request *req = ahash_request_cast(req_async);
	struct mcryptd_hash_request_ctx *rctx = ahash_request_ctx(req);
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

static int mcryptd_hash_init_enqueue(struct ahash_request *req)
{
	return mcryptd_hash_enqueue(req, mcryptd_hash_init);
}

static void mcryptd_hash_update(struct crypto_async_request *req_async, int err)
{
	struct ahash_request *req = ahash_request_cast(req_async);
	struct mcryptd_hash_request_ctx *rctx = ahash_request_ctx(req);

	if (unlikely(err == -EINPROGRESS))
		goto out;

	err = shash_ahash_mcryptd_update(req, &rctx->desc);
	if (err) {
		req->base.complete = rctx->complete;
		goto out;
	}

	return;
out:
	local_bh_disable();
	rctx->complete(&req->base, err);
	local_bh_enable();
}

static int mcryptd_hash_update_enqueue(struct ahash_request *req)
{
	return mcryptd_hash_enqueue(req, mcryptd_hash_update);
}

static void mcryptd_hash_final(struct crypto_async_request *req_async, int err)
{
	struct ahash_request *req = ahash_request_cast(req_async);
	struct mcryptd_hash_request_ctx *rctx = ahash_request_ctx(req);

	if (unlikely(err == -EINPROGRESS))
		goto out;

	err = shash_ahash_mcryptd_final(req, &rctx->desc);
	if (err) {
		req->base.complete = rctx->complete;
		goto out;
	}

	return;
out:
	local_bh_disable();
	rctx->complete(&req->base, err);
	local_bh_enable();
}

static int mcryptd_hash_final_enqueue(struct ahash_request *req)
{
	return mcryptd_hash_enqueue(req, mcryptd_hash_final);
}

static void mcryptd_hash_finup(struct crypto_async_request *req_async, int err)
{
	struct ahash_request *req = ahash_request_cast(req_async);
	struct mcryptd_hash_request_ctx *rctx = ahash_request_ctx(req);

	if (unlikely(err == -EINPROGRESS))
		goto out;

	err = shash_ahash_mcryptd_finup(req, &rctx->desc);

	if (err) {
		req->base.complete = rctx->complete;
		goto out;
	}

	return;
out:
	local_bh_disable();
	rctx->complete(&req->base, err);
	local_bh_enable();
}

static int mcryptd_hash_finup_enqueue(struct ahash_request *req)
{
	return mcryptd_hash_enqueue(req, mcryptd_hash_finup);
}

static void mcryptd_hash_digest(struct crypto_async_request *req_async, int err)
{
	struct mcryptd_hash_ctx *ctx = crypto_tfm_ctx(req_async->tfm);
	struct crypto_shash *child = ctx->child;
	struct ahash_request *req = ahash_request_cast(req_async);
	struct mcryptd_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct shash_desc *desc = &rctx->desc;

	if (unlikely(err == -EINPROGRESS))
		goto out;

	desc->tfm = child;
	desc->flags = CRYPTO_TFM_REQ_MAY_SLEEP;  /* check this again */

	err = shash_ahash_mcryptd_digest(req, desc);

	if (err) {
		req->base.complete = rctx->complete;
		goto out;
	}

	return;
out:
	local_bh_disable();
	rctx->complete(&req->base, err);
	local_bh_enable();
}

static int mcryptd_hash_digest_enqueue(struct ahash_request *req)
{
	return mcryptd_hash_enqueue(req, mcryptd_hash_digest);
}

static int mcryptd_hash_export(struct ahash_request *req, void *out)
{
	struct mcryptd_hash_request_ctx *rctx = ahash_request_ctx(req);

	return crypto_shash_export(&rctx->desc, out);
}

static int mcryptd_hash_import(struct ahash_request *req, const void *in)
{
	struct mcryptd_hash_request_ctx *rctx = ahash_request_ctx(req);

	return crypto_shash_import(&rctx->desc, in);
}

static int mcryptd_create_hash(struct crypto_template *tmpl, struct rtattr **tb,
			      struct mcryptd_queue *queue)
{
	struct hashd_instance_ctx *ctx;
	struct ahash_instance *inst;
	struct shash_alg *salg;
	struct crypto_alg *alg;
	u32 type = 0;
	u32 mask = 0;
	int err;

	mcryptd_check_internal(tb, &type, &mask);

	salg = shash_attr_alg(tb[1], type, mask);
	if (IS_ERR(salg))
		return PTR_ERR(salg);

	alg = &salg->base;
	pr_debug("crypto: mcryptd hash alg: %s\n", alg->cra_name);
	inst = mcryptd_alloc_instance(alg, ahash_instance_headroom(),
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

	type = CRYPTO_ALG_ASYNC;
	if (alg->cra_flags & CRYPTO_ALG_INTERNAL)
		type |= CRYPTO_ALG_INTERNAL;
	inst->alg.halg.base.cra_flags = type;

	inst->alg.halg.digestsize = salg->digestsize;
	inst->alg.halg.statesize = salg->statesize;
	inst->alg.halg.base.cra_ctxsize = sizeof(struct mcryptd_hash_ctx);

	inst->alg.halg.base.cra_init = mcryptd_hash_init_tfm;
	inst->alg.halg.base.cra_exit = mcryptd_hash_exit_tfm;

	inst->alg.init   = mcryptd_hash_init_enqueue;
	inst->alg.update = mcryptd_hash_update_enqueue;
	inst->alg.final  = mcryptd_hash_final_enqueue;
	inst->alg.finup  = mcryptd_hash_finup_enqueue;
	inst->alg.export = mcryptd_hash_export;
	inst->alg.import = mcryptd_hash_import;
	inst->alg.setkey = mcryptd_hash_setkey;
	inst->alg.digest = mcryptd_hash_digest_enqueue;

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

static struct mcryptd_queue mqueue;

static int mcryptd_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct crypto_attr_type *algt;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	switch (algt->type & algt->mask & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_DIGEST:
		return mcryptd_create_hash(tmpl, tb, &mqueue);
	break;
	}

	return -EINVAL;
}

static void mcryptd_free(struct crypto_instance *inst)
{
	struct mcryptd_instance_ctx *ctx = crypto_instance_ctx(inst);
	struct hashd_instance_ctx *hctx = crypto_instance_ctx(inst);

	switch (inst->alg.cra_flags & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_AHASH:
		crypto_drop_shash(&hctx->spawn);
		kfree(ahash_instance(inst));
		return;
	default:
		crypto_drop_spawn(&ctx->spawn);
		kfree(inst);
	}
}

static struct crypto_template mcryptd_tmpl = {
	.name = "mcryptd",
	.create = mcryptd_create,
	.free = mcryptd_free,
	.module = THIS_MODULE,
};

struct mcryptd_ahash *mcryptd_alloc_ahash(const char *alg_name,
					u32 type, u32 mask)
{
	char mcryptd_alg_name[CRYPTO_MAX_ALG_NAME];
	struct crypto_ahash *tfm;

	if (snprintf(mcryptd_alg_name, CRYPTO_MAX_ALG_NAME,
		     "mcryptd(%s)", alg_name) >= CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-EINVAL);
	tfm = crypto_alloc_ahash(mcryptd_alg_name, type, mask);
	if (IS_ERR(tfm))
		return ERR_CAST(tfm);
	if (tfm->base.__crt_alg->cra_module != THIS_MODULE) {
		crypto_free_ahash(tfm);
		return ERR_PTR(-EINVAL);
	}

	return __mcryptd_ahash_cast(tfm);
}
EXPORT_SYMBOL_GPL(mcryptd_alloc_ahash);

int shash_ahash_mcryptd_digest(struct ahash_request *req,
			       struct shash_desc *desc)
{
	int err;

	err = crypto_shash_init(desc) ?:
	      shash_ahash_mcryptd_finup(req, desc);

	return err;
}
EXPORT_SYMBOL_GPL(shash_ahash_mcryptd_digest);

int shash_ahash_mcryptd_update(struct ahash_request *req,
			       struct shash_desc *desc)
{
	struct crypto_shash *tfm = desc->tfm;
	struct shash_alg *shash = crypto_shash_alg(tfm);

	/* alignment is to be done by multi-buffer crypto algorithm if needed */

	return shash->update(desc, NULL, 0);
}
EXPORT_SYMBOL_GPL(shash_ahash_mcryptd_update);

int shash_ahash_mcryptd_finup(struct ahash_request *req,
			      struct shash_desc *desc)
{
	struct crypto_shash *tfm = desc->tfm;
	struct shash_alg *shash = crypto_shash_alg(tfm);

	/* alignment is to be done by multi-buffer crypto algorithm if needed */

	return shash->finup(desc, NULL, 0, req->result);
}
EXPORT_SYMBOL_GPL(shash_ahash_mcryptd_finup);

int shash_ahash_mcryptd_final(struct ahash_request *req,
			      struct shash_desc *desc)
{
	struct crypto_shash *tfm = desc->tfm;
	struct shash_alg *shash = crypto_shash_alg(tfm);

	/* alignment is to be done by multi-buffer crypto algorithm if needed */

	return shash->final(desc, req->result);
}
EXPORT_SYMBOL_GPL(shash_ahash_mcryptd_final);

struct crypto_shash *mcryptd_ahash_child(struct mcryptd_ahash *tfm)
{
	struct mcryptd_hash_ctx *ctx = crypto_ahash_ctx(&tfm->base);

	return ctx->child;
}
EXPORT_SYMBOL_GPL(mcryptd_ahash_child);

struct shash_desc *mcryptd_shash_desc(struct ahash_request *req)
{
	struct mcryptd_hash_request_ctx *rctx = ahash_request_ctx(req);
	return &rctx->desc;
}
EXPORT_SYMBOL_GPL(mcryptd_shash_desc);

void mcryptd_free_ahash(struct mcryptd_ahash *tfm)
{
	crypto_free_ahash(&tfm->base);
}
EXPORT_SYMBOL_GPL(mcryptd_free_ahash);


static int __init mcryptd_init(void)
{
	int err, cpu;
	struct mcryptd_flush_list *flist;

	mcryptd_flist = alloc_percpu(struct mcryptd_flush_list);
	for_each_possible_cpu(cpu) {
		flist = per_cpu_ptr(mcryptd_flist, cpu);
		INIT_LIST_HEAD(&flist->list);
		mutex_init(&flist->lock);
	}

	err = mcryptd_init_queue(&mqueue, MCRYPTD_MAX_CPU_QLEN);
	if (err) {
		free_percpu(mcryptd_flist);
		return err;
	}

	err = crypto_register_template(&mcryptd_tmpl);
	if (err) {
		mcryptd_fini_queue(&mqueue);
		free_percpu(mcryptd_flist);
	}

	return err;
}

static void __exit mcryptd_exit(void)
{
	mcryptd_fini_queue(&mqueue);
	crypto_unregister_template(&mcryptd_tmpl);
	free_percpu(mcryptd_flist);
}

subsys_initcall(mcryptd_init);
module_exit(mcryptd_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Software async multibuffer crypto daemon");
MODULE_ALIAS_CRYPTO("mcryptd");
