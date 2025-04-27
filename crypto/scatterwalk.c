// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * Cipher operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *               2002 Adam J. Richter <adam@yggdrasil.com>
 *               2004 Jean-Luc Cooke <jlcooke@certainkey.com>
 */

#include <crypto/scatterwalk.h>
#include <linux/crypto.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

enum {
	SKCIPHER_WALK_SLOW = 1 << 0,
	SKCIPHER_WALK_COPY = 1 << 1,
	SKCIPHER_WALK_DIFF = 1 << 2,
	SKCIPHER_WALK_SLEEP = 1 << 3,
};

static inline gfp_t skcipher_walk_gfp(struct skcipher_walk *walk)
{
	return walk->flags & SKCIPHER_WALK_SLEEP ? GFP_KERNEL : GFP_ATOMIC;
}

void scatterwalk_skip(struct scatter_walk *walk, unsigned int nbytes)
{
	struct scatterlist *sg = walk->sg;

	nbytes += walk->offset - sg->offset;

	while (nbytes > sg->length) {
		nbytes -= sg->length;
		sg = sg_next(sg);
	}
	walk->sg = sg;
	walk->offset = sg->offset + nbytes;
}
EXPORT_SYMBOL_GPL(scatterwalk_skip);

inline void memcpy_from_scatterwalk(void *buf, struct scatter_walk *walk,
				    unsigned int nbytes)
{
	do {
		unsigned int to_copy;

		to_copy = scatterwalk_next(walk, nbytes);
		memcpy(buf, walk->addr, to_copy);
		scatterwalk_done_src(walk, to_copy);
		buf += to_copy;
		nbytes -= to_copy;
	} while (nbytes);
}
EXPORT_SYMBOL_GPL(memcpy_from_scatterwalk);

inline void memcpy_to_scatterwalk(struct scatter_walk *walk, const void *buf,
				  unsigned int nbytes)
{
	do {
		unsigned int to_copy;

		to_copy = scatterwalk_next(walk, nbytes);
		memcpy(walk->addr, buf, to_copy);
		scatterwalk_done_dst(walk, to_copy);
		buf += to_copy;
		nbytes -= to_copy;
	} while (nbytes);
}
EXPORT_SYMBOL_GPL(memcpy_to_scatterwalk);

void memcpy_from_sglist(void *buf, struct scatterlist *sg,
			unsigned int start, unsigned int nbytes)
{
	struct scatter_walk walk;

	if (unlikely(nbytes == 0)) /* in case sg == NULL */
		return;

	scatterwalk_start_at_pos(&walk, sg, start);
	memcpy_from_scatterwalk(buf, &walk, nbytes);
}
EXPORT_SYMBOL_GPL(memcpy_from_sglist);

void memcpy_to_sglist(struct scatterlist *sg, unsigned int start,
		      const void *buf, unsigned int nbytes)
{
	struct scatter_walk walk;

	if (unlikely(nbytes == 0)) /* in case sg == NULL */
		return;

	scatterwalk_start_at_pos(&walk, sg, start);
	memcpy_to_scatterwalk(&walk, buf, nbytes);
}
EXPORT_SYMBOL_GPL(memcpy_to_sglist);

void memcpy_sglist(struct scatterlist *dst, struct scatterlist *src,
		   unsigned int nbytes)
{
	struct skcipher_walk walk = {};

	if (unlikely(nbytes == 0)) /* in case sg == NULL */
		return;

	walk.total = nbytes;

	scatterwalk_start(&walk.in, src);
	scatterwalk_start(&walk.out, dst);

	skcipher_walk_first(&walk, true);
	do {
		if (walk.src.virt.addr != walk.dst.virt.addr)
			memcpy(walk.dst.virt.addr, walk.src.virt.addr,
			       walk.nbytes);
		skcipher_walk_done(&walk, 0);
	} while (walk.nbytes);
}
EXPORT_SYMBOL_GPL(memcpy_sglist);

