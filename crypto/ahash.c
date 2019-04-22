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
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/cryptouser.h>
#include <linux/compiler.h>
#include <net/netlink.h>

#include "internal.h"

struct ahash_request_priv {
	crypto_completion_t complete;
	void *data;
	u8 *result;
	u32 flags;
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

	if (walk->flags & CRYPTO_ALG_ASYNC)
		walk->data = kmap(walk->pg);
	else
		walk->data = kmap_atomic(walk->pg);
	walk->data += offset;

	if (offset & alignmask) {
		unsigned int unaligned = alignmask + 1 - (offset & alignmask);

		if (nbytes > unaligned)
			nbytes = unaligned;
	}

	walk->entrylen -= nbytes;
	return nbytes;
}

static int hash_walk_new_entry(struct crypto_hash_walk *walk)
{
	struct scatterlist *sg;

	sg = walk->sg;
	walk->offset = sg->offset;
	walk->pg = sg_page(walk->sg) + (walk->offset >> PAGE_SHIFT);
	walk->offset = offset_in_page(walk->offset);
	walk->entrylen = sg->length;

	if (walk->entrylen > walk->total)
		walk->entrylen = walk->total;
	walk->total -= walk->entrylen;

	return hash_walk_next(walk);
}

int crypto_hash_walk_done(struct crypto_hash_walk *walk, int err)
{
	unsigned int alignmask = walk->alignmask;

	walk->data -= walk->offset;

	if (walk->entrylen && (walk->offset & alignmask) && !err) {
		unsigned int nbytes;

		walk->offset = ALIGN(walk->offset, alignmask + 1);
		nbytes = min(walk->entrylen,
			     (unsigned int)(PAGE_SIZE - walk->offset));
		if (nbytes) {
			walk->entrylen -= nbytes;
			walk->data += walk->offset;
			return nbytes;
		}
	}

	if (walk->flags & CRYPTO_ALG_ASYNC)
		kunmap(walk->pg);
	else {
		kunmap_atomic(walk->data);
		/*
		 * The may sleep test only makes sense for sync users.
		 * Async users don't need to sleep here anyway.
		 */
		crypto_yield(walk->flags);
	}

	if (err)
		return err;

	if (walk->entrylen) {
		walk->offset = 0;
		walk->pg++;
		return hash_walk_next(walk);
	}

	if (!walk->total)
		return 0;

	walk->sg = sg_next(walk->sg);

	return hash_walk_new_entry(walk);
}
EXPORT_SYMBOL_GPL(crypto_hash_walk_done);

int crypto_hash_walk_first(struct ahash_request *req,
			   struct crypto_hash_walk *walk)
{
	walk->total = req->nbytes;

	if (!walk->total) {
		walk->entrylen = 0;
		return 0;
	}

	walk->alignmask = crypto_ahash_alignmask(crypto_ahash_reqtfm(req));
	walk->sg = req->src;
	walk->flags = req->base.flags & CRYPTO_TFM_REQ_MASK;

	return hash_walk_new_entry(walk);
}
EXPORT_SYMBOL_GPL(crypto_hash_walk_first);

int crypto_ahash_walk_first(struct ahash_request *req,
			    struct crypto_hash_walk *walk)
{
	walk->total = req->nbytes;

	if (!walk->total) {
		walk->entrylen = 0;
		return 0;
	}

	walk->alignmask = crypto_ahash_alignmask(crypto_ahash_reqtfm(req));
	walk->sg = req->src;
	walk->flags = req->base.flags & CRYPTO_TFM_REQ_MASK;
	walk->flags |= CRYPTO_ALG_ASYNC;

	BUILD_BUG_ON(CRYPTO_TFM_REQ_MASK & CRYPTO_ALG_ASYNC);

	return hash_walk_new_entry(walk);
}
EXPORT_SYMBOL_GPL(crypto_ahash_walk_first);

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

static int ahash_nosetkey(struct crypto_ahash *tfm, const u8 *key,
			  unsigned int keylen)
{
	return -ENOSYS;
}

static void ahash_set_needkey(struct crypto_ahash *tfm)
{
	const struct hash_alg_common *alg = crypto_hash_alg_common(tfm);

	if (tfm->setkey != ahash_nosetkey &&
	    !(alg->base.cra_flags & CRYPTO_ALG_OPTIONAL_KEY))
		crypto_ahash_set_flags(tfm, CRYPTO_TFM_NEED_KEY);
}

