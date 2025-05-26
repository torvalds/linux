// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Asynchronous Compression operations
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Weigang Li <weigang.li@intel.com>
 *          Giovanni Cabiddu <giovanni.cabiddu@intel.com>
 */

#include <crypto/internal/acompress.h>
#include <crypto/scatterwalk.h>
#include <linux/cryptouser.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <net/netlink.h>

#include "compress.h"

struct crypto_scomp;

enum {
	ACOMP_WALK_SLEEP = 1 << 0,
	ACOMP_WALK_SRC_LINEAR = 1 << 1,
	ACOMP_WALK_DST_LINEAR = 1 << 2,
};

static const struct crypto_type crypto_acomp_type;

static void acomp_reqchain_done(void *data, int err);

static inline struct acomp_alg *__crypto_acomp_alg(struct crypto_alg *alg)
{
	return container_of(alg, struct acomp_alg, calg.base);
}

static inline struct acomp_alg *crypto_acomp_alg(struct crypto_acomp *tfm)
{
	return __crypto_acomp_alg(crypto_acomp_tfm(tfm)->__crt_alg);
}

static int __maybe_unused crypto_acomp_report(
	struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_acomp racomp;

	memset(&racomp, 0, sizeof(racomp));

	strscpy(racomp.type, "acomp", sizeof(racomp.type));

	return nla_put(skb, CRYPTOCFGA_REPORT_ACOMP, sizeof(racomp), &racomp);
}

static void crypto_acomp_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;

static void crypto_acomp_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_puts(m, "type         : acomp\n");
}

static void crypto_acomp_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_acomp *acomp = __crypto_acomp_tfm(tfm);
	struct acomp_alg *alg = crypto_acomp_alg(acomp);

	if (alg->exit)
		alg->exit(acomp);

	if (acomp_is_async(acomp))
		crypto_free_acomp(crypto_acomp_fb(acomp));
}

static int crypto_acomp_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_acomp *acomp = __crypto_acomp_tfm(tfm);
	struct acomp_alg *alg = crypto_acomp_alg(acomp);
	struct crypto_acomp *fb = NULL;
	int err;

	if (tfm->__crt_alg->cra_type != &crypto_acomp_type)
		return crypto_init_scomp_ops_async(tfm);

	if (acomp_is_async(acomp)) {
		fb = crypto_alloc_acomp(crypto_acomp_alg_name(acomp), 0,
					CRYPTO_ALG_ASYNC);
		if (IS_ERR(fb))
			return PTR_ERR(fb);

		err = -EINVAL;
		if (crypto_acomp_reqsize(fb) > MAX_SYNC_COMP_REQSIZE)
			goto out_free_fb;

		tfm->fb = crypto_acomp_tfm(fb);
	}

	acomp->compress = alg->compress;
	acomp->decompress = alg->decompress;
	acomp->reqsize = alg->base.cra_reqsize;

	acomp->base.exit = crypto_acomp_exit_tfm;

	if (!alg->init)
		return 0;

	err = alg->init(acomp);
	if (err)
		goto out_free_fb;

	return 0;

out_free_fb:
	crypto_free_acomp(fb);
	return err;
}

static unsigned int crypto_acomp_extsize(struct crypto_alg *alg)
{
	int extsize = crypto_alg_extsize(alg);

	if (alg->cra_type != &crypto_acomp_type)
		extsize += sizeof(struct crypto_scomp *);

	return extsize;
}

static const struct crypto_type crypto_acomp_type = {
	.extsize = crypto_acomp_extsize,
	.init_tfm = crypto_acomp_init_tfm,
#ifdef CONFIG_PROC_FS
	.show = crypto_acomp_show,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_USER)
	.report = crypto_acomp_report,
#endif
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_ACOMPRESS_MASK,
	.type = CRYPTO_ALG_TYPE_ACOMPRESS,
	.tfmsize = offsetof(struct crypto_acomp, base),
	.algsize = offsetof(struct acomp_alg, base),
};

struct crypto_acomp *crypto_alloc_acomp(const char *alg_name, u32 type,
					u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_acomp_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_acomp);

struct crypto_acomp *crypto_alloc_acomp_node(const char *alg_name, u32 type,
					u32 mask, int node)
{
	return crypto_alloc_tfm_node(alg_name, &crypto_acomp_type, type, mask,
				node);
}
EXPORT_SYMBOL_GPL(crypto_alloc_acomp_node);

