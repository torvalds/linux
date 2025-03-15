// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Synchronous Compression operations
 *
 * Copyright 2015 LG Electronics Inc.
 * Copyright (c) 2016, Intel Corporation
 * Author: Giovanni Cabiddu <giovanni.cabiddu@intel.com>
 */

#include <crypto/internal/acompress.h>
#include <crypto/internal/scompress.h>
#include <crypto/scatterwalk.h>
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
#include <linux/vmalloc.h>
#include <net/netlink.h>

#include "compress.h"

#define SCOMP_SCRATCH_SIZE 65400

struct scomp_scratch {
	spinlock_t	lock;
	union {
		void	*src;
		unsigned long saddr;
	};
	void		*dst;
};

static DEFINE_PER_CPU(struct scomp_scratch, scomp_scratch) = {
	.lock = __SPIN_LOCK_UNLOCKED(scomp_scratch.lock),
};

static const struct crypto_type crypto_scomp_type;
static int scomp_scratch_users;
static DEFINE_MUTEX(scomp_lock);

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
		vfree(scratch->dst);
		scratch->src = NULL;
		scratch->dst = NULL;
	}
}

static int crypto_scomp_alloc_scratches(void)
{
	struct scomp_scratch *scratch;
	int i;

	for_each_possible_cpu(i) {
		struct page *page;
		void *mem;

		scratch = per_cpu_ptr(&scomp_scratch, i);

		page = alloc_pages_node(cpu_to_node(i), GFP_KERNEL, 0);
		if (!page)
			goto error;
		scratch->src = page_address(page);
		mem = vmalloc_node(SCOMP_SCRATCH_SIZE, cpu_to_node(i));
		if (!mem)
			goto error;
		scratch->dst = mem;
	}
	return 0;
error:
	crypto_scomp_free_scratches();
	return -ENOMEM;
}

static void scomp_free_streams(struct scomp_alg *alg)
{
	struct crypto_acomp_stream __percpu *stream = alg->stream;
	int i;

	for_each_possible_cpu(i) {
		struct crypto_acomp_stream *ps = per_cpu_ptr(stream, i);

		if (!ps->ctx)
			break;

		alg->free_ctx(ps);
	}

	free_percpu(stream);
}

static int scomp_alloc_streams(struct scomp_alg *alg)
{
	struct crypto_acomp_stream __percpu *stream;
	int i;

	stream = alloc_percpu(struct crypto_acomp_stream);
	if (!stream)
		return -ENOMEM;

	for_each_possible_cpu(i) {
		struct crypto_acomp_stream *ps = per_cpu_ptr(stream, i);

		ps->ctx = alg->alloc_ctx();
		if (IS_ERR(ps->ctx)) {
			scomp_free_streams(alg);
			return PTR_ERR(ps->ctx);
		}

		spin_lock_init(&ps->lock);
	}

	alg->stream = stream;
	return 0;
}

static int crypto_scomp_init_tfm(struct crypto_tfm *tfm)
{
	struct scomp_alg *alg = crypto_scomp_alg(__crypto_scomp_tfm(tfm));
	int ret = 0;

	mutex_lock(&scomp_lock);
	if (!alg->stream) {
		ret = scomp_alloc_streams(alg);
		if (ret)
			goto unlock;
	}
	if (!scomp_scratch_users++)
		ret = crypto_scomp_alloc_scratches();
unlock:
	mutex_unlock(&scomp_lock);

	return ret;
}

static int scomp_acomp_comp_decomp(struct acomp_req *req, int dir)
{
	struct scomp_scratch *scratch = raw_cpu_ptr(&scomp_scratch);
	struct crypto_acomp *tfm = crypto_acomp_reqtfm(req);
	struct crypto_scomp **tfm_ctx = acomp_tfm_ctx(tfm);
	struct crypto_scomp *scomp = *tfm_ctx;
	struct crypto_acomp_stream *stream;
	unsigned int slen = req->slen;
	unsigned int dlen = req->dlen;
	struct page *spage, *dpage;
	unsigned int soff, doff;
	void *src, *dst;
	unsigned int n;
	int ret;

	if (!req->src || !slen)
		return -EINVAL;

	if (!req->dst || !dlen)
		return -EINVAL;

	soff = req->src->offset;
	spage = nth_page(sg_page(req->src), soff / PAGE_SIZE);
	soff = offset_in_page(soff);

	n = slen / PAGE_SIZE;
	n += (offset_in_page(slen) + soff - 1) / PAGE_SIZE;
	if (slen <= req->src->length && (!PageHighMem(nth_page(spage, n)) ||
					 size_add(soff, slen) <= PAGE_SIZE))
		src = kmap_local_page(spage) + soff;
	else
		src = scratch->src;

	doff = req->dst->offset;
	dpage = nth_page(sg_page(req->dst), doff / PAGE_SIZE);
	doff = offset_in_page(doff);

	n = dlen / PAGE_SIZE;
	n += (offset_in_page(dlen) + doff - 1) / PAGE_SIZE;
	if (dlen <= req->dst->length && (!PageHighMem(nth_page(dpage, n)) ||
					 size_add(doff, dlen) <= PAGE_SIZE))
		dst = kmap_local_page(dpage) + doff;
	else {
		if (dlen > SCOMP_SCRATCH_SIZE)
			dlen = SCOMP_SCRATCH_SIZE;
		dst = scratch->dst;
	}

	spin_lock_bh(&scratch->lock);

	if (src == scratch->src)
		memcpy_from_sglist(src, req->src, 0, slen);

	stream = raw_cpu_ptr(crypto_scomp_alg(scomp)->stream);
	spin_lock(&stream->lock);
	if (dir)
		ret = crypto_scomp_compress(scomp, src, slen,
					    dst, &dlen, stream->ctx);
	else
		ret = crypto_scomp_decompress(scomp, src, slen,
					      dst, &dlen, stream->ctx);

	if (dst == scratch->dst)
		memcpy_to_sglist(req->dst, 0, dst, dlen);

	spin_unlock(&stream->lock);
	spin_unlock_bh(&scratch->lock);

	req->dlen = dlen;

	if (dst != scratch->dst) {
		kunmap_local(dst);
		dlen += doff;
		for (;;) {
			flush_dcache_page(dpage);
			if (dlen <= PAGE_SIZE)
				break;
			dlen -= PAGE_SIZE;
			dpage = nth_page(dpage, 1);
		}
	}
	if (src != scratch->src)
		kunmap_local(src);

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
	scomp_free_streams(__crypto_scomp_alg(alg));
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
};

int crypto_register_scomp(struct scomp_alg *alg)
{
	struct crypto_alg *base = &alg->calg.base;

	comp_prepare_alg(&alg->calg);

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