int crypto_ahash_setkey(struct crypto_ahash *tfm, const u8 *key,
			unsigned int keylen)
{
	unsigned long alignmask = crypto_ahash_alignmask(tfm);
	int err;

	if ((unsigned long)key & alignmask)
		err = ahash_setkey_unaligned(tfm, key, keylen);
	else
		err = tfm->setkey(tfm, key, keylen);

	if (unlikely(err)) {
		ahash_set_needkey(tfm);
		return err;
	}

	crypto_ahash_clear_flags(tfm, CRYPTO_TFM_NEED_KEY);
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_ahash_setkey);

static inline unsigned int ahash_align_buffer_size(unsigned len,
						   unsigned long mask)
{
	return len + (mask & ~(crypto_tfm_ctx_alignment() - 1));
}

static int ahash_save_req(struct ahash_request *req, crypto_completion_t cplt)
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

	/*
	 * WARNING: Voodoo programming below!
	 *
	 * The code below is obscure and hard to understand, thus explanation
	 * is necessary. See include/crypto/hash.h and include/linux/crypto.h
	 * to understand the layout of structures used here!
	 *
	 * The code here will replace portions of the ORIGINAL request with
	 * pointers to new code and buffers so the hashing operation can store
	 * the result in aligned buffer. We will call the modified request
	 * an ADJUSTED request.
	 *
	 * The newly mangled request will look as such:
	 *
	 * req {
	 *   .result        = ADJUSTED[new aligned buffer]
	 *   .base.complete = ADJUSTED[pointer to completion function]
	 *   .base.data     = ADJUSTED[*req (pointer to self)]
	 *   .priv          = ADJUSTED[new priv] {
	 *           .result   = ORIGINAL(result)
	 *           .complete = ORIGINAL(base.complete)
	 *           .data     = ORIGINAL(base.data)
	 *   }
	 */

	priv->result = req->result;
	priv->complete = req->base.complete;
	priv->data = req->base.data;
	priv->flags = req->base.flags;

	/*
	 * WARNING: We do not backup req->priv here! The req->priv
	 *          is for internal use of the Crypto API and the
	 *          user must _NOT_ _EVER_ depend on it's content!
	 */

	req->result = PTR_ALIGN((u8 *)priv->ubuf, alignmask + 1);
	req->base.complete = cplt;
	req->base.data = req;
	req->priv = priv;

	return 0;
}

static void ahash_restore_req(struct ahash_request *req, int err)
{
	struct ahash_request_priv *priv = req->priv;

	if (!err)
		memcpy(priv->result, req->result,
		       crypto_ahash_digestsize(crypto_ahash_reqtfm(req)));

	/* Restore the original crypto request. */
	req->result = priv->result;

	ahash_request_set_callback(req, priv->flags,
				   priv->complete, priv->data);
	req->priv = NULL;

	/* Free the req->priv.priv from the ADJUSTED request. */
	kzfree(priv);
}

static void ahash_notify_einprogress(struct ahash_request *req)
{
	struct ahash_request_priv *priv = req->priv;
	struct crypto_async_request oreq;

	oreq.data = priv->data;

	priv->complete(&oreq, -EINPROGRESS);
}

static void ahash_op_unaligned_done(struct crypto_async_request *req, int err)
{
	struct ahash_request *areq = req->data;

	if (err == -EINPROGRESS) {
		ahash_notify_einprogress(areq);
		return;
	}

	/*
	 * Restore the original request, see ahash_op_unaligned() for what
	 * goes where.
	 *
	 * The "struct ahash_request *req" here is in fact the "req.base"
	 * from the ADJUSTED request from ahash_op_unaligned(), thus as it
	 * is a pointer to self, it is also the ADJUSTED "req" .
	 */

	/* First copy req->result into req->priv.result */
	ahash_restore_req(areq, err);

	/* Complete the ORIGINAL request. */
	areq->base.complete(&areq->base, err);
}

static int ahash_op_unaligned(struct ahash_request *req,
			      int (*op)(struct ahash_request *))
{
	int err;

	err = ahash_save_req(req, ahash_op_unaligned_done);
	if (err)
		return err;

	err = op(req);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

	ahash_restore_req(req, err);

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
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct crypto_alg *alg = tfm->base.__crt_alg;
	unsigned int nbytes = req->nbytes;
	int ret;

	crypto_stats_get(alg);
	ret = crypto_ahash_op(req, crypto_ahash_reqtfm(req)->final);
	crypto_stats_ahash_final(nbytes, ret, alg);
	return ret;
}
EXPORT_SYMBOL_GPL(crypto_ahash_final);

int crypto_ahash_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct crypto_alg *alg = tfm->base.__crt_alg;
	unsigned int nbytes = req->nbytes;
	int ret;

	crypto_stats_get(alg);
	ret = crypto_ahash_op(req, crypto_ahash_reqtfm(req)->finup);
	crypto_stats_ahash_final(nbytes, ret, alg);
	return ret;
}
EXPORT_SYMBOL_GPL(crypto_ahash_finup);

