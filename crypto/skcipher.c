/*
 * Symmetric key cipher operations.
 *
 * Generic encrypt/decrypt wrapper for ciphers, handles operations across
 * multiple page boundaries by using temporary blocks.  In user context,
 * the kernel is given a chance to schedule us once per page.
 *
 * Copyright (c) 2015 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/bug.h>
#include <linux/cryptouser.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/seq_file.h>
#include <net/netlink.h>

#include "internal.h"

enum {
	SKCIPHER_WALK_PHYS = 1 << 0,
	SKCIPHER_WALK_SLOW = 1 << 1,
	SKCIPHER_WALK_COPY = 1 << 2,
	SKCIPHER_WALK_DIFF = 1 << 3,
	SKCIPHER_WALK_SLEEP = 1 << 4,
};

struct skcipher_walk_buffer {
	struct list_head entry;
	struct scatter_walk dst;
	unsigned int len;
	u8 *data;
	u8 buffer[];
};

static int skcipher_walk_next(struct skcipher_walk *walk);

static inline void skcipher_unmap(struct scatter_walk *walk, void *vaddr)
{
	if (PageHighMem(scatterwalk_page(walk)))
		kunmap_atomic(vaddr);
}

static inline void *skcipher_map(struct scatter_walk *walk)
{
	struct page *page = scatterwalk_page(walk);

	return (PageHighMem(page) ? kmap_atomic(page) : page_address(page)) +
	       offset_in_page(walk->offset);
}

static inline void skcipher_map_src(struct skcipher_walk *walk)
{
	walk->src.virt.addr = skcipher_map(&walk->in);
}

static inline void skcipher_map_dst(struct skcipher_walk *walk)
{
	walk->dst.virt.addr = skcipher_map(&walk->out);
}

static inline void skcipher_unmap_src(struct skcipher_walk *walk)
{
	skcipher_unmap(&walk->in, walk->src.virt.addr);
}

static inline void skcipher_unmap_dst(struct skcipher_walk *walk)
{
	skcipher_unmap(&walk->out, walk->dst.virt.addr);
}

static inline gfp_t skcipher_walk_gfp(struct skcipher_walk *walk)
{
	return walk->flags & SKCIPHER_WALK_SLEEP ? GFP_KERNEL : GFP_ATOMIC;
}

/* Get a spot of the specified length that does not straddle a page.
 * The caller needs to ensure that there is enough space for this operation.
 */
static inline u8 *skcipher_get_spot(u8 *start, unsigned int len)
{
	u8 *end_page = (u8 *)(((unsigned long)(start + len - 1)) & PAGE_MASK);

	return max(start, end_page);
}

static int skcipher_done_slow(struct skcipher_walk *walk, unsigned int bsize)
{
	u8 *addr;

	addr = (u8 *)ALIGN((unsigned long)walk->buffer, walk->alignmask + 1);
	addr = skcipher_get_spot(addr, bsize);
	scatterwalk_copychunks(addr, &walk->out, bsize,
			       (walk->flags & SKCIPHER_WALK_PHYS) ? 2 : 1);
	return 0;
}