static void acomp_save_req(struct acomp_req *req, crypto_completion_t cplt)
{
	struct acomp_req_chain *state = &req->chain;

	state->compl = req->base.complete;
	state->data = req->base.data;
	req->base.complete = cplt;
	req->base.data = state;
}

static void acomp_restore_req(struct acomp_req *req)
{
	struct acomp_req_chain *state = req->base.data;

	req->base.complete = state->compl;
	req->base.data = state->data;
}

static void acomp_reqchain_virt(struct acomp_req *req)
{
	struct acomp_req_chain *state = &req->chain;
	unsigned int slen = req->slen;
	unsigned int dlen = req->dlen;

	if (state->flags & CRYPTO_ACOMP_REQ_SRC_VIRT)
		acomp_request_set_src_dma(req, state->src, slen);
	if (state->flags & CRYPTO_ACOMP_REQ_DST_VIRT)
		acomp_request_set_dst_dma(req, state->dst, dlen);
}

static void acomp_virt_to_sg(struct acomp_req *req)
{
	struct acomp_req_chain *state = &req->chain;

	state->flags = req->base.flags & (CRYPTO_ACOMP_REQ_SRC_VIRT |
					  CRYPTO_ACOMP_REQ_DST_VIRT);

	if (acomp_request_src_isvirt(req)) {
		unsigned int slen = req->slen;
		const u8 *svirt = req->svirt;

		state->src = svirt;
		sg_init_one(&state->ssg, svirt, slen);
		acomp_request_set_src_sg(req, &state->ssg, slen);
	}

	if (acomp_request_dst_isvirt(req)) {
		unsigned int dlen = req->dlen;
		u8 *dvirt = req->dvirt;

		state->dst = dvirt;
		sg_init_one(&state->dsg, dvirt, dlen);
		acomp_request_set_dst_sg(req, &state->dsg, dlen);
	}
}

static int acomp_do_nondma(struct acomp_req *req, bool comp)
{
	ACOMP_FBREQ_ON_STACK(fbreq, req);
	int err;

	if (comp)
		err = crypto_acomp_compress(fbreq);
	else
		err = crypto_acomp_decompress(fbreq);

	req->dlen = fbreq->dlen;
	return err;
}

static int acomp_do_one_req(struct acomp_req *req, bool comp)
{
	if (acomp_request_isnondma(req))
		return acomp_do_nondma(req, comp);

	acomp_virt_to_sg(req);
	return comp ? crypto_acomp_reqtfm(req)->compress(req) :
		      crypto_acomp_reqtfm(req)->decompress(req);
}

static int acomp_reqchain_finish(struct acomp_req *req, int err)
{
	acomp_reqchain_virt(req);
	acomp_restore_req(req);
	return err;
}

static void acomp_reqchain_done(void *data, int err)
{
	struct acomp_req *req = data;
	crypto_completion_t compl;

	compl = req->chain.compl;
	data = req->chain.data;

	if (err == -EINPROGRESS)
		goto notify;

	err = acomp_reqchain_finish(req, err);

notify:
	compl(data, err);
}

static int acomp_do_req_chain(struct acomp_req *req, bool comp)
{
	int err;

	acomp_save_req(req, acomp_reqchain_done);

	err = acomp_do_one_req(req, comp);
	if (err == -EBUSY || err == -EINPROGRESS)
		return err;

	return acomp_reqchain_finish(req, err);
}

int crypto_acomp_compress(struct acomp_req *req)
{
	struct crypto_acomp *tfm = crypto_acomp_reqtfm(req);

	if (acomp_req_on_stack(req) && acomp_is_async(tfm))
		return -EAGAIN;
	if (crypto_acomp_req_virt(tfm) || acomp_request_issg(req))
		return crypto_acomp_reqtfm(req)->compress(req);
	return acomp_do_req_chain(req, true);
}
EXPORT_SYMBOL_GPL(crypto_acomp_compress);

int crypto_acomp_decompress(struct acomp_req *req)
{
	struct crypto_acomp *tfm = crypto_acomp_reqtfm(req);

	if (acomp_req_on_stack(req) && acomp_is_async(tfm))
		return -EAGAIN;
	if (crypto_acomp_req_virt(tfm) || acomp_request_issg(req))
		return crypto_acomp_reqtfm(req)->decompress(req);
	return acomp_do_req_chain(req, false);
}
EXPORT_SYMBOL_GPL(crypto_acomp_decompress);

