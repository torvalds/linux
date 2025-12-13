// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Synchronous Compression operations
 *
 * Copyright 2015 LG Electronics Inc.
 * Copyright (c) 2016, Intel Corporation
 * Author: Giovanni Cabiddu <giovanni.cabiddu@intel.com>
 */

#include <crypto/internal/scompress.h>
#include <crypto/scatterwalk.h>
#include <linux/cpumask.h>
#include <linux/cryptouser.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <net/netlink.h>

#include "compress.h"

struct scomp_scratch {
	spinlock_t	lock;
	union {
		void	*src;
		unsigned long saddr;
	};
};

static DEFINE_PER_CPU(struct scomp_scratch, scomp_scratch) = {
	.lock = __SPIN_LOCK_UNLOCKED(scomp_scratch.lock),
};

static const struct crypto_type crypto_scomp_type;
static int scomp_scratch_users;
static DEFINE_MUTEX(scomp_lock);

static cpumask_t scomp_scratch_want;
static void scomp_scratch_workfn(struct work_struct *work);
static DECLARE_WORK(scomp_scratch_work, scomp_scratch_workfn);

static int __maybe_unused crypto_scomp_report(
	struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_comp rscomp;

	memset(&rscomp, 0, sizeof(rscomp));

	strscpy(rscomp.type, "scomp", sizeof(rscomp.type));

	return nla_put(skb, CRYPTOCFGA_REPORT_COMPRESS,
		       sizeof(rscomp), &rscomp);
}

static void crypto_scomp_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;

static void crypto_scomp_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_puts(m, "type         : scomp\n");
}

static void crypto_scomp_free_scratches(void)
{
	struct scomp_scratch *scratch;
	int i;

	for_each_possible_cpu(i) {
		scratch = per_cpu_ptr(&scomp_scratch, i);

		free_page(scratch->saddr);
		scratch->src = NULL;
	}
}

static int scomp_alloc_scratch(struct scomp_scratch *scratch, int cpu)
{
	int node = cpu_to_node(cpu);
	struct page *page;

	page = alloc_pages_node(node, GFP_KERNEL, 0);
	if (!page)
		return -ENOMEM;
	spin_lock_bh(&scratch->lock);
	scratch->src = page_address(page);
	spin_unlock_bh(&scratch->lock);
	return 0;
}

static void scomp_scratch_workfn(struct work_struct *work)
{
	int cpu;

	for_each_cpu(cpu, &scomp_scratch_want) {
		struct scomp_scratch *scratch;

		scratch = per_cpu_ptr(&scomp_scratch, cpu);
		if (scratch->src)
			continue;
		if (scomp_alloc_scratch(scratch, cpu))
			break;

		cpumask_clear_cpu(cpu, &scomp_scratch_want);
	}
}

static int crypto_scomp_alloc_scratches(void)
{
	unsigned int i = cpumask_first(cpu_possible_mask);
	struct scomp_scratch *scratch;

	scratch = per_cpu_ptr(&scomp_scratch, i);
	return scomp_alloc_scratch(scratch, i);
}

static int crypto_scomp_init_tfm(struct crypto_tfm *tfm)
{
	struct scomp_alg *alg = crypto_scomp_alg(__crypto_scomp_tfm(tfm));
	int ret = 0;

	mutex_lock(&scomp_lock);
	ret = crypto_acomp_alloc_streams(&alg->streams);
	if (ret)
		goto unlock;
	if (!scomp_scratch_users++) {
		ret = crypto_scomp_alloc_scratches();
		if (ret)
			scomp_scratch_users--;
	}
unlock:
	mutex_unlock(&scomp_lock);

	return ret;
}

static struct scomp_scratch *scomp_lock_scratch(void) __acquires(scratch)
{
	int cpu = raw_smp_processor_id();
	struct scomp_scratch *scratch;

	scratch = per_cpu_ptr(&scomp_scratch, cpu);
	spin_lock(&scratch->lock);
	if (likely(scratch->src))
		return scratch;
	spin_unlock(&scratch->lock);

	cpumask_set_cpu(cpu, &scomp_scratch_want);
	schedule_work(&scomp_scratch_work);

	scratch = per_cpu_ptr(&scomp_scratch, cpumask_first(cpu_possible_mask));
	spin_lock(&scratch->lock);
	return scratch;
}

static inline void scomp_unlock_scratch(struct scomp_scratch *scratch)
	__releases(scratch)
{
	spin_unlock(&scratch->lock);
}

