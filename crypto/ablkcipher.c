/*
 * Asynchronous block chaining cipher operations.
 *
 * This is the asynchronous version of blkcipher.c indicating completion
 * via a callback.
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/cryptouser.h>
#include <linux/compiler.h>
#include <net/netlink.h>

#include <crypto/scatterwalk.h>

#include "internal.h"

struct ablkcipher_buffer {
	struct list_head	entry;
	struct scatter_walk	dst;
	unsigned int		len;
	void			*data;
};

enum {
	ABLKCIPHER_WALK_SLOW = 1 << 0,
};

static inline void ablkcipher_buffer_write(struct ablkcipher_buffer *p)
{
	scatterwalk_copychunks(p->data, &p->dst, p->len, 1);
}

void __ablkcipher_walk_complete(struct ablkcipher_walk *walk)
{
	struct ablkcipher_buffer *p, *tmp;

	list_for_each_entry_safe(p, tmp, &walk->buffers, entry) {
		ablkcipher_buffer_write(p);
		list_del(&p->entry);
		kfree(p);
	}
}
EXPORT_SYMBOL_GPL(__ablkcipher_walk_complete);

static inline void ablkcipher_queue_write(struct ablkcipher_walk *walk,
					  struct ablkcipher_buffer *p)
{
	p->dst = walk->out;
	list_add_tail(&p->entry, &walk->buffers);
}

/* Get a spot of the specified length that does not straddle a page.
 * The caller needs to ensure that there is enough space for this operation.
 */
static inline u8 *ablkcipher_get_spot(u8 *start, unsigned int len)
{
	u8 *end_page = (u8 *)(((unsigned long)(start + len - 1)) & PAGE_MASK);

	return max(start, end_page);
}

static inline void ablkcipher_done_slow(struct ablkcipher_walk *walk,
					unsigned int n)
{
	for (;;) {
		unsigned int len_this_page = scatterwalk_pagelen(&walk->out);

		if (len_this_page > n)
			len_this_page = n;
		scatterwalk_advance(&walk->out, n);
		if (n == len_this_page)
			break;
		n -= len_this_page;
		scatterwalk_start(&walk->out, sg_next(walk->out.sg));
	}
}

static inline void ablkcipher_done_fast(struct ablkcipher_walk *walk,
					unsigned int n)
{
	scatterwalk_advance(&walk->in, n);
	scatterwalk_advance(&walk->out, n);
}

static int ablkcipher_walk_next(struct ablkcipher_request *req,
				struct ablkcipher_walk *walk);

int ablkcipher_walk_done(struct ablkcipher_request *req,
			 struct ablkcipher_walk *walk, int err)
{
	struct crypto_tfm *tfm = req->base.tfm;
	unsigned int n; /* bytes processed */
	bool more;

	if (unlikely(err < 0))
		goto finish;

	n = walk->nbytes - err;
	walk->total -= n;
	more = (walk->total != 0);

	if (likely(!(walk->flags & ABLKCIPHER_WALK_SLOW))) {
		ablkcipher_done_fast(walk, n);
	} else {
		if (WARN_ON(err)) {
			/* unexpected case; didn't process all bytes */
			err = -EINVAL;
			goto finish;
		}
		ablkcipher_done_slow(walk, n);
	}

	scatterwalk_done(&walk->in, 0, more);
	scatterwalk_done(&walk->out, 1, more);

	if (more) {
		crypto_yield(req->base.flags);
		return ablkcipher_walk_next(req, walk);
	}
	err = 0;
finish:
	walk->nbytes = 0;
	if (walk->iv != req->info)
		memcpy(req->info, walk->iv, tfm->crt_ablkcipher.ivsize);
	kfree(walk->iv_buffer);
	return err;
}
EXPORT_SYMBOL_GPL(ablkcipher_walk_done);

