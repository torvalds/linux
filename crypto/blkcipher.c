// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Block chaining cipher operations.
 *
 * Generic encrypt/decrypt wrapper for ciphers, handles operations across
 * multiple page boundaries by using temporary blocks.  In user context,
 * the kernel is given a chance to schedule us once per page.
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/cryptouser.h>
#include <linux/compiler.h>
#include <net/netlink.h>

#include "internal.h"

enum {
	BLKCIPHER_WALK_PHYS = 1 << 0,
	BLKCIPHER_WALK_SLOW = 1 << 1,
	BLKCIPHER_WALK_COPY = 1 << 2,
	BLKCIPHER_WALK_DIFF = 1 << 3,
};

static int blkcipher_walk_next(struct blkcipher_desc *desc,
			       struct blkcipher_walk *walk);
static int blkcipher_walk_first(struct blkcipher_desc *desc,
				struct blkcipher_walk *walk);

static inline void blkcipher_map_src(struct blkcipher_walk *walk)
{
	walk->src.virt.addr = scatterwalk_map(&walk->in);
}

static inline void blkcipher_map_dst(struct blkcipher_walk *walk)
{
	walk->dst.virt.addr = scatterwalk_map(&walk->out);
}

static inline void blkcipher_unmap_src(struct blkcipher_walk *walk)
{
	scatterwalk_unmap(walk->src.virt.addr);
}

static inline void blkcipher_unmap_dst(struct blkcipher_walk *walk)
{
	scatterwalk_unmap(walk->dst.virt.addr);
}

/* Get a spot of the specified length that does not straddle a page.
 * The caller needs to ensure that there is enough space for this operation.
 */
static inline u8 *blkcipher_get_spot(u8 *start, unsigned int len)
{
	u8 *end_page = (u8 *)(((unsigned long)(start + len - 1)) & PAGE_MASK);
	return max(start, end_page);
}

static inline void blkcipher_done_slow(struct blkcipher_walk *walk,
				       unsigned int bsize)
{
	u8 *addr;

	addr = (u8 *)ALIGN((unsigned long)walk->buffer, walk->alignmask + 1);
	addr = blkcipher_get_spot(addr, bsize);
	scatterwalk_copychunks(addr, &walk->out, bsize, 1);
}

static inline void blkcipher_done_fast(struct blkcipher_walk *walk,
				       unsigned int n)
{
	if (walk->flags & BLKCIPHER_WALK_COPY) {
		blkcipher_map_dst(walk);
		memcpy(walk->dst.virt.addr, walk->page, n);
		blkcipher_unmap_dst(walk);
	} else if (!(walk->flags & BLKCIPHER_WALK_PHYS)) {
		if (walk->flags & BLKCIPHER_WALK_DIFF)
			blkcipher_unmap_dst(walk);
		blkcipher_unmap_src(walk);
	}

	scatterwalk_advance(&walk->in, n);
	scatterwalk_advance(&walk->out, n);
}

int blkcipher_walk_done(struct blkcipher_desc *desc,
			struct blkcipher_walk *walk, int err)
{
	unsigned int n; /* bytes processed */
	bool more;

	if (unlikely(err < 0))
		goto finish;

	n = walk->nbytes - err;
	walk->total -= n;
	more = (walk->total != 0);

	if (likely(!(walk->flags & BLKCIPHER_WALK_SLOW))) {
		blkcipher_done_fast(walk, n);
	} else {
		if (WARN_ON(err)) {
			/* unexpected case; didn't process all bytes */
			err = -EINVAL;
			goto finish;
		}
		blkcipher_done_slow(walk, n);
	}

	scatterwalk_done(&walk->in, 0, more);
	scatterwalk_done(&walk->out, 1, more);

	if (more) {
		crypto_yield(desc->flags);
		return blkcipher_walk_next(desc, walk);
	}
	err = 0;
finish:
	walk->nbytes = 0;
	if (walk->iv != desc->info)
		memcpy(desc->info, walk->iv, walk->ivsize);
	if (walk->buffer != walk->page)
		kfree(walk->buffer);
	if (walk->page)
		free_page((unsigned long)walk->page);
	return err;
}
EXPORT_SYMBOL_GPL(blkcipher_walk_done);