int skcipher_walk_done(struct skcipher_walk *walk, int err)
{
	unsigned int n = walk->nbytes - err;
	unsigned int nbytes;

	nbytes = walk->total - n;

	if (unlikely(err < 0)) {
		nbytes = 0;
		n = 0;
	} else if (likely(!(walk->flags & (SKCIPHER_WALK_PHYS |
					   SKCIPHER_WALK_SLOW |
					   SKCIPHER_WALK_COPY |
					   SKCIPHER_WALK_DIFF)))) {
unmap_src:
		skcipher_unmap_src(walk);
	} else if (walk->flags & SKCIPHER_WALK_DIFF) {
		skcipher_unmap_dst(walk);
		goto unmap_src;
	} else if (walk->flags & SKCIPHER_WALK_COPY) {
		skcipher_map_dst(walk);
		memcpy(walk->dst.virt.addr, walk->page, n);
		skcipher_unmap_dst(walk);
	} else if (unlikely(walk->flags & SKCIPHER_WALK_SLOW)) {
		if (WARN_ON(err)) {
			err = -EINVAL;
			nbytes = 0;
		} else
			n = skcipher_done_slow(walk, n);
	}

	if (err > 0)
		err = 0;

	walk->total = nbytes;
	walk->nbytes = nbytes;

	scatterwalk_advance(&walk->in, n);
	scatterwalk_advance(&walk->out, n);
	scatterwalk_done(&walk->in, 0, nbytes);
	scatterwalk_done(&walk->out, 1, nbytes);

	if (nbytes) {
		crypto_yield(walk->flags & SKCIPHER_WALK_SLEEP ?
			     CRYPTO_TFM_REQ_MAY_SLEEP : 0);
		return skcipher_walk_next(walk);
	}

	/* Short-circuit for the common/fast path. */
	if (!((unsigned long)walk->buffer | (unsigned long)walk->page))
		goto out;

	if (walk->flags & SKCIPHER_WALK_PHYS)
		goto out;

	if (walk->iv != walk->oiv)
		memcpy(walk->oiv, walk->iv, walk->ivsize);
	if (walk->buffer != walk->page)
		kfree(walk->buffer);
	if (walk->page)
		free_page((unsigned long)walk->page);

out:
	return err;
}
EXPORT_SYMBOL_GPL(skcipher_walk_done);

void skcipher_walk_complete(struct skcipher_walk *walk, int err)
{
	struct skcipher_walk_buffer *p, *tmp;

	list_for_each_entry_safe(p, tmp, &walk->buffers, entry) {
		u8 *data;

		if (err)
			goto done;

		data = p->data;
		if (!data) {
			data = PTR_ALIGN(&p->buffer[0], walk->alignmask + 1);
			data = skcipher_get_spot(data, walk->stride);
		}

		scatterwalk_copychunks(data, &p->dst, p->len, 1);

		if (offset_in_page(p->data) + p->len + walk->stride >
		    PAGE_SIZE)
			free_page((unsigned long)p->data);

done:
		list_del(&p->entry);
		kfree(p);
	}

	if (!err && walk->iv != walk->oiv)
		memcpy(walk->oiv, walk->iv, walk->ivsize);
	if (walk->buffer != walk->page)
		kfree(walk->buffer);
	if (walk->page)
		free_page((unsigned long)walk->page);
}
EXPORT_SYMBOL_GPL(skcipher_walk_complete);

static void skcipher_queue_write(struct skcipher_walk *walk,
				 struct skcipher_walk_buffer *p)
{
	p->dst = walk->out;
	list_add_tail(&p->entry, &walk->buffers);
}

static int skcipher_next_slow(struct skcipher_walk *walk, unsigned int bsize)
{
	bool phys = walk->flags & SKCIPHER_WALK_PHYS;
	unsigned alignmask = walk->alignmask;
	struct skcipher_walk_buffer *p;
	unsigned a;
	unsigned n;
	u8 *buffer;
	void *v;

	if (!phys) {
		if (!walk->buffer)
			walk->buffer = walk->page;
		buffer = walk->buffer;
		if (buffer)
			goto ok;
	}

	/* Start with the minimum alignment of kmalloc. */
	a = crypto_tfm_ctx_alignment() - 1;
	n = bsize;

	if (phys) {
		/* Calculate the minimum alignment of p->buffer. */
		a &= (sizeof(*p) ^ (sizeof(*p) - 1)) >> 1;
		n += sizeof(*p);
	}

	/* Minimum size to align p->buffer by alignmask. */
	n += alignmask & ~a;

	/* Minimum size to ensure p->buffer does not straddle a page. */
	n += (bsize - 1) & ~(alignmask | a);

	v = kzalloc(n, skcipher_walk_gfp(walk));
	if (!v)
		return skcipher_walk_done(walk, -ENOMEM);

	if (phys) {
		p = v;
		p->len = bsize;
		skcipher_queue_write(walk, p);
		buffer = p->buffer;
	} else {
		walk->buffer = v;
		buffer = v;
	}

ok:
	walk->dst.virt.addr = PTR_ALIGN(buffer, alignmask + 1);
	walk->dst.virt.addr = skcipher_get_spot(walk->dst.virt.addr, bsize);
	walk->src.virt.addr = walk->dst.virt.addr;

	scatterwalk_copychunks(walk->src.virt.addr, &walk->in, bsize, 0);

	walk->nbytes = bsize;
	walk->flags |= SKCIPHER_WALK_SLOW;

	return 0;
}