void comp_prepare_alg(struct comp_alg_common *alg)
{
	struct crypto_alg *base = &alg->base;

	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
}

int crypto_register_acomp(struct acomp_alg *alg)
{
	struct crypto_alg *base = &alg->calg.base;

	comp_prepare_alg(&alg->calg);

	base->cra_type = &crypto_acomp_type;
	base->cra_flags |= CRYPTO_ALG_TYPE_ACOMPRESS;

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_acomp);

void crypto_unregister_acomp(struct acomp_alg *alg)
{
	crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_acomp);

int crypto_register_acomps(struct acomp_alg *algs, int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = crypto_register_acomp(&algs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (--i; i >= 0; --i)
		crypto_unregister_acomp(&algs[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_register_acomps);

void crypto_unregister_acomps(struct acomp_alg *algs, int count)
{
	int i;

	for (i = count - 1; i >= 0; --i)
		crypto_unregister_acomp(&algs[i]);
}
EXPORT_SYMBOL_GPL(crypto_unregister_acomps);

static void acomp_stream_workfn(struct work_struct *work)
{
	struct crypto_acomp_streams *s =
		container_of(work, struct crypto_acomp_streams, stream_work);
	struct crypto_acomp_stream __percpu *streams = s->streams;
	int cpu;

	for_each_cpu(cpu, &s->stream_want) {
		struct crypto_acomp_stream *ps;
		void *ctx;

		ps = per_cpu_ptr(streams, cpu);
		if (ps->ctx)
			continue;

		ctx = s->alloc_ctx();
		if (IS_ERR(ctx))
			break;

		spin_lock_bh(&ps->lock);
		ps->ctx = ctx;
		spin_unlock_bh(&ps->lock);

		cpumask_clear_cpu(cpu, &s->stream_want);
	}
}

void crypto_acomp_free_streams(struct crypto_acomp_streams *s)
{
	struct crypto_acomp_stream __percpu *streams = s->streams;
	void (*free_ctx)(void *);
	int i;

	s->streams = NULL;
	if (!streams)
		return;

	cancel_work_sync(&s->stream_work);
	free_ctx = s->free_ctx;

	for_each_possible_cpu(i) {
		struct crypto_acomp_stream *ps = per_cpu_ptr(streams, i);

		if (!ps->ctx)
			continue;

		free_ctx(ps->ctx);
	}

	free_percpu(streams);
}
EXPORT_SYMBOL_GPL(crypto_acomp_free_streams);

int crypto_acomp_alloc_streams(struct crypto_acomp_streams *s)
{
	struct crypto_acomp_stream __percpu *streams;
	struct crypto_acomp_stream *ps;
	unsigned int i;
	void *ctx;

	if (s->streams)
		return 0;

	streams = alloc_percpu(struct crypto_acomp_stream);
	if (!streams)
		return -ENOMEM;

	ctx = s->alloc_ctx();
	if (IS_ERR(ctx)) {
		free_percpu(streams);
		return PTR_ERR(ctx);
	}

	i = cpumask_first(cpu_possible_mask);
	ps = per_cpu_ptr(streams, i);
	ps->ctx = ctx;

	for_each_possible_cpu(i) {
		ps = per_cpu_ptr(streams, i);
		spin_lock_init(&ps->lock);
	}

	s->streams = streams;

	INIT_WORK(&s->stream_work, acomp_stream_workfn);
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_acomp_alloc_streams);

struct crypto_acomp_stream *crypto_acomp_lock_stream_bh(
	struct crypto_acomp_streams *s) __acquires(stream)
{
	struct crypto_acomp_stream __percpu *streams = s->streams;
	int cpu = raw_smp_processor_id();
	struct crypto_acomp_stream *ps;

	ps = per_cpu_ptr(streams, cpu);
	spin_lock_bh(&ps->lock);
	if (likely(ps->ctx))
		return ps;
	spin_unlock(&ps->lock);

	cpumask_set_cpu(cpu, &s->stream_want);
	schedule_work(&s->stream_work);

	ps = per_cpu_ptr(streams, cpumask_first(cpu_possible_mask));
	spin_lock(&ps->lock);
	return ps;
}
EXPORT_SYMBOL_GPL(crypto_acomp_lock_stream_bh);

void acomp_walk_done_src(struct acomp_walk *walk, int used)
{
	walk->slen -= used;
	if ((walk->flags & ACOMP_WALK_SRC_LINEAR))
		scatterwalk_advance(&walk->in, used);
	else
		scatterwalk_done_src(&walk->in, used);

	if ((walk->flags & ACOMP_WALK_SLEEP))
		cond_resched();
}
EXPORT_SYMBOL_GPL(acomp_walk_done_src);

void acomp_walk_done_dst(struct acomp_walk *walk, int used)
{
	walk->dlen -= used;
	if ((walk->flags & ACOMP_WALK_DST_LINEAR))
		scatterwalk_advance(&walk->out, used);
	else
		scatterwalk_done_dst(&walk->out, used);

	if ((walk->flags & ACOMP_WALK_SLEEP))
		cond_resched();
}
EXPORT_SYMBOL_GPL(acomp_walk_done_dst);

int acomp_walk_next_src(struct acomp_walk *walk)
{
	unsigned int slen = walk->slen;
	unsigned int max = UINT_MAX;

	if (!preempt_model_preemptible() && (walk->flags & ACOMP_WALK_SLEEP))
		max = PAGE_SIZE;
	if ((walk->flags & ACOMP_WALK_SRC_LINEAR)) {
		walk->in.__addr = (void *)(((u8 *)walk->in.sg) +
					   walk->in.offset);
		return min(slen, max);
	}

	return slen ? scatterwalk_next(&walk->in, slen) : 0;
}
EXPORT_SYMBOL_GPL(acomp_walk_next_src);

int acomp_walk_next_dst(struct acomp_walk *walk)
{
	unsigned int dlen = walk->dlen;
	unsigned int max = UINT_MAX;

	if (!preempt_model_preemptible() && (walk->flags & ACOMP_WALK_SLEEP))
		max = PAGE_SIZE;
	if ((walk->flags & ACOMP_WALK_DST_LINEAR)) {
		walk->out.__addr = (void *)(((u8 *)walk->out.sg) +
					    walk->out.offset);
		return min(dlen, max);
	}

	return dlen ? scatterwalk_next(&walk->out, dlen) : 0;
}
EXPORT_SYMBOL_GPL(acomp_walk_next_dst);

int acomp_walk_virt(struct acomp_walk *__restrict walk,
		    struct acomp_req *__restrict req, bool atomic)
{
	struct scatterlist *src = req->src;
	struct scatterlist *dst = req->dst;

	walk->slen = req->slen;
	walk->dlen = req->dlen;

	if (!walk->slen || !walk->dlen)
		return -EINVAL;

	walk->flags = 0;
	if ((req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) && !atomic)
		walk->flags |= ACOMP_WALK_SLEEP;
	if ((req->base.flags & CRYPTO_ACOMP_REQ_SRC_VIRT))
		walk->flags |= ACOMP_WALK_SRC_LINEAR;
	if ((req->base.flags & CRYPTO_ACOMP_REQ_DST_VIRT))
		walk->flags |= ACOMP_WALK_DST_LINEAR;

	if ((walk->flags & ACOMP_WALK_SRC_LINEAR)) {
		walk->in.sg = (void *)req->svirt;
		walk->in.offset = 0;
	} else
		scatterwalk_start(&walk->in, src);
	if ((walk->flags & ACOMP_WALK_DST_LINEAR)) {
		walk->out.sg = (void *)req->dvirt;
		walk->out.offset = 0;
	} else
		scatterwalk_start(&walk->out, dst);

	return 0;
}
EXPORT_SYMBOL_GPL(acomp_walk_virt);

struct acomp_req *acomp_request_clone(struct acomp_req *req,
				      size_t total, gfp_t gfp)
{
	struct acomp_req *nreq;

	nreq = container_of(crypto_request_clone(&req->base, total, gfp),
			    struct acomp_req, base);
	if (nreq == req)
		return req;

	if (req->src == &req->chain.ssg)
		nreq->src = &nreq->chain.ssg;
	if (req->dst == &req->chain.dsg)
		nreq->dst = &nreq->chain.dsg;
	return nreq;
}
EXPORT_SYMBOL_GPL(acomp_request_clone);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Asynchronous compression type");