static inline int ablkcipher_next_slow(struct ablkcipher_request *req,
				       struct ablkcipher_walk *walk,
				       unsigned int bsize,
				       unsigned int alignmask,
				       void **src_p, void **dst_p)
{
	unsigned aligned_bsize = ALIGN(bsize, alignmask + 1);
	struct ablkcipher_buffer *p;
	void *src, *dst, *base;
	unsigned int n;

	n = ALIGN(sizeof(struct ablkcipher_buffer), alignmask + 1);
	n += (aligned_bsize * 3 - (alignmask + 1) +
	      (alignmask & ~(crypto_tfm_ctx_alignment() - 1)));

	p = kmalloc(n, GFP_ATOMIC);
	if (!p)
		return ablkcipher_walk_done(req, walk, -ENOMEM);

	base = p + 1;

	dst = (u8 *)ALIGN((unsigned long)base, alignmask + 1);
	src = dst = ablkcipher_get_spot(dst, bsize);

	p->len = bsize;
	p->data = dst;

	scatterwalk_copychunks(src, &walk->in, bsize, 0);

	ablkcipher_queue_write(walk, p);

	walk->nbytes = bsize;
	walk->flags |= ABLKCIPHER_WALK_SLOW;

	*src_p = src;
	*dst_p = dst;

	return 0;
}

static inline int ablkcipher_copy_iv(struct ablkcipher_walk *walk,
				     struct crypto_tfm *tfm,
				     unsigned int alignmask)
{
	unsigned bs = walk->blocksize;
	unsigned int ivsize = tfm->crt_ablkcipher.ivsize;
	unsigned aligned_bs = ALIGN(bs, alignmask + 1);
	unsigned int size = aligned_bs * 2 + ivsize + max(aligned_bs, ivsize) -
			    (alignmask + 1);
	u8 *iv;

	size += alignmask & ~(crypto_tfm_ctx_alignment() - 1);
	walk->iv_buffer = kmalloc(size, GFP_ATOMIC);
	if (!walk->iv_buffer)
		return -ENOMEM;

	iv = (u8 *)ALIGN((unsigned long)walk->iv_buffer, alignmask + 1);
	iv = ablkcipher_get_spot(iv, bs) + aligned_bs;
	iv = ablkcipher_get_spot(iv, bs) + aligned_bs;
	iv = ablkcipher_get_spot(iv, ivsize);

	walk->iv = memcpy(iv, walk->iv, ivsize);
	return 0;
}

static inline int ablkcipher_next_fast(struct ablkcipher_request *req,
				       struct ablkcipher_walk *walk)
{
	walk->src.page = scatterwalk_page(&walk->in);
	walk->src.offset = offset_in_page(walk->in.offset);
	walk->dst.page = scatterwalk_page(&walk->out);
	walk->dst.offset = offset_in_page(walk->out.offset);

	return 0;
}

static int ablkcipher_walk_next(struct ablkcipher_request *req,
				struct ablkcipher_walk *walk)
{
	struct crypto_tfm *tfm = req->base.tfm;
	unsigned int alignmask, bsize, n;
	void *src, *dst;
	int err;

	alignmask = crypto_tfm_alg_alignmask(tfm);
	n = walk->total;
	if (unlikely(n < crypto_tfm_alg_blocksize(tfm))) {
		req->base.flags |= CRYPTO_TFM_RES_BAD_BLOCK_LEN;
		return ablkcipher_walk_done(req, walk, -EINVAL);
	}

	walk->flags &= ~ABLKCIPHER_WALK_SLOW;
	src = dst = NULL;

	bsize = min(walk->blocksize, n);
	n = scatterwalk_clamp(&walk->in, n);
	n = scatterwalk_clamp(&walk->out, n);

	if (n < bsize ||
	    !scatterwalk_aligned(&walk->in, alignmask) ||
	    !scatterwalk_aligned(&walk->out, alignmask)) {
		err = ablkcipher_next_slow(req, walk, bsize, alignmask,
					   &src, &dst);
		goto set_phys_lowmem;
	}

	walk->nbytes = n;

	return ablkcipher_next_fast(req, walk);

set_phys_lowmem:
	if (err >= 0) {
		walk->src.page = virt_to_page(src);
		walk->dst.page = virt_to_page(dst);
		walk->src.offset = ((unsigned long)src & (PAGE_SIZE - 1));
		walk->dst.offset = ((unsigned long)dst & (PAGE_SIZE - 1));
	}

	return err;
}