static int skcipher_next_copy(struct skcipher_walk *walk)
{
	struct skcipher_walk_buffer *p;
	u8 *tmp = walk->page;

	skcipher_map_src(walk);
	memcpy(tmp, walk->src.virt.addr, walk->nbytes);
	skcipher_unmap_src(walk);

	walk->src.virt.addr = tmp;
	walk->dst.virt.addr = tmp;

	if (!(walk->flags & SKCIPHER_WALK_PHYS))
		return 0;

	p = kmalloc(sizeof(*p), skcipher_walk_gfp(walk));
	if (!p)
		return -ENOMEM;

	p->data = walk->page;
	p->len = walk->nbytes;
	skcipher_queue_write(walk, p);

	if (offset_in_page(walk->page) + walk->nbytes + walk->stride >
	    PAGE_SIZE)
		walk->page = NULL;
	else
		walk->page += walk->nbytes;

	return 0;
}

static int skcipher_next_fast(struct skcipher_walk *walk)
{
	unsigned long diff;

	walk->src.phys.page = scatterwalk_page(&walk->in);
	walk->src.phys.offset = offset_in_page(walk->in.offset);
	walk->dst.phys.page = scatterwalk_page(&walk->out);
	walk->dst.phys.offset = offset_in_page(walk->out.offset);

	if (walk->flags & SKCIPHER_WALK_PHYS)
		return 0;

	diff = walk->src.phys.offset - walk->dst.phys.offset;
	diff |= walk->src.virt.page - walk->dst.virt.page;

	skcipher_map_src(walk);
	walk->dst.virt.addr = walk->src.virt.addr;

	if (diff) {
		walk->flags |= SKCIPHER_WALK_DIFF;
		skcipher_map_dst(walk);
	}

	return 0;
}

static int skcipher_walk_next(struct skcipher_walk *walk)
{
	unsigned int bsize;
	unsigned int n;
	int err;

	walk->flags &= ~(SKCIPHER_WALK_SLOW | SKCIPHER_WALK_COPY |
			 SKCIPHER_WALK_DIFF);

	n = walk->total;
	bsize = min(walk->stride, max(n, walk->blocksize));
	n = scatterwalk_clamp(&walk->in, n);
	n = scatterwalk_clamp(&walk->out, n);

	if (unlikely(n < bsize)) {
		if (unlikely(walk->total < walk->blocksize))
			return skcipher_walk_done(walk, -EINVAL);

slow_path:
		err = skcipher_next_slow(walk, bsize);
		goto set_phys_lowmem;
	}

	if (unlikely((walk->in.offset | walk->out.offset) & walk->alignmask)) {
		if (!walk->page) {
			gfp_t gfp = skcipher_walk_gfp(walk);

			walk->page = (void *)__get_free_page(gfp);
			if (!walk->page)
				goto slow_path;
		}

		walk->nbytes = min_t(unsigned, n,
				     PAGE_SIZE - offset_in_page(walk->page));
		walk->flags |= SKCIPHER_WALK_COPY;
		err = skcipher_next_copy(walk);
		goto set_phys_lowmem;
	}

	walk->nbytes = n;

	return skcipher_next_fast(walk);

set_phys_lowmem:
	if (!err && (walk->flags & SKCIPHER_WALK_PHYS)) {
		walk->src.phys.page = virt_to_page(walk->src.virt.addr);
		walk->dst.phys.page = virt_to_page(walk->dst.virt.addr);
		walk->src.phys.offset &= PAGE_SIZE - 1;
		walk->dst.phys.offset &= PAGE_SIZE - 1;
	}
	return err;
}
EXPORT_SYMBOL_GPL(skcipher_walk_next);