int crypto_ahash_digest(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct crypto_alg *alg = tfm->base.__crt_alg;
	unsigned int nbytes = req->nbytes;
	int ret;

	crypto_stats_get(alg);
	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		ret = -ENOKEY;
	else
		ret = crypto_ahash_op(req, tfm->digest);
	crypto_stats_ahash_final(nbytes, ret, alg);
	return ret;
}
EXPORT_SYMBOL_GPL(crypto_ahash_digest);

static void ahash_def_finup_done2(struct crypto_async_request *req, int err)
{
	struct ahash_request *areq = req->data;

	if (err == -EINPROGRESS)
		return;

	ahash_restore_req(areq, err);

	areq->base.complete(&areq->base, err);
}

static int ahash_def_finup_finish1(struct ahash_request *req, int err)
{
	if (err)
		goto out;

	req->base.complete = ahash_def_finup_done2;

	err = crypto_ahash_reqtfm(req)->final(req);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

out:
	ahash_restore_req(req, err);
	return err;
}

static void ahash_def_finup_done1(struct crypto_async_request *req, int err)
{
	struct ahash_request *areq = req->data;

	if (err == -EINPROGRESS) {
		ahash_notify_einprogress(areq);
		return;
	}

	areq->base.flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	err = ahash_def_finup_finish1(areq, err);
	if (areq->priv)
		return;

	areq->base.complete(&areq->base, err);
}

static int ahash_def_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	int err;

	err = ahash_save_req(req, ahash_def_finup_done1);
	if (err)
		return err;

	err = tfm->update(req);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

	return ahash_def_finup_finish1(req, err);
}

static int crypto_ahash_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_ahash *hash = __crypto_ahash_cast(tfm);
	struct ahash_alg *alg = crypto_ahash_alg(hash);

	hash->setkey = ahash_nosetkey;

	if (tfm->__crt_alg->cra_type != &crypto_ahash_type)
		return crypto_init_shash_ops_async(tfm);

	hash->init = alg->init;
	hash->update = alg->update;
	hash->final = alg->final;
	hash->finup = alg->finup ?: ahash_def_finup;
	hash->digest = alg->digest;
	hash->export = alg->export;
	hash->import = alg->import;

	if (alg->setkey) {
		hash->setkey = alg->setkey;
		ahash_set_needkey(hash);
	}

	return 0;
}

static unsigned int crypto_ahash_extsize(struct crypto_alg *alg)
{
	if (alg->cra_type != &crypto_ahash_type)
		return sizeof(struct crypto_shash *);

	return crypto_alg_extsize(alg);
}

#ifdef CONFIG_NET
static int crypto_ahash_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_hash rhash;

	memset(&rhash, 0, sizeof(rhash));

	strscpy(rhash.type, "ahash", sizeof(rhash.type));

	rhash.blocksize = alg->cra_blocksize;
	rhash.digestsize = __crypto_hash_alg_common(alg)->digestsize;

	return nla_put(skb, CRYPTOCFGA_REPORT_HASH, sizeof(rhash), &rhash);
}
#else
static int crypto_ahash_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif

static void crypto_ahash_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;
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
	.report = crypto_ahash_report,
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

int crypto_has_ahash(const char *alg_name, u32 type, u32 mask)
{
	return crypto_type_has_alg(alg_name, &crypto_ahash_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_has_ahash);

static int ahash_prepare_alg(struct ahash_alg *alg)
{
	struct crypto_alg *base = &alg->halg.base;

	if (alg->halg.digestsize > HASH_MAX_DIGESTSIZE ||
	    alg->halg.statesize > HASH_MAX_STATESIZE ||
	    alg->halg.statesize == 0)
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

int crypto_register_ahashes(struct ahash_alg *algs, int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = crypto_register_ahash(&algs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (--i; i >= 0; --i)
		crypto_unregister_ahash(&algs[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_register_ahashes);

void crypto_unregister_ahashes(struct ahash_alg *algs, int count)
{
	int i;

	for (i = count - 1; i >= 0; --i)
		crypto_unregister_ahash(&algs[i]);
}
EXPORT_SYMBOL_GPL(crypto_unregister_ahashes);

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

bool crypto_hash_alg_has_setkey(struct hash_alg_common *halg)
{
	struct crypto_alg *alg = &halg->base;

	if (alg->cra_type != &crypto_ahash_type)
		return crypto_shash_alg_has_setkey(__crypto_shash_alg(alg));

	return __crypto_ahash_alg(alg)->setkey != NULL;
}
EXPORT_SYMBOL_GPL(crypto_hash_alg_has_setkey);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Asynchronous cryptographic hash type");