static int ablkcipher_walk_first(struct ablkcipher_request *req,
				 struct ablkcipher_walk *walk)
{
	struct crypto_tfm *tfm = req->base.tfm;
	unsigned int alignmask;

	alignmask = crypto_tfm_alg_alignmask(tfm);
	if (WARN_ON_ONCE(in_irq()))
		return -EDEADLK;

	walk->iv = req->info;
	walk->nbytes = walk->total;
	if (unlikely(!walk->total))
		return 0;

	walk->iv_buffer = NULL;
	if (unlikely(((unsigned long)walk->iv & alignmask))) {
		int err = ablkcipher_copy_iv(walk, tfm, alignmask);

		if (err)
			return err;
	}

	scatterwalk_start(&walk->in, walk->in.sg);
	scatterwalk_start(&walk->out, walk->out.sg);

	return ablkcipher_walk_next(req, walk);
}

int ablkcipher_walk_phys(struct ablkcipher_request *req,
			 struct ablkcipher_walk *walk)
{
	walk->blocksize = crypto_tfm_alg_blocksize(req->base.tfm);
	return ablkcipher_walk_first(req, walk);
}
EXPORT_SYMBOL_GPL(ablkcipher_walk_phys);

static int setkey_unaligned(struct crypto_ablkcipher *tfm, const u8 *key,
			    unsigned int keylen)
{
	struct ablkcipher_alg *cipher = crypto_ablkcipher_alg(tfm);
	unsigned long alignmask = crypto_ablkcipher_alignmask(tfm);
	int ret;
	u8 *buffer, *alignbuffer;
	unsigned long absize;

	absize = keylen + alignmask;
	buffer = kmalloc(absize, GFP_ATOMIC);
	if (!buffer)
		return -ENOMEM;

	alignbuffer = (u8 *)ALIGN((unsigned long)buffer, alignmask + 1);
	memcpy(alignbuffer, key, keylen);
	ret = cipher->setkey(tfm, alignbuffer, keylen);
	memset(alignbuffer, 0, keylen);
	kfree(buffer);
	return ret;
}

static int setkey(struct crypto_ablkcipher *tfm, const u8 *key,
		  unsigned int keylen)
{
	struct ablkcipher_alg *cipher = crypto_ablkcipher_alg(tfm);
	unsigned long alignmask = crypto_ablkcipher_alignmask(tfm);

	if (keylen < cipher->min_keysize || keylen > cipher->max_keysize) {
		crypto_ablkcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	if ((unsigned long)key & alignmask)
		return setkey_unaligned(tfm, key, keylen);

	return cipher->setkey(tfm, key, keylen);
}

static unsigned int crypto_ablkcipher_ctxsize(struct crypto_alg *alg, u32 type,
					      u32 mask)
{
	return alg->cra_ctxsize;
}

static int crypto_init_ablkcipher_ops(struct crypto_tfm *tfm, u32 type,
				      u32 mask)
{
	struct ablkcipher_alg *alg = &tfm->__crt_alg->cra_ablkcipher;
	struct ablkcipher_tfm *crt = &tfm->crt_ablkcipher;

	if (alg->ivsize > PAGE_SIZE / 8)
		return -EINVAL;

	crt->setkey = setkey;
	crt->encrypt = alg->encrypt;
	crt->decrypt = alg->decrypt;
	crt->base = __crypto_ablkcipher_cast(tfm);
	crt->ivsize = alg->ivsize;

	return 0;
}

#ifdef CONFIG_NET
static int crypto_ablkcipher_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_blkcipher rblkcipher;

	strncpy(rblkcipher.type, "ablkcipher", sizeof(rblkcipher.type));
	strncpy(rblkcipher.geniv, alg->cra_ablkcipher.geniv ?: "<default>",
		sizeof(rblkcipher.geniv));
	rblkcipher.geniv[sizeof(rblkcipher.geniv) - 1] = '\0';

	rblkcipher.blocksize = alg->cra_blocksize;
	rblkcipher.min_keysize = alg->cra_ablkcipher.min_keysize;
	rblkcipher.max_keysize = alg->cra_ablkcipher.max_keysize;
	rblkcipher.ivsize = alg->cra_ablkcipher.ivsize;

	if (nla_put(skb, CRYPTOCFGA_REPORT_BLKCIPHER,
		    sizeof(struct crypto_report_blkcipher), &rblkcipher))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}