static int skcipher_copy_iv(struct skcipher_walk *walk)
{
	unsigned a = crypto_tfm_ctx_alignment() - 1;
	unsigned alignmask = walk->alignmask;
	unsigned ivsize = walk->ivsize;
	unsigned bs = walk->stride;
	unsigned aligned_bs;
	unsigned size;
	u8 *iv;

	aligned_bs = ALIGN(bs, alignmask);

	/* Minimum size to align buffer by alignmask. */
	size = alignmask & ~a;

	if (walk->flags & SKCIPHER_WALK_PHYS)
		size += ivsize;
	else {
		size += aligned_bs + ivsize;

		/* Minimum size to ensure buffer does not straddle a page. */
		size += (bs - 1) & ~(alignmask | a);
	}

	walk->buffer = kmalloc(size, skcipher_walk_gfp(walk));
	if (!walk->buffer)
		return -ENOMEM;

	iv = PTR_ALIGN(walk->buffer, alignmask + 1);
	iv = skcipher_get_spot(iv, bs) + aligned_bs;

	walk->iv = memcpy(iv, walk->iv, walk->ivsize);
	return 0;
}

static int skcipher_walk_first(struct skcipher_walk *walk)
{
	if (WARN_ON_ONCE(in_irq()))
		return -EDEADLK;

	walk->buffer = NULL;
	if (unlikely(((unsigned long)walk->iv & walk->alignmask))) {
		int err = skcipher_copy_iv(walk);
		if (err)
			return err;
	}

	walk->page = NULL;
	walk->nbytes = walk->total;

	return skcipher_walk_next(walk);
}

static int skcipher_walk_skcipher(struct skcipher_walk *walk,
				  struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);

	walk->total = req->cryptlen;
	walk->nbytes = 0;

	if (unlikely(!walk->total))
		return 0;

	scatterwalk_start(&walk->in, req->src);
	scatterwalk_start(&walk->out, req->dst);

	walk->iv = req->iv;
	walk->oiv = req->iv;

	walk->flags &= ~SKCIPHER_WALK_SLEEP;
	walk->flags |= req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ?
		       SKCIPHER_WALK_SLEEP : 0;

	walk->blocksize = crypto_skcipher_blocksize(tfm);
	walk->stride = crypto_skcipher_walksize(tfm);
	walk->ivsize = crypto_skcipher_ivsize(tfm);
	walk->alignmask = crypto_skcipher_alignmask(tfm);

	return skcipher_walk_first(walk);
}

int skcipher_walk_virt(struct skcipher_walk *walk,
		       struct skcipher_request *req, bool atomic)
{
	int err;

	walk->flags &= ~SKCIPHER_WALK_PHYS;

	err = skcipher_walk_skcipher(walk, req);

	walk->flags &= atomic ? ~SKCIPHER_WALK_SLEEP : ~0;

	return err;
}
EXPORT_SYMBOL_GPL(skcipher_walk_virt);

void skcipher_walk_atomise(struct skcipher_walk *walk)
{
	walk->flags &= ~SKCIPHER_WALK_SLEEP;
}
EXPORT_SYMBOL_GPL(skcipher_walk_atomise);

int skcipher_walk_async(struct skcipher_walk *walk,
			struct skcipher_request *req)
{
	walk->flags |= SKCIPHER_WALK_PHYS;

	INIT_LIST_HEAD(&walk->buffers);

	return skcipher_walk_skcipher(walk, req);
}
EXPORT_SYMBOL_GPL(skcipher_walk_async);