static inline int blkcipher_next_slow(struct blkcipher_desc *desc,
				      struct blkcipher_walk *walk,
				      unsigned int bsize,
				      unsigned int alignmask)
{
	unsigned int n;
	unsigned aligned_bsize = ALIGN(bsize, alignmask + 1);

	if (walk->buffer)
		goto ok;

	walk->buffer = walk->page;
	if (walk->buffer)
		goto ok;

	n = aligned_bsize * 3 - (alignmask + 1) +
	    (alignmask & ~(crypto_tfm_ctx_alignment() - 1));
	walk->buffer = kmalloc(n, GFP_ATOMIC);
	if (!walk->buffer)
		return blkcipher_walk_done(desc, walk, -ENOMEM);

ok:
	walk->dst.virt.addr = (u8 *)ALIGN((unsigned long)walk->buffer,
					  alignmask + 1);
	walk->dst.virt.addr = blkcipher_get_spot(walk->dst.virt.addr, bsize);
	walk->src.virt.addr = blkcipher_get_spot(walk->dst.virt.addr +
						 aligned_bsize, bsize);

	scatterwalk_copychunks(walk->src.virt.addr, &walk->in, bsize, 0);

	walk->nbytes = bsize;
	walk->flags |= BLKCIPHER_WALK_SLOW;

	return 0;
}

static inline int blkcipher_next_copy(struct blkcipher_walk *walk)
{
	u8 *tmp = walk->page;

	blkcipher_map_src(walk);
	memcpy(tmp, walk->src.virt.addr, walk->nbytes);
	blkcipher_unmap_src(walk);

	walk->src.virt.addr = tmp;
	walk->dst.virt.addr = tmp;

	return 0;
}

static inline int blkcipher_next_fast(struct blkcipher_desc *desc,
				      struct blkcipher_walk *walk)
{
	unsigned long diff;

	walk->src.phys.page = scatterwalk_page(&walk->in);
	walk->src.phys.offset = offset_in_page(walk->in.offset);
	walk->dst.phys.page = scatterwalk_page(&walk->out);
	walk->dst.phys.offset = offset_in_page(walk->out.offset);

	if (walk->flags & BLKCIPHER_WALK_PHYS)
		return 0;

	diff = walk->src.phys.offset - walk->dst.phys.offset;
	diff |= walk->src.virt.page - walk->dst.virt.page;

	blkcipher_map_src(walk);
	walk->dst.virt.addr = walk->src.virt.addr;

	if (diff) {
		walk->flags |= BLKCIPHER_WALK_DIFF;
		blkcipher_map_dst(walk);
	}

	return 0;
}

static int blkcipher_walk_next(struct blkcipher_desc *desc,
			       struct blkcipher_walk *walk)
{
	unsigned int bsize;
	unsigned int n;
	int err;

	n = walk->total;
	if (unlikely(n < walk->cipher_blocksize)) {
		desc->flags |= CRYPTO_TFM_RES_BAD_BLOCK_LEN;
		return blkcipher_walk_done(desc, walk, -EINVAL);
	}

	bsize = min(walk->walk_blocksize, n);

	walk->flags &= ~(BLKCIPHER_WALK_SLOW | BLKCIPHER_WALK_COPY |
			 BLKCIPHER_WALK_DIFF);
	if (!scatterwalk_aligned(&walk->in, walk->alignmask) ||
	    !scatterwalk_aligned(&walk->out, walk->alignmask)) {
		walk->flags |= BLKCIPHER_WALK_COPY;
		if (!walk->page) {
			walk->page = (void *)__get_free_page(GFP_ATOMIC);
			if (!walk->page)
				n = 0;
		}
	}

	n = scatterwalk_clamp(&walk->in, n);
	n = scatterwalk_clamp(&walk->out, n);

	if (unlikely(n < bsize)) {
		err = blkcipher_next_slow(desc, walk, bsize, walk->alignmask);
		goto set_phys_lowmem;
	}

	walk->nbytes = n;
	if (walk->flags & BLKCIPHER_WALK_COPY) {
		err = blkcipher_next_copy(walk);
		goto set_phys_lowmem;
	}

	return blkcipher_next_fast(desc, walk);

set_phys_lowmem:
	if (walk->flags & BLKCIPHER_WALK_PHYS) {
		walk->src.phys.page = virt_to_page(walk->src.virt.addr);
		walk->dst.phys.page = virt_to_page(walk->dst.virt.addr);
		walk->src.phys.offset &= PAGE_SIZE - 1;
		walk->dst.phys.offset &= PAGE_SIZE - 1;
	}
	return err;
}

static inline int blkcipher_copy_iv(struct blkcipher_walk *walk)
{
	unsigned bs = walk->walk_blocksize;
	unsigned aligned_bs = ALIGN(bs, walk->alignmask + 1);
	unsigned int size = aligned_bs * 2 +
			    walk->ivsize + max(aligned_bs, walk->ivsize) -
			    (walk->alignmask + 1);
	u8 *iv;

	size += walk->alignmask & ~(crypto_tfm_ctx_alignment() - 1);
	walk->buffer = kmalloc(size, GFP_ATOMIC);
	if (!walk->buffer)
		return -ENOMEM;

	iv = (u8 *)ALIGN((unsigned long)walk->buffer, walk->alignmask + 1);
	iv = blkcipher_get_spot(iv, bs) + aligned_bs;
	iv = blkcipher_get_spot(iv, bs) + aligned_bs;
	iv = blkcipher_get_spot(iv, walk->ivsize);

	walk->iv = memcpy(iv, walk->iv, walk->ivsize);
	return 0;
}

