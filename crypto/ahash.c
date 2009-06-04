/*
 * Asynchronous Cryptographic Hash operations.
 *
 * This is the asynchronous version of hash.c with notification of
 * completion via a callback.
 *
 * Copyright (c) 2008 Loc Ho <lho@amcc.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/hash.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/seq_file.h>

#include "internal.h"

static int hash_walk_next(struct crypto_hash_walk *walk)
{
	unsigned int alignmask = walk->alignmask;
	unsigned int offset = walk->offset;
	unsigned int nbytes = min(walk->entrylen,
				  ((unsigned int)(PAGE_SIZE)) - offset);

	walk->data = crypto_kmap(walk->pg, 0);
	walk->data += offset;

	if (offset & alignmask)
		nbytes = alignmask + 1 - (offset & alignmask);

	walk->entrylen -= nbytes;
	return nbytes;
}

static int hash_walk_new_entry(struct crypto_hash_walk *walk)
{
	struct scatterlist *sg;

	sg = walk->sg;
	walk->pg = sg_page(sg);
	walk->offset = sg->offset;
	walk->entrylen = sg->length;

	if (walk->entrylen > walk->total)
		walk->entrylen = walk->total;
	walk->total -= walk->entrylen;

	return hash_walk_next(walk);
}

int crypto_hash_walk_done(struct crypto_hash_walk *walk, int err)
{
	unsigned int alignmask = walk->alignmask;
	unsigned int nbytes = walk->entrylen;

	walk->data -= walk->offset;

	if (nbytes && walk->offset & alignmask && !err) {
		walk->offset += alignmask - 1;
		walk->offset = ALIGN(walk->offset, alignmask + 1);
		walk->data += walk->offset;

		nbytes = min(nbytes,
			     ((unsigned int)(PAGE_SIZE)) - walk->offset);
		walk->entrylen -= nbytes;

		return nbytes;
	}

	crypto_kunmap(walk->data, 0);
	crypto_yield(walk->flags);

	if (err)
		return err;

	if (nbytes) {
		walk->offset = 0;
		walk->pg++;
		return hash_walk_next(walk);
	}

	if (!walk->total)
		return 0;

	walk->sg = scatterwalk_sg_next(walk->sg);

	return hash_walk_new_entry(walk);
}
EXPORT_SYMBOL_GPL(crypto_hash_walk_done);

int crypto_hash_walk_first(struct ahash_request *req,
			   struct crypto_hash_walk *walk)
{
	walk->total = req->nbytes;

	if (!walk->total)
		return 0;

	walk->alignmask = crypto_ahash_alignmask(crypto_ahash_reqtfm(req));
	walk->sg = req->src;
	walk->flags = req->base.flags;

	return hash_walk_new_entry(walk);
}
EXPORT_SYMBOL_GPL(crypto_hash_walk_first);

int crypto_hash_walk_first_compat(struct hash_desc *hdesc,
				  struct crypto_hash_walk *walk,
				  struct scatterlist *sg, unsigned int len)
{
	walk->total = len;

	if (!walk->total)
		return 0;

	walk->alignmask = crypto_hash_alignmask(hdesc->tfm);
	walk->sg = sg;
	walk->flags = hdesc->flags;

	return hash_walk_new_entry(walk);
}

static int ahash_setkey_unaligned(struct crypto_ahash *tfm, const u8 *key,
				unsigned int keylen)
{
	struct ahash_alg *ahash = crypto_ahash_alg(tfm);
	unsigned long alignmask = crypto_ahash_alignmask(tfm);
	int ret;
	u8 *buffer, *alignbuffer;
	unsigned long absize;

	absize = keylen + alignmask;
	buffer = kmalloc(absize, GFP_ATOMIC);
	if (!buffer)
		return -ENOMEM;

	alignbuffer = (u8 *)ALIGN((unsigned long)buffer, alignmask + 1);
	memcpy(alignbuffer, key, keylen);
	ret = ahash->setkey(tfm, alignbuffer, keylen);
	memset(alignbuffer, 0, keylen);
	kfree(buffer);
	return ret;
}

static int ahash_setkey(struct crypto_ahash *tfm, const u8 *key,
			unsigned int keylen)
{
	struct ahash_alg *ahash = crypto_ahash_alg(tfm);
	unsigned long alignmask = crypto_ahash_alignmask(tfm);

	if ((unsigned long)key & alignmask)
		return ahash_setkey_unaligned(tfm, key, keylen);

	return ahash->setkey(tfm, key, keylen);
}

static int ahash_nosetkey(struct crypto_ahash *tfm, const u8 *key,
			  unsigned int keylen)
{
	return -ENOSYS;
}

int crypto_ahash_import(struct ahash_request *req, const u8 *in)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);

	memcpy(ahash_request_ctx(req), in, crypto_ahash_reqsize(tfm));

	if (alg->reinit)
		alg->reinit(req);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_ahash_import);

static unsigned int crypto_ahash_ctxsize(struct crypto_alg *alg, u32 type,
					u32 mask)
{
	return alg->cra_ctxsize;
}

static int crypto_init_ahash_ops(struct crypto_tfm *tfm, u32 type, u32 mask)
{
	struct ahash_alg *alg = &tfm->__crt_alg->cra_ahash;
	struct ahash_tfm *crt   = &tfm->crt_ahash;

	if (alg->digestsize > PAGE_SIZE / 8)
		return -EINVAL;

	crt->init = alg->init;
	crt->update = alg->update;
	crt->final  = alg->final;
	crt->digest = alg->digest;
	crt->setkey = alg->setkey ? ahash_setkey : ahash_nosetkey;
	crt->digestsize = alg->digestsize;

	return 0;
}

static void crypto_ahash_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_ahash_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_printf(m, "type         : ahash\n");
	seq_printf(m, "async        : %s\n", alg->cra_flags & CRYPTO_ALG_ASYNC ?
					     "yes" : "no");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "digestsize   : %u\n", alg->cra_ahash.digestsize);
}

const struct crypto_type crypto_ahash_type = {
	.ctxsize = crypto_ahash_ctxsize,
	.init = crypto_init_ahash_ops,
#ifdef CONFIG_PROC_FS
	.show = crypto_ahash_show,
#endif
};
EXPORT_SYMBOL_GPL(crypto_ahash_type);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Asynchronous cryptographic hash type");