static int skcipher_walk_aead_common(struct skcipher_walk *walk,
				     struct aead_request *req, bool atomic)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	int err;

	walk->nbytes = 0;

	if (unlikely(!walk->total))
		return 0;

	walk->flags &= ~SKCIPHER_WALK_PHYS;

	scatterwalk_start(&walk->in, req->src);
	scatterwalk_start(&walk->out, req->dst);

	scatterwalk_copychunks(NULL, &walk->in, req->assoclen, 2);
	scatterwalk_copychunks(NULL, &walk->out, req->assoclen, 2);

	scatterwalk_done(&walk->in, 0, walk->total);
	scatterwalk_done(&walk->out, 0, walk->total);

	walk->iv = req->iv;
	walk->oiv = req->iv;

	if (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP)
		walk->flags |= SKCIPHER_WALK_SLEEP;
	else
		walk->flags &= ~SKCIPHER_WALK_SLEEP;

	walk->blocksize = crypto_aead_blocksize(tfm);
	walk->stride = crypto_aead_chunksize(tfm);
	walk->ivsize = crypto_aead_ivsize(tfm);
	walk->alignmask = crypto_aead_alignmask(tfm);

	err = skcipher_walk_first(walk);

	if (atomic)
		walk->flags &= ~SKCIPHER_WALK_SLEEP;

	return err;
}

int skcipher_walk_aead(struct skcipher_walk *walk, struct aead_request *req,
		       bool atomic)
{
	walk->total = req->cryptlen;

	return skcipher_walk_aead_common(walk, req, atomic);
}
EXPORT_SYMBOL_GPL(skcipher_walk_aead);

int skcipher_walk_aead_encrypt(struct skcipher_walk *walk,
			       struct aead_request *req, bool atomic)
{
	walk->total = req->cryptlen;

	return skcipher_walk_aead_common(walk, req, atomic);
}
EXPORT_SYMBOL_GPL(skcipher_walk_aead_encrypt);

int skcipher_walk_aead_decrypt(struct skcipher_walk *walk,
			       struct aead_request *req, bool atomic)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);

	walk->total = req->cryptlen - crypto_aead_authsize(tfm);

	return skcipher_walk_aead_common(walk, req, atomic);
}
EXPORT_SYMBOL_GPL(skcipher_walk_aead_decrypt);

static unsigned int crypto_skcipher_extsize(struct crypto_alg *alg)
{
	if (alg->cra_type == &crypto_blkcipher_type)
		return sizeof(struct crypto_blkcipher *);

	if (alg->cra_type == &crypto_ablkcipher_type ||
	    alg->cra_type == &crypto_givcipher_type)
		return sizeof(struct crypto_ablkcipher *);

	return crypto_alg_extsize(alg);
}

static int skcipher_setkey_blkcipher(struct crypto_skcipher *tfm,
				     const u8 *key, unsigned int keylen)
{
	struct crypto_blkcipher **ctx = crypto_skcipher_ctx(tfm);
	struct crypto_blkcipher *blkcipher = *ctx;
	int err;

	crypto_blkcipher_clear_flags(blkcipher, ~0);
	crypto_blkcipher_set_flags(blkcipher, crypto_skcipher_get_flags(tfm) &
					      CRYPTO_TFM_REQ_MASK);
	err = crypto_blkcipher_setkey(blkcipher, key, keylen);
	crypto_skcipher_set_flags(tfm, crypto_blkcipher_get_flags(blkcipher) &
				       CRYPTO_TFM_RES_MASK);

	return err;
}

static int skcipher_crypt_blkcipher(struct skcipher_request *req,
				    int (*crypt)(struct blkcipher_desc *,
						 struct scatterlist *,
						 struct scatterlist *,
						 unsigned int))
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_blkcipher **ctx = crypto_skcipher_ctx(tfm);
	struct blkcipher_desc desc = {
		.tfm = *ctx,
		.info = req->iv,
		.flags = req->base.flags,
	};


	return crypt(&desc, req->dst, req->src, req->cryptlen);
}

static int skcipher_encrypt_blkcipher(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(skcipher);
	struct blkcipher_alg *alg = &tfm->__crt_alg->cra_blkcipher;

	return skcipher_crypt_blkcipher(req, alg->encrypt);
}

static int skcipher_decrypt_blkcipher(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(skcipher);
	struct blkcipher_alg *alg = &tfm->__crt_alg->cra_blkcipher;

	return skcipher_crypt_blkcipher(req, alg->decrypt);
}

static void crypto_exit_skcipher_ops_blkcipher(struct crypto_tfm *tfm)
{
	struct crypto_blkcipher **ctx = crypto_tfm_ctx(tfm);

	crypto_free_blkcipher(*ctx);
}