static int scomp_acomp_comp_decomp(struct acomp_req *req, int dir)
{
	struct crypto_acomp *tfm = crypto_acomp_reqtfm(req);
	struct crypto_scomp **tfm_ctx = acomp_tfm_ctx(tfm);
	bool src_isvirt = acomp_request_src_isvirt(req);
	bool dst_isvirt = acomp_request_dst_isvirt(req);
	struct crypto_scomp *scomp = *tfm_ctx;
	struct crypto_acomp_stream *stream;
	struct scomp_scratch *scratch;
	unsigned int slen = req->slen;
	unsigned int dlen = req->dlen;
	struct page *spage, *dpage;
	unsigned int n;
	const u8 *src;
	size_t soff;
	size_t doff;
	u8 *dst;
	int ret;

	if (!req->src || !slen)
		return -EINVAL;

	if (!req->dst || !dlen)
		return -EINVAL;

	if (dst_isvirt)
		dst = req->dvirt;
	else {
		if (dlen <= req->dst->length) {
			dpage = sg_page(req->dst);
			doff = req->dst->offset;
		} else
			return -ENOSYS;

		dpage += doff / PAGE_SIZE;
		doff = offset_in_page(doff);

		n = (dlen - 1) / PAGE_SIZE;
		n += (offset_in_page(dlen - 1) + doff) / PAGE_SIZE;
		if (PageHighMem(dpage + n) &&
		    size_add(doff, dlen) > PAGE_SIZE)
			return -ENOSYS;
		dst = kmap_local_page(dpage) + doff;
	}

	if (src_isvirt)
		src = req->svirt;
	else {
		src = NULL;
		do {
			if (slen <= req->src->length) {
				spage = sg_page(req->src);
				soff = req->src->offset;
			} else
				break;

			spage = spage + soff / PAGE_SIZE;
			soff = offset_in_page(soff);

			n = (slen - 1) / PAGE_SIZE;
			n += (offset_in_page(slen - 1) + soff) / PAGE_SIZE;
			if (PageHighMem(spage + n) &&
			    size_add(soff, slen) > PAGE_SIZE)
				break;
			src = kmap_local_page(spage) + soff;
		} while (0);
	}

	stream = crypto_acomp_lock_stream_bh(&crypto_scomp_alg(scomp)->streams);

	if (!src_isvirt && !src) {
		const u8 *src;

		scratch = scomp_lock_scratch();
		src = scratch->src;
		memcpy_from_sglist(scratch->src, req->src, 0, slen);

		if (dir)
			ret = crypto_scomp_compress(scomp, src, slen,
						    dst, &dlen, stream->ctx);
		else
			ret = crypto_scomp_decompress(scomp, src, slen,
						      dst, &dlen, stream->ctx);

		scomp_unlock_scratch(scratch);
	} else if (dir)
		ret = crypto_scomp_compress(scomp, src, slen,
					    dst, &dlen, stream->ctx);
	else
		ret = crypto_scomp_decompress(scomp, src, slen,
					      dst, &dlen, stream->ctx);

	crypto_acomp_unlock_stream_bh(stream);

	req->dlen = dlen;

	if (!src_isvirt && src)
		kunmap_local(src);
	if (!dst_isvirt) {
		kunmap_local(dst);
		dlen += doff;
		for (;;) {
			flush_dcache_page(dpage);
			if (dlen <= PAGE_SIZE)
				break;
			dlen -= PAGE_SIZE;
			dpage++;
		}
	}

	return ret;
}

static int scomp_acomp_compress(struct acomp_req *req)
{
	return scomp_acomp_comp_decomp(req, 1);
}

static int scomp_acomp_decompress(struct acomp_req *req)
{
	return scomp_acomp_comp_decomp(req, 0);
}

static void crypto_exit_scomp_ops_async(struct crypto_tfm *tfm)
{
	struct crypto_scomp **ctx = crypto_tfm_ctx(tfm);

	crypto_free_scomp(*ctx);

	flush_work(&scomp_scratch_work);
	mutex_lock(&scomp_lock);
	if (!--scomp_scratch_users)
		crypto_scomp_free_scratches();
	mutex_unlock(&scomp_lock);
}

int crypto_init_scomp_ops_async(struct crypto_tfm *tfm)
{
	struct crypto_alg *calg = tfm->__crt_alg;
	struct crypto_acomp *crt = __crypto_acomp_tfm(tfm);
	struct crypto_scomp **ctx = crypto_tfm_ctx(tfm);
	struct crypto_scomp *scomp;

	if (!crypto_mod_get(calg))
		return -EAGAIN;

	scomp = crypto_create_tfm(calg, &crypto_scomp_type);
	if (IS_ERR(scomp)) {
		crypto_mod_put(calg);
		return PTR_ERR(scomp);
	}

	*ctx = scomp;
	tfm->exit = crypto_exit_scomp_ops_async;

	crt->compress = scomp_acomp_compress;
	crt->decompress = scomp_acomp_decompress;

	return 0;
}

static void crypto_scomp_destroy(struct crypto_alg *alg)
{
	struct scomp_alg *scomp = __crypto_scomp_alg(alg);

	crypto_acomp_free_streams(&scomp->streams);
}

static const struct crypto_type crypto_scomp_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_scomp_init_tfm,
	.destroy = crypto_scomp_destroy,
#ifdef CONFIG_PROC_FS
	.show = crypto_scomp_show,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_USER)
	.report = crypto_scomp_report,
#endif
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_MASK,
	.type = CRYPTO_ALG_TYPE_SCOMPRESS,
	.tfmsize = offsetof(struct crypto_scomp, base),
	.algsize = offsetof(struct scomp_alg, base),
};

static void scomp_prepare_alg(struct scomp_alg *alg)
{
	struct crypto_alg *base = &alg->calg.base;

	comp_prepare_alg(&alg->calg);

	base->cra_flags |= CRYPTO_ALG_REQ_VIRT;
}

int crypto_register_scomp(struct scomp_alg *alg)
{
	struct crypto_alg *base = &alg->calg.base;

	scomp_prepare_alg(alg);

	base->cra_type = &crypto_scomp_type;
	base->cra_flags |= CRYPTO_ALG_TYPE_SCOMPRESS;

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_scomp);

void crypto_unregister_scomp(struct scomp_alg *alg)
{
	crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_scomp);

int crypto_register_scomps(struct scomp_alg *algs, int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = crypto_register_scomp(&algs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (--i; i >= 0; --i)
		crypto_unregister_scomp(&algs[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_register_scomps);

void crypto_unregister_scomps(struct scomp_alg *algs, int count)
{
	int i;

	for (i = count - 1; i >= 0; --i)
		crypto_unregister_scomp(&algs[i]);
}
EXPORT_SYMBOL_GPL(crypto_unregister_scomps);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synchronous compression type");
