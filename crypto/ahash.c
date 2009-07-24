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

struct ahash_request_priv {
	crypto_completion_t complete;
	void *data;
	u8 *result;
	void *ubuf[] CRYPTO_MINALIGN_ATTR;
};

static inline struct ahash_alg *crypto_ahash_alg(struct crypto_ahash *hash)
{
	return container_of(crypto_hash_alg_common(hash), struct ahash_alg,
			    halg);
}

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
	unsigned long alignmask = crypto_ahash_alignmask(tfm);
	int ret;
	u8 *buffer, *alignbuffer;
	unsigned long absize;

	absize = keylen + alignmask;
	buffer = kmalloc(absize, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	alignbuffer = (u8 *)ALIGN((unsigned long)buffer, alignmask + 1);
	memcpy(alignbuffer, key, keylen);
	ret = tfm->setkey(tfm, alignbuffer, keylen);
	kzfree(buffer);
	return ret;
}

int crypto_ahash_setkey(struct crypto_ahash *tfm, const u8 *key,
			unsigned int keylen)
{
	unsigned long alignmask = crypto_ahash_alignmask(tfm);

	if ((unsigned long)key & alignmask)
		return ahash_setkey_unaligned(tfm, key, keylen);

	return tfm->setkey(tfm, key, keylen);
}
EXPORT_SYMBOL_GPL(crypto_ahash_setkey);

static int ahash_nosetkey(struct crypto_ahash *tfm, const u8 *key,
			  unsigned int keylen)
{
	return -ENOSYS;
}

static inline unsigned int ahash_align_buffer_size(unsigned len,
						   unsigned long mask)
{
	return len + (mask & ~(crypto_tfm_ctx_alignment() - 1));
}

static void ahash_op_unaligned_finish(struct ahash_request *req, int err)
{
	struct ahash_request_priv *priv = req->priv;

	if (err == -EINPROGRESS)
		return;

	if (!err)
		memcpy(priv->result, req->result,
		       crypto_ahash_digestsize(crypto_ahash_reqtfm(req)));

	kzfree(priv);
}

static void ahash_op_unaligned_done(struct crypto_async_request *req, int err)
{
	struct ahash_request *areq = req->data;
	struct ahash_request_priv *priv = areq->priv;
	crypto_completion_t complete = priv->complete;
	void *data = priv->data;

	ahash_op_unaligned_finish(areq, err);

	complete(data, err);
}

static int ahash_op_unaligned(struct ahash_request *req,
			      int (*op)(struct ahash_request *))
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	unsigned long alignmask = crypto_ahash_alignmask(tfm);
	unsigned int ds = crypto_ahash_digestsize(tfm);
	struct ahash_request_priv *priv;
	int err;

	priv = kmalloc(sizeof(*priv) + ahash_align_buffer_size(ds, alignmask),
		       (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		       GFP_KERNEL : GFP_ATOMIC);
	if (!priv)
		return -ENOMEM;

	priv->result = req->result;
	priv->complete = req->base.complete;
	priv->data = req->base.data;

	req->result = PTR_ALIGN((u8 *)priv->ubuf, alignmask + 1);
	req->base.complete = ahash_op_unaligned_done;
	req->base.data = req;
	req->priv = priv;

	err = op(req);
	ahash_op_unaligned_finish(req, err);

	return err;
}

static int crypto_ahash_op(struct ahash_request *req,
			   int (*op)(struct ahash_request *))
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	unsigned long alignmask = crypto_ahash_alignmask(tfm);

	if ((unsigned long)req->result & alignmask)
		return ahash_op_unaligned(req, op);

	return op(req);
}

int crypto_ahash_final(struct ahash_request *req)
{
	return crypto_ahash_op(req, crypto_ahash_reqtfm(req)->final);
}
EXPORT_SYMBOL_GPL(crypto_ahash_final);

int crypto_ahash_finup(struct ahash_request *req)
{
	return crypto_ahash_op(req, crypto_ahash_reqtfm(req)->finup);
}
EXPORT_SYMBOL_GPL(crypto_ahash_finup);

int crypto_ahash_digest(struct ahash_request *req)
{
	return crypto_ahash_op(req, crypto_ahash_reqtfm(req)->digest);
}
EXPORT_SYMBOL_GPL(crypto_ahash_digest);

static void ahash_def_finup_finish2(struct ahash_request *req, int err)
{
	struct ahash_request_priv *priv = req->priv;

	if (err == -EINPROGRESS)
		return;

	if (!err)
		memcpy(priv->result, req->result,
		       crypto_ahash_digestsize(crypto_ahash_reqtfm(req)));

	kzfree(priv);
}

static void ahash_def_finup_done2(struct crypto_async_request *req, int err)
{
	struct ahash_request *areq = req->data;
	struct ahash_request_priv *priv = areq->priv;
	crypto_completion_t complete = priv->complete;
	void *data = priv->data;

	ahash_def_finup_finish2(areq, err);

	complete(data, err);
}

static int ahash_def_finup_finish1(struct ahash_request *req, int err)
{
	if (err)
		goto out;

	req->base.complete = ahash_def_finup_done2;
	req->base.flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
	err = crypto_ahash_reqtfm(req)->final(req);

out:
	ahash_def_finup_finish2(req, err);
	return err;
}