static int crypto_init_skcipher_ops_blkcipher(struct crypto_tfm *tfm)
{
	struct crypto_alg *calg = tfm->__crt_alg;
	struct crypto_skcipher *skcipher = __crypto_skcipher_cast(tfm);
	struct crypto_blkcipher **ctx = crypto_tfm_ctx(tfm);
	struct crypto_blkcipher *blkcipher;
	struct crypto_tfm *btfm;

	if (!crypto_mod_get(calg))
		return -EAGAIN;

	btfm = __crypto_alloc_tfm(calg, CRYPTO_ALG_TYPE_BLKCIPHER,
					CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(btfm)) {
		crypto_mod_put(calg);
		return PTR_ERR(btfm);
	}

	blkcipher = __crypto_blkcipher_cast(btfm);
	*ctx = blkcipher;
	tfm->exit = crypto_exit_skcipher_ops_blkcipher;

	skcipher->setkey = skcipher_setkey_blkcipher;
	skcipher->encrypt = skcipher_encrypt_blkcipher;
	skcipher->decrypt = skcipher_decrypt_blkcipher;

	skcipher->ivsize = crypto_blkcipher_ivsize(blkcipher);
	skcipher->keysize = calg->cra_blkcipher.max_keysize;

	return 0;
}

static int skcipher_setkey_ablkcipher(struct crypto_skcipher *tfm,
				      const u8 *key, unsigned int keylen)
{
	struct crypto_ablkcipher **ctx = crypto_skcipher_ctx(tfm);
	struct crypto_ablkcipher *ablkcipher = *ctx;
	int err;

	crypto_ablkcipher_clear_flags(ablkcipher, ~0);
	crypto_ablkcipher_set_flags(ablkcipher,
				    crypto_skcipher_get_flags(tfm) &
				    CRYPTO_TFM_REQ_MASK);
	err = crypto_ablkcipher_setkey(ablkcipher, key, keylen);
	crypto_skcipher_set_flags(tfm,
				  crypto_ablkcipher_get_flags(ablkcipher) &
				  CRYPTO_TFM_RES_MASK);

	return err;
}

static int skcipher_crypt_ablkcipher(struct skcipher_request *req,
				     int (*crypt)(struct ablkcipher_request *))
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_ablkcipher **ctx = crypto_skcipher_ctx(tfm);
	struct ablkcipher_request *subreq = skcipher_request_ctx(req);

	ablkcipher_request_set_tfm(subreq, *ctx);
	ablkcipher_request_set_callback(subreq, skcipher_request_flags(req),
					req->base.complete, req->base.data);
	ablkcipher_request_set_crypt(subreq, req->src, req->dst, req->cryptlen,
				     req->iv);

	return crypt(subreq);
}

static int skcipher_encrypt_ablkcipher(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(skcipher);
	struct ablkcipher_alg *alg = &tfm->__crt_alg->cra_ablkcipher;

	return skcipher_crypt_ablkcipher(req, alg->encrypt);
}

static int skcipher_decrypt_ablkcipher(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(skcipher);
	struct ablkcipher_alg *alg = &tfm->__crt_alg->cra_ablkcipher;

	return skcipher_crypt_ablkcipher(req, alg->decrypt);
}

static void crypto_exit_skcipher_ops_ablkcipher(struct crypto_tfm *tfm)
{
	struct crypto_ablkcipher **ctx = crypto_tfm_ctx(tfm);

	crypto_free_ablkcipher(*ctx);
}