#else
static int crypto_ablkcipher_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif

static void crypto_ablkcipher_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;
static void crypto_ablkcipher_show(struct seq_file *m, struct crypto_alg *alg)
{
	struct ablkcipher_alg *ablkcipher = &alg->cra_ablkcipher;

	seq_printf(m, "type         : ablkcipher\n");
	seq_printf(m, "async        : %s\n", alg->cra_flags & CRYPTO_ALG_ASYNC ?
					     "yes" : "no");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "min keysize  : %u\n", ablkcipher->min_keysize);
	seq_printf(m, "max keysize  : %u\n", ablkcipher->max_keysize);
	seq_printf(m, "ivsize       : %u\n", ablkcipher->ivsize);
	seq_printf(m, "geniv        : %s\n", ablkcipher->geniv ?: "<default>");
}

const struct crypto_type crypto_ablkcipher_type = {
	.ctxsize = crypto_ablkcipher_ctxsize,
	.init = crypto_init_ablkcipher_ops,
#ifdef CONFIG_PROC_FS
	.show = crypto_ablkcipher_show,
#endif
	.report = crypto_ablkcipher_report,
};
EXPORT_SYMBOL_GPL(crypto_ablkcipher_type);

static int crypto_init_givcipher_ops(struct crypto_tfm *tfm, u32 type,
				      u32 mask)
{
	struct ablkcipher_alg *alg = &tfm->__crt_alg->cra_ablkcipher;
	struct ablkcipher_tfm *crt = &tfm->crt_ablkcipher;

	if (alg->ivsize > PAGE_SIZE / 8)
		return -EINVAL;

	crt->setkey = tfm->__crt_alg->cra_flags & CRYPTO_ALG_GENIV ?
		      alg->setkey : setkey;
	crt->encrypt = alg->encrypt;
	crt->decrypt = alg->decrypt;
	crt->base = __crypto_ablkcipher_cast(tfm);
	crt->ivsize = alg->ivsize;

	return 0;
}

#ifdef CONFIG_NET
static int crypto_givcipher_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_blkcipher rblkcipher;

	strncpy(rblkcipher.type, "givcipher", sizeof(rblkcipher.type));
	strncpy(rblkcipher.geniv, alg->cra_ablkcipher.geniv ?: "<built-in>",
		sizeof(rblkcipher.geniv));
	rblkcipher.geniv[sizeof(rblkcipher.geniv) - 1] = '\0';

	rblkcipher.blocksize = alg->cra_blocksize;
	rblkcipher.min_keysize = alg->cra_ablkcipher.min_keysize;
	rblkcipher.max_keysize = alg->cra_ablkcipher.max_keysize;
	rblkcipher.ivsize = alg->cra_ablkcipher.ivsize;

	if (nla_put(skb, CRYPTOCFGA_REPORT_BLKCIPHER,
		    sizeof(struct crypto_report_blkcipher), &rblkcipher))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}
#else
static int crypto_givcipher_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif

static void crypto_givcipher_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;
static void crypto_givcipher_show(struct seq_file *m, struct crypto_alg *alg)
{
	struct ablkcipher_alg *ablkcipher = &alg->cra_ablkcipher;

	seq_printf(m, "type         : givcipher\n");
	seq_printf(m, "async        : %s\n", alg->cra_flags & CRYPTO_ALG_ASYNC ?
					     "yes" : "no");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "min keysize  : %u\n", ablkcipher->min_keysize);
	seq_printf(m, "max keysize  : %u\n", ablkcipher->max_keysize);
	seq_printf(m, "ivsize       : %u\n", ablkcipher->ivsize);
	seq_printf(m, "geniv        : %s\n", ablkcipher->geniv ?: "<built-in>");
}

const struct crypto_type crypto_givcipher_type = {
	.ctxsize = crypto_ablkcipher_ctxsize,
	.init = crypto_init_givcipher_ops,
#ifdef CONFIG_PROC_FS
	.show = crypto_givcipher_show,
#endif
	.report = crypto_givcipher_report,
};
EXPORT_SYMBOL_GPL(crypto_givcipher_type);