static void ahash_def_finup_done1(struct crypto_async_request *req, int err)
{
	struct ahash_request *areq = req->data;
	struct ahash_request_priv *priv = areq->priv;
	crypto_completion_t complete = priv->complete;
	void *data = priv->data;

	err = ahash_def_finup_finish1(areq, err);

	complete(data, err);
}

static int ahash_def_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	unsigned long alignmask = crypto_ahash_alignmask(tfm);
	unsigned int ds = crypto_ahash_digestsize(tfm);
	struct ahash_request_priv *priv;

	priv = kmalloc(sizeof(*priv) + ahash_align_buffer_size(ds, alignmask),
		       (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		       GFP_KERNEL : GFP_ATOMIC);
	if (!priv)
		return -ENOMEM;

	priv->result = req->result;
	priv->complete = req->base.complete;
	priv->data = req->base.data;

	req->result = PTR_ALIGN((u8 *)priv->ubuf, alignmask + 1);
	req->base.complete = ahash_def_finup_done1;
	req->base.data = req;
	req->priv = priv;

	return ahash_def_finup_finish1(req, tfm->update(req));
}

static int ahash_no_export(struct ahash_request *req, void *out)
{
	return -ENOSYS;
}

static int ahash_no_import(struct ahash_request *req, const void *in)
{
	return -ENOSYS;
}

static int crypto_ahash_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_ahash *hash = __crypto_ahash_cast(tfm);
	struct ahash_alg *alg = crypto_ahash_alg(hash);

	hash->setkey = ahash_nosetkey;
	hash->export = ahash_no_export;
	hash->import = ahash_no_import;

	if (tfm->__crt_alg->cra_type != &crypto_ahash_type)
		return crypto_init_shash_ops_async(tfm);

	hash->init = alg->init;
	hash->update = alg->update;
	hash->final = alg->final;
	hash->finup = alg->finup ?: ahash_def_finup;
	hash->digest = alg->digest;

	if (alg->setkey)
		hash->setkey = alg->setkey;
	if (alg->export)
		hash->export = alg->export;
	if (alg->import)
		hash->import = alg->import;

	return 0;
}

static unsigned int crypto_ahash_extsize(struct crypto_alg *alg)
{
	if (alg->cra_type == &crypto_ahash_type)
		return alg->cra_ctxsize;

	return sizeof(struct crypto_shash *);
}

static void crypto_ahash_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_ahash_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_printf(m, "type         : ahash\n");
	seq_printf(m, "async        : %s\n", alg->cra_flags & CRYPTO_ALG_ASYNC ?
					     "yes" : "no");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "digestsize   : %u\n",
		   __crypto_hash_alg_common(alg)->digestsize);
}

const struct crypto_type crypto_ahash_type = {
	.extsize = crypto_ahash_extsize,
	.init_tfm = crypto_ahash_init_tfm,
#ifdef CONFIG_PROC_FS
	.show = crypto_ahash_show,
#endif
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_AHASH_MASK,
	.type = CRYPTO_ALG_TYPE_AHASH,
	.tfmsize = offsetof(struct crypto_ahash, base),
};
EXPORT_SYMBOL_GPL(crypto_ahash_type);

struct crypto_ahash *crypto_alloc_ahash(const char *alg_name, u32 type,
					u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_ahash_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_ahash);

static int ahash_prepare_alg(struct ahash_alg *alg)
{
	struct crypto_alg *base = &alg->halg.base;

	if (alg->halg.digestsize > PAGE_SIZE / 8 ||
	    alg->halg.statesize > PAGE_SIZE / 8)
		return -EINVAL;

	base->cra_type = &crypto_ahash_type;
	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
	base->cra_flags |= CRYPTO_ALG_TYPE_AHASH;

	return 0;
}

int crypto_register_ahash(struct ahash_alg *alg)
{
	struct crypto_alg *base = &alg->halg.base;
	int err;

	err = ahash_prepare_alg(alg);
	if (err)
		return err;

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_ahash);

int crypto_unregister_ahash(struct ahash_alg *alg)
{
	return crypto_unregister_alg(&alg->halg.base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_ahash);

int ahash_register_instance(struct crypto_template *tmpl,
			    struct ahash_instance *inst)
{
	int err;

	err = ahash_prepare_alg(&inst->alg);
	if (err)
		return err;

	return crypto_register_instance(tmpl, ahash_crypto_instance(inst));
}
EXPORT_SYMBOL_GPL(ahash_register_instance);

void ahash_free_instance(struct crypto_instance *inst)
{
	crypto_drop_spawn(crypto_instance_ctx(inst));
	kfree(ahash_instance(inst));
}
EXPORT_SYMBOL_GPL(ahash_free_instance);

int crypto_init_ahash_spawn(struct crypto_ahash_spawn *spawn,
			    struct hash_alg_common *alg,
			    struct crypto_instance *inst)
{
	return crypto_init_spawn2(&spawn->base, &alg->base, inst,
				  &crypto_ahash_type);
}
EXPORT_SYMBOL_GPL(crypto_init_ahash_spawn);

struct hash_alg_common *ahash_attr_alg(struct rtattr *rta, u32 type, u32 mask)
{
	struct crypto_alg *alg;

	alg = crypto_attr_alg2(rta, &crypto_ahash_type, type, mask);
	return IS_ERR(alg) ? ERR_CAST(alg) : __crypto_hash_alg_common(alg);
}
EXPORT_SYMBOL_GPL(ahash_attr_alg);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Asynchronous cryptographic hash type");