static int crypto_init_skcipher_ops_ablkcipher(struct crypto_tfm *tfm)
{
	struct crypto_alg *calg = tfm->__crt_alg;
	struct crypto_skcipher *skcipher = __crypto_skcipher_cast(tfm);
	struct crypto_ablkcipher **ctx = crypto_tfm_ctx(tfm);
	struct crypto_ablkcipher *ablkcipher;
	struct crypto_tfm *abtfm;

	if (!crypto_mod_get(calg))
		return -EAGAIN;

	abtfm = __crypto_alloc_tfm(calg, 0, 0);
	if (IS_ERR(abtfm)) {
		crypto_mod_put(calg);
		return PTR_ERR(abtfm);
	}

	ablkcipher = __crypto_ablkcipher_cast(abtfm);
	*ctx = ablkcipher;
	tfm->exit = crypto_exit_skcipher_ops_ablkcipher;

	skcipher->setkey = skcipher_setkey_ablkcipher;
	skcipher->encrypt = skcipher_encrypt_ablkcipher;
	skcipher->decrypt = skcipher_decrypt_ablkcipher;

	skcipher->ivsize = crypto_ablkcipher_ivsize(ablkcipher);
	skcipher->reqsize = crypto_ablkcipher_reqsize(ablkcipher) +
			    sizeof(struct ablkcipher_request);
	skcipher->keysize = calg->cra_ablkcipher.max_keysize;

	return 0;
}

static int skcipher_setkey_unaligned(struct crypto_skcipher *tfm,
				     const u8 *key, unsigned int keylen)
{
	unsigned long alignmask = crypto_skcipher_alignmask(tfm);
	struct skcipher_alg *cipher = crypto_skcipher_alg(tfm);
	u8 *buffer, *alignbuffer;
	unsigned long absize;
	int ret;

	absize = keylen + alignmask;
	buffer = kmalloc(absize, GFP_ATOMIC);
	if (!buffer)
		return -ENOMEM;

	alignbuffer = (u8 *)ALIGN((unsigned long)buffer, alignmask + 1);
	memcpy(alignbuffer, key, keylen);
	ret = cipher->setkey(tfm, alignbuffer, keylen);
	kzfree(buffer);
	return ret;
}

static int skcipher_setkey(struct crypto_skcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	struct skcipher_alg *cipher = crypto_skcipher_alg(tfm);
	unsigned long alignmask = crypto_skcipher_alignmask(tfm);

	if (keylen < cipher->min_keysize || keylen > cipher->max_keysize) {
		crypto_skcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	if ((unsigned long)key & alignmask)
		return skcipher_setkey_unaligned(tfm, key, keylen);

	return cipher->setkey(tfm, key, keylen);
}

static void crypto_skcipher_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_skcipher *skcipher = __crypto_skcipher_cast(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(skcipher);

	alg->exit(skcipher);
}

static int crypto_skcipher_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_skcipher *skcipher = __crypto_skcipher_cast(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(skcipher);

	if (tfm->__crt_alg->cra_type == &crypto_blkcipher_type)
		return crypto_init_skcipher_ops_blkcipher(tfm);

	if (tfm->__crt_alg->cra_type == &crypto_ablkcipher_type ||
	    tfm->__crt_alg->cra_type == &crypto_givcipher_type)
		return crypto_init_skcipher_ops_ablkcipher(tfm);

	skcipher->setkey = skcipher_setkey;
	skcipher->encrypt = alg->encrypt;
	skcipher->decrypt = alg->decrypt;
	skcipher->ivsize = alg->ivsize;
	skcipher->keysize = alg->max_keysize;

	if (alg->exit)
		skcipher->base.exit = crypto_skcipher_exit_tfm;

	if (alg->init)
		return alg->init(skcipher);

	return 0;
}

static void crypto_skcipher_free_instance(struct crypto_instance *inst)
{
	struct skcipher_instance *skcipher =
		container_of(inst, struct skcipher_instance, s.base);

	skcipher->free(skcipher);
}

static void crypto_skcipher_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;
static void crypto_skcipher_show(struct seq_file *m, struct crypto_alg *alg)
{
	struct skcipher_alg *skcipher = container_of(alg, struct skcipher_alg,
						     base);

	seq_printf(m, "type         : skcipher\n");
	seq_printf(m, "async        : %s\n",
		   alg->cra_flags & CRYPTO_ALG_ASYNC ?  "yes" : "no");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "min keysize  : %u\n", skcipher->min_keysize);
	seq_printf(m, "max keysize  : %u\n", skcipher->max_keysize);
	seq_printf(m, "ivsize       : %u\n", skcipher->ivsize);
	seq_printf(m, "chunksize    : %u\n", skcipher->chunksize);
	seq_printf(m, "walksize     : %u\n", skcipher->walksize);
}