int blkcipher_walk_virt(struct blkcipher_desc *desc,
			struct blkcipher_walk *walk)
{
	walk->flags &= ~BLKCIPHER_WALK_PHYS;
	walk->walk_blocksize = crypto_blkcipher_blocksize(desc->tfm);
	walk->cipher_blocksize = walk->walk_blocksize;
	walk->ivsize = crypto_blkcipher_ivsize(desc->tfm);
	walk->alignmask = crypto_blkcipher_alignmask(desc->tfm);
	return blkcipher_walk_first(desc, walk);
}
EXPORT_SYMBOL_GPL(blkcipher_walk_virt);

int blkcipher_walk_phys(struct blkcipher_desc *desc,
			struct blkcipher_walk *walk)
{
	walk->flags |= BLKCIPHER_WALK_PHYS;
	walk->walk_blocksize = crypto_blkcipher_blocksize(desc->tfm);
	walk->cipher_blocksize = walk->walk_blocksize;
	walk->ivsize = crypto_blkcipher_ivsize(desc->tfm);
	walk->alignmask = crypto_blkcipher_alignmask(desc->tfm);
	return blkcipher_walk_first(desc, walk);
}
EXPORT_SYMBOL_GPL(blkcipher_walk_phys);

static int blkcipher_walk_first(struct blkcipher_desc *desc,
				struct blkcipher_walk *walk)
{
	if (WARN_ON_ONCE(in_irq()))
		return -EDEADLK;

	walk->iv = desc->info;
	walk->nbytes = walk->total;
	if (unlikely(!walk->total))
		return 0;

	walk->buffer = NULL;
	if (unlikely(((unsigned long)walk->iv & walk->alignmask))) {
		int err = blkcipher_copy_iv(walk);
		if (err)
			return err;
	}

	scatterwalk_start(&walk->in, walk->in.sg);
	scatterwalk_start(&walk->out, walk->out.sg);
	walk->page = NULL;

	return blkcipher_walk_next(desc, walk);
}

int blkcipher_walk_virt_block(struct blkcipher_desc *desc,
			      struct blkcipher_walk *walk,
			      unsigned int blocksize)
{
	walk->flags &= ~BLKCIPHER_WALK_PHYS;
	walk->walk_blocksize = blocksize;
	walk->cipher_blocksize = crypto_blkcipher_blocksize(desc->tfm);
	walk->ivsize = crypto_blkcipher_ivsize(desc->tfm);
	walk->alignmask = crypto_blkcipher_alignmask(desc->tfm);
	return blkcipher_walk_first(desc, walk);
}
EXPORT_SYMBOL_GPL(blkcipher_walk_virt_block);

int blkcipher_aead_walk_virt_block(struct blkcipher_desc *desc,
				   struct blkcipher_walk *walk,
				   struct crypto_aead *tfm,
				   unsigned int blocksize)
{
	walk->flags &= ~BLKCIPHER_WALK_PHYS;
	walk->walk_blocksize = blocksize;
	walk->cipher_blocksize = crypto_aead_blocksize(tfm);
	walk->ivsize = crypto_aead_ivsize(tfm);
	walk->alignmask = crypto_aead_alignmask(tfm);
	return blkcipher_walk_first(desc, walk);
}
EXPORT_SYMBOL_GPL(blkcipher_aead_walk_virt_block);

static int setkey_unaligned(struct crypto_tfm *tfm, const u8 *key,
			    unsigned int keylen)
{
	struct blkcipher_alg *cipher = &tfm->__crt_alg->cra_blkcipher;
	unsigned long alignmask = crypto_tfm_alg_alignmask(tfm);
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

static int setkey(struct crypto_tfm *tfm, const u8 *key, unsigned int keylen)
{
	struct blkcipher_alg *cipher = &tfm->__crt_alg->cra_blkcipher;
	unsigned long alignmask = crypto_tfm_alg_alignmask(tfm);

	if (keylen < cipher->min_keysize || keylen > cipher->max_keysize) {
		tfm->crt_flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}

	if ((unsigned long)key & alignmask)
		return setkey_unaligned(tfm, key, keylen);

	return cipher->setkey(tfm, key, keylen);
}

static int async_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
			unsigned int keylen)
{
	return setkey(crypto_ablkcipher_tfm(tfm), key, keylen);
}

static int async_encrypt(struct ablkcipher_request *req)
{
	struct crypto_tfm *tfm = req->base.tfm;
	struct blkcipher_alg *alg = &tfm->__crt_alg->cra_blkcipher;
	struct blkcipher_desc desc = {
		.tfm = __crypto_blkcipher_cast(tfm),
		.info = req->info,
		.flags = req->base.flags,
	};


	return alg->encrypt(&desc, req->dst, req->src, req->nbytes);
}