struct scatterlist *scatterwalk_ffwd(struct scatterlist dst[2],
				     struct scatterlist *src,
				     unsigned int len)
{
	for (;;) {
		if (!len)
			return src;

		if (src->length > len)
			break;

		len -= src->length;
		src = sg_next(src);
	}

	sg_init_table(dst, 2);
	sg_set_page(dst, sg_page(src), src->length - len, src->offset + len);
	scatterwalk_crypto_chain(dst, sg_next(src), 2);

	return dst;
}
EXPORT_SYMBOL_GPL(scatterwalk_ffwd);

static int skcipher_next_slow(struct skcipher_walk *walk, unsigned int bsize)
{
	unsigned alignmask = walk->alignmask;
	unsigned n;
	void *buffer;

	if (!walk->buffer)
		walk->buffer = walk->page;
	buffer = walk->buffer;
	if (!buffer) {
		/* Min size for a buffer of bsize bytes aligned to alignmask */
		n = bsize + (alignmask & ~(crypto_tfm_ctx_alignment() - 1));

		buffer = kzalloc(n, skcipher_walk_gfp(walk));
		if (!buffer)
			return skcipher_walk_done(walk, -ENOMEM);
		walk->buffer = buffer;
	}

	buffer = PTR_ALIGN(buffer, alignmask + 1);
	memcpy_from_scatterwalk(buffer, &walk->in, bsize);
	walk->out.__addr = buffer;
	walk->in.__addr = walk->out.addr;

	walk->nbytes = bsize;
	walk->flags |= SKCIPHER_WALK_SLOW;

	return 0;
}

static int skcipher_next_copy(struct skcipher_walk *walk)
{
	void *tmp = walk->page;

	scatterwalk_map(&walk->in);
	memcpy(tmp, walk->in.addr, walk->nbytes);
	scatterwalk_unmap(&walk->in);
	/*
	 * walk->in is advanced later when the number of bytes actually
	 * processed (which might be less than walk->nbytes) is known.
	 */

	walk->in.__addr = tmp;
	walk->out.__addr = tmp;
	return 0;
}

static int skcipher_next_fast(struct skcipher_walk *walk)
{
	unsigned long diff;

	diff = offset_in_page(walk->in.offset) -
	       offset_in_page(walk->out.offset);
	diff |= (u8 *)(sg_page(walk->in.sg) + (walk->in.offset >> PAGE_SHIFT)) -
		(u8 *)(sg_page(walk->out.sg) + (walk->out.offset >> PAGE_SHIFT));

	scatterwalk_map(&walk->out);
	walk->in.__addr = walk->out.__addr;

	if (diff) {
		walk->flags |= SKCIPHER_WALK_DIFF;
		scatterwalk_map(&walk->in);
	}

	return 0;
}

static int skcipher_walk_next(struct skcipher_walk *walk)
{
	unsigned int bsize;
	unsigned int n;

	n = walk->total;
	bsize = min(walk->stride, max(n, walk->blocksize));
	n = scatterwalk_clamp(&walk->in, n);
	n = scatterwalk_clamp(&walk->out, n);

	if (unlikely(n < bsize)) {
		if (unlikely(walk->total < walk->blocksize))
			return skcipher_walk_done(walk, -EINVAL);

slow_path:
		return skcipher_next_slow(walk, bsize);
	}
	walk->nbytes = n;

	if (unlikely((walk->in.offset | walk->out.offset) & walk->alignmask)) {
		if (!walk->page) {
			gfp_t gfp = skcipher_walk_gfp(walk);

			walk->page = (void *)__get_free_page(gfp);
			if (!walk->page)
				goto slow_path;
		}
		walk->flags |= SKCIPHER_WALK_COPY;
		return skcipher_next_copy(walk);
	}

	return skcipher_next_fast(walk);
}

static int skcipher_copy_iv(struct skcipher_walk *walk)
{
	unsigned alignmask = walk->alignmask;
	unsigned ivsize = walk->ivsize;
	unsigned aligned_stride = ALIGN(walk->stride, alignmask + 1);
	unsigned size;
	u8 *iv;

	/* Min size for a buffer of stride + ivsize, aligned to alignmask */
	size = aligned_stride + ivsize +
	       (alignmask & ~(crypto_tfm_ctx_alignment() - 1));

	walk->buffer = kmalloc(size, skcipher_walk_gfp(walk));
	if (!walk->buffer)
		return -ENOMEM;

	iv = PTR_ALIGN(walk->buffer, alignmask + 1) + aligned_stride;

	walk->iv = memcpy(iv, walk->iv, walk->ivsize);
	return 0;
}