#ifdef CONFIG_NET
static int crypto_skcipher_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_blkcipher rblkcipher;
	struct skcipher_alg *skcipher = container_of(alg, struct skcipher_alg,
						     base);

	strncpy(rblkcipher.type, "skcipher", sizeof(rblkcipher.type));
	strncpy(rblkcipher.geniv, "<none>", sizeof(rblkcipher.geniv));

	rblkcipher.blocksize = alg->cra_blocksize;
	rblkcipher.min_keysize = skcipher->min_keysize;
	rblkcipher.max_keysize = skcipher->max_keysize;
	rblkcipher.ivsize = skcipher->ivsize;

	if (nla_put(skb, CRYPTOCFGA_REPORT_BLKCIPHER,
		    sizeof(struct crypto_report_blkcipher), &rblkcipher))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}
#else
static int crypto_skcipher_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif

static const struct crypto_type crypto_skcipher_type2 = {
	.extsize = crypto_skcipher_extsize,
	.init_tfm = crypto_skcipher_init_tfm,
	.free = crypto_skcipher_free_instance,
#ifdef CONFIG_PROC_FS
	.show = crypto_skcipher_show,
#endif
	.report = crypto_skcipher_report,
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_BLKCIPHER_MASK,
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.tfmsize = offsetof(struct crypto_skcipher, base),
};

int crypto_grab_skcipher(struct crypto_skcipher_spawn *spawn,
			  const char *name, u32 type, u32 mask)
{
	spawn->base.frontend = &crypto_skcipher_type2;
	return crypto_grab_spawn(&spawn->base, name, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_grab_skcipher);

struct crypto_skcipher *crypto_alloc_skcipher(const char *alg_name,
					      u32 type, u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_skcipher_type2, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_skcipher);

int crypto_has_skcipher2(const char *alg_name, u32 type, u32 mask)
{
	return crypto_type_has_alg(alg_name, &crypto_skcipher_type2,
				   type, mask);
}
EXPORT_SYMBOL_GPL(crypto_has_skcipher2);

static int skcipher_prepare_alg(struct skcipher_alg *alg)
{
	struct crypto_alg *base = &alg->base;

	if (alg->ivsize > PAGE_SIZE / 8 || alg->chunksize > PAGE_SIZE / 8 ||
	    alg->walksize > PAGE_SIZE / 8)
		return -EINVAL;

	if (!alg->chunksize)
		alg->chunksize = base->cra_blocksize;
	if (!alg->walksize)
		alg->walksize = alg->chunksize;

	base->cra_type = &crypto_skcipher_type2;
	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
	base->cra_flags |= CRYPTO_ALG_TYPE_SKCIPHER;

	return 0;
}

int crypto_register_skcipher(struct skcipher_alg *alg)
{
	struct crypto_alg *base = &alg->base;
	int err;

	err = skcipher_prepare_alg(alg);
	if (err)
		return err;

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_skcipher);

void crypto_unregister_skcipher(struct skcipher_alg *alg)
{
	crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_skcipher);

int crypto_register_skciphers(struct skcipher_alg *algs, int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = crypto_register_skcipher(&algs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (--i; i >= 0; --i)
		crypto_unregister_skcipher(&algs[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_register_skciphers);

void crypto_unregister_skciphers(struct skcipher_alg *algs, int count)
{
	int i;

	for (i = count - 1; i >= 0; --i)
		crypto_unregister_skcipher(&algs[i]);
}
EXPORT_SYMBOL_GPL(crypto_unregister_skciphers);

int skcipher_register_instance(struct crypto_template *tmpl,
			   struct skcipher_instance *inst)
{
	int err;

	err = skcipher_prepare_alg(&inst->alg);
	if (err)
		return err;

	return crypto_register_instance(tmpl, skcipher_crypto_instance(inst));
}
EXPORT_SYMBOL_GPL(skcipher_register_instance);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Symmetric key cipher type");