static int async_decrypt(struct ablkcipher_request *req)
{
	struct crypto_tfm *tfm = req->base.tfm;
	struct blkcipher_alg *alg = &tfm->__crt_alg->cra_blkcipher;
	struct blkcipher_desc desc = {
		.tfm = __crypto_blkcipher_cast(tfm),
		.info = req->info,
		.flags = req->base.flags,
	};

	return alg->decrypt(&desc, req->dst, req->src, req->nbytes);
}

static unsigned int crypto_blkcipher_ctxsize(struct crypto_alg *alg, u32 type,
					     u32 mask)
{
	struct blkcipher_alg *cipher = &alg->cra_blkcipher;
	unsigned int len = alg->cra_ctxsize;

	if ((mask & CRYPTO_ALG_TYPE_MASK) == CRYPTO_ALG_TYPE_MASK &&
	    cipher->ivsize) {
		len = ALIGN(len, (unsigned long)alg->cra_alignmask + 1);
		len += cipher->ivsize;
	}

	return len;
}

static int crypto_init_blkcipher_ops_async(struct crypto_tfm *tfm)
{
	struct ablkcipher_tfm *crt = &tfm->crt_ablkcipher;
	struct blkcipher_alg *alg = &tfm->__crt_alg->cra_blkcipher;

	crt->setkey = async_setkey;
	crt->encrypt = async_encrypt;
	crt->decrypt = async_decrypt;
	crt->base = __crypto_ablkcipher_cast(tfm);
	crt->ivsize = alg->ivsize;

	return 0;
}

static int crypto_init_blkcipher_ops_sync(struct crypto_tfm *tfm)
{
	struct blkcipher_tfm *crt = &tfm->crt_blkcipher;
	struct blkcipher_alg *alg = &tfm->__crt_alg->cra_blkcipher;
	unsigned long align = crypto_tfm_alg_alignmask(tfm) + 1;
	unsigned long addr;

	crt->setkey = setkey;
	crt->encrypt = alg->encrypt;
	crt->decrypt = alg->decrypt;

	addr = (unsigned long)crypto_tfm_ctx(tfm);
	addr = ALIGN(addr, align);
	addr += ALIGN(tfm->__crt_alg->cra_ctxsize, align);
	crt->iv = (void *)addr;

	return 0;
}

static int crypto_init_blkcipher_ops(struct crypto_tfm *tfm, u32 type, u32 mask)
{
	struct blkcipher_alg *alg = &tfm->__crt_alg->cra_blkcipher;

	if (alg->ivsize > PAGE_SIZE / 8)
		return -EINVAL;

	if ((mask & CRYPTO_ALG_TYPE_MASK) == CRYPTO_ALG_TYPE_MASK)
		return crypto_init_blkcipher_ops_sync(tfm);
	else
		return crypto_init_blkcipher_ops_async(tfm);
}

#ifdef CONFIG_NET
static int crypto_blkcipher_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_blkcipher rblkcipher;

	memset(&rblkcipher, 0, sizeof(rblkcipher));

	strscpy(rblkcipher.type, "blkcipher", sizeof(rblkcipher.type));
	strscpy(rblkcipher.geniv, "<default>", sizeof(rblkcipher.geniv));

	rblkcipher.blocksize = alg->cra_blocksize;
	rblkcipher.min_keysize = alg->cra_blkcipher.min_keysize;
	rblkcipher.max_keysize = alg->cra_blkcipher.max_keysize;
	rblkcipher.ivsize = alg->cra_blkcipher.ivsize;

	return nla_put(skb, CRYPTOCFGA_REPORT_BLKCIPHER,
		       sizeof(rblkcipher), &rblkcipher);
}
#else
static int crypto_blkcipher_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif

static void crypto_blkcipher_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;
static void crypto_blkcipher_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_printf(m, "type         : blkcipher\n");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "min keysize  : %u\n", alg->cra_blkcipher.min_keysize);
	seq_printf(m, "max keysize  : %u\n", alg->cra_blkcipher.max_keysize);
	seq_printf(m, "ivsize       : %u\n", alg->cra_blkcipher.ivsize);
	seq_printf(m, "geniv        : <default>\n");
}

const struct crypto_type crypto_blkcipher_type = {
	.ctxsize = crypto_blkcipher_ctxsize,
	.init = crypto_init_blkcipher_ops,
#ifdef CONFIG_PROC_FS
	.show = crypto_blkcipher_show,
#endif
	.report = crypto_blkcipher_report,
};
EXPORT_SYMBOL_GPL(crypto_blkcipher_type);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic block chaining cipher type");