int skcipher_walk_first(struct skcipher_walk *walk, bool atomic)
{
	if (WARN_ON_ONCE(in_hardirq()))
		return -EDEADLK;

	walk->flags = atomic ? 0 : SKCIPHER_WALK_SLEEP;

	walk->buffer = NULL;
	if (unlikely(((unsigned long)walk->iv & walk->alignmask))) {
		int err = skcipher_copy_iv(walk);
		if (err)
			return err;
	}

	walk->page = NULL;

	return skcipher_walk_next(walk);
}
EXPORT_SYMBOL_GPL(skcipher_walk_first);

/**
 * skcipher_walk_done() - finish one step of a skcipher_walk
 * @walk: the skcipher_walk
 * @res: number of bytes *not* processed (>= 0) from walk->nbytes,
 *	 or a -errno value to terminate the walk due to an error
 *
 * This function cleans up after one step of walking through the source and
 * destination scatterlists, and advances to the next step if applicable.
 * walk->nbytes is set to the number of bytes available in the next step,
 * walk->total is set to the new total number of bytes remaining, and
 * walk->{src,dst}.virt.addr is set to the next pair of data pointers.  If there
 * is no more data, or if an error occurred (i.e. -errno return), then
 * walk->nbytes and walk->total are set to 0 and all resources owned by the
 * skcipher_walk are freed.
 *
 * Return: 0 or a -errno value.  If @res was a -errno value then it will be
 *	   returned, but other errors may occur too.
 */
int skcipher_walk_done(struct skcipher_walk *walk, int res)
{
	unsigned int n = walk->nbytes; /* num bytes processed this step */
	unsigned int total = 0; /* new total remaining */

	if (!n)
		goto finish;

	if (likely(res >= 0)) {
		n -= res; /* subtract num bytes *not* processed */
		total = walk->total - n;
	}

	if (likely(!(walk->flags & (SKCIPHER_WALK_SLOW |
				    SKCIPHER_WALK_COPY |
				    SKCIPHER_WALK_DIFF)))) {
		scatterwalk_advance(&walk->in, n);
	} else if (walk->flags & SKCIPHER_WALK_DIFF) {
		scatterwalk_done_src(&walk->in, n);
	} else if (walk->flags & SKCIPHER_WALK_COPY) {
		scatterwalk_advance(&walk->in, n);
		scatterwalk_map(&walk->out);
		memcpy(walk->out.addr, walk->page, n);
	} else { /* SKCIPHER_WALK_SLOW */
		if (res > 0) {
			/*
			 * Didn't process all bytes.  Either the algorithm is
			 * broken, or this was the last step and it turned out
			 * the message wasn't evenly divisible into blocks but
			 * the algorithm requires it.
			 */
			res = -EINVAL;
			total = 0;
		} else
			memcpy_to_scatterwalk(&walk->out, walk->out.addr, n);
		goto dst_done;
	}

	scatterwalk_done_dst(&walk->out, n);
dst_done:

	if (res > 0)
		res = 0;

	walk->total = total;
	walk->nbytes = 0;

	if (total) {
		if (walk->flags & SKCIPHER_WALK_SLEEP)
			cond_resched();
		walk->flags &= ~(SKCIPHER_WALK_SLOW | SKCIPHER_WALK_COPY |
				 SKCIPHER_WALK_DIFF);
		return skcipher_walk_next(walk);
	}

finish:
	/* Short-circuit for the common/fast path. */
	if (!((unsigned long)walk->buffer | (unsigned long)walk->page))
		goto out;

	if (walk->iv != walk->oiv)
		memcpy(walk->oiv, walk->iv, walk->ivsize);
	if (walk->buffer != walk->page)
		kfree(walk->buffer);
	if (walk->page)
		free_page((unsigned long)walk->page);

out:
	return res;
}
EXPORT_SYMBOL_GPL(skcipher_walk_done);
