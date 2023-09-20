// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Asynchronous Cryptographic Hash operations.
 *
 * This is the asynchronous version of hash.c with notification of
 * completion via a callback.
 *
 * Copyright (c) 2008 Loc Ho <lho@amcc.com>
 */

#include <crypto/scatterwalk.h>
#include <linux/cryptouser.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <net/netlink.h>

#include "hash.h"

static const struct crypto_type crypto_ahash_type;

struct ahash_request_priv {
	crypto_completion_t complete;
	void *data;
	u8 *result;
	u32 flags;
	void *ubuf[] CRYPTO_MINALIGN_ATTR;
};

static int hash_walk_next(struct crypto_hash_walk *walk)
{
	unsigned int alignmask = walk->alignmask;
	unsigned int offset = walk->offset;
	unsigned int nbytes = min(walk->entrylen,
				  ((unsigned int)(PAGE_SIZE)) - offset);

	walk->data = kmap_local_page(walk->pg);
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

	kunmap_local(walk->data);
	crypto_yield(walk->flags);

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
	walk->flags = req->base.flags;

	return hash_walk_new_entry(walk);
}
EXPORT_SYMBOL_GPL(crypto_hash_walk_first);

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
	kfree_sensitive(buffer);
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

static int ahash_save_req(struct ahash_request *req, crypto_completion_t cplt,
			  bool has_state)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	unsigned long alignmask = crypto_ahash_alignmask(tfm);
	unsigned int ds = crypto_ahash_digestsize(tfm);
	struct ahash_request *subreq;
	unsigned int subreq_size;
	unsigned int reqsize;
	u8 *result;
	gfp_t gfp;
	u32 flags;

	subreq_size = sizeof(*subreq);
	reqsize = crypto_ahash_reqsize(tfm);
	reqsize = ALIGN(reqsize, crypto_tfm_ctx_alignment());
	subreq_size += reqsize;
	subreq_size += ds;
	subreq_size += alignmask & ~(crypto_tfm_ctx_alignment() - 1);

	flags = ahash_request_flags(req);
	gfp = (flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?  GFP_KERNEL : GFP_ATOMIC;
	subreq = kmalloc(subreq_size, gfp);
	if (!subreq)
		return -ENOMEM;

	ahash_request_set_tfm(subreq, tfm);
	ahash_request_set_callback(subreq, flags, cplt, req);

	result = (u8 *)(subreq + 1) + reqsize;
	result = PTR_ALIGN(result, alignmask + 1);

	ahash_request_set_crypt(subreq, req->src, result, req->nbytes);

	if (has_state) {
		void *state;

		state = kmalloc(crypto_ahash_statesize(tfm), gfp);
		if (!state) {
			kfree(subreq);
			return -ENOMEM;
		}

		crypto_ahash_export(req, state);
		crypto_ahash_import(subreq, state);
		kfree_sensitive(state);
	}

	req->priv = subreq;

	return 0;
}

static void ahash_restore_req(struct ahash_request *req, int err)
{
	struct ahash_request *subreq = req->priv;

	if (!err)
		memcpy(req->result, subreq->result,
		       crypto_ahash_digestsize(crypto_ahash_reqtfm(req)));

	req->priv = NULL;

	kfree_sensitive(subreq);
}

static void ahash_op_unaligned_done(void *data, int err)
{
	struct ahash_request *areq = data;

	if (err == -EINPROGRESS)
		goto out;

	/* First copy req->result into req->priv.result */
	ahash_restore_req(areq, err);

out:
	/* Complete the ORIGINAL request. */
	ahash_request_complete(areq, err);
}

static int ahash_op_unaligned(struct ahash_request *req,
			      int (*op)(struct ahash_request *),
			      bool has_state)
{
	int err;

	err = ahash_save_req(req, ahash_op_unaligned_done, has_state);
	if (err)
		return err;

	err = op(req->priv);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

	ahash_restore_req(req, err);

	return err;
}

static int crypto_ahash_op(struct ahash_request *req,
			   int (*op)(struct ahash_request *),
			   bool has_state)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	unsigned long alignmask = crypto_ahash_alignmask(tfm);
	int err;

	if ((unsigned long)req->result & alignmask)
		err = ahash_op_unaligned(req, op, has_state);
	else
		err = op(req);

	return crypto_hash_errstat(crypto_hash_alg_common(tfm), err);
}

int crypto_ahash_final(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct hash_alg_common *alg = crypto_hash_alg_common(tfm);

	if (IS_ENABLED(CONFIG_CRYPTO_STATS))
		atomic64_inc(&hash_get_stat(alg)->hash_cnt);

	return crypto_ahash_op(req, tfm->final, true);
}
EXPORT_SYMBOL_GPL(crypto_ahash_final);

int crypto_ahash_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct hash_alg_common *alg = crypto_hash_alg_common(tfm);

	if (IS_ENABLED(CONFIG_CRYPTO_STATS)) {
		struct crypto_istat_hash *istat = hash_get_stat(alg);

		atomic64_inc(&istat->hash_cnt);
		atomic64_add(req->nbytes, &istat->hash_tlen);
	}

	return crypto_ahash_op(req, tfm->finup, true);
}
EXPORT_SYMBOL_GPL(crypto_ahash_finup);

int crypto_ahash_digest(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct hash_alg_common *alg = crypto_hash_alg_common(tfm);

	if (IS_ENABLED(CONFIG_CRYPTO_STATS)) {
		struct crypto_istat_hash *istat = hash_get_stat(alg);

		atomic64_inc(&istat->hash_cnt);
		atomic64_add(req->nbytes, &istat->hash_tlen);
	}

	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return crypto_hash_errstat(alg, -ENOKEY);

	return crypto_ahash_op(req, tfm->digest, false);
}
EXPORT_SYMBOL_GPL(crypto_ahash_digest);

static void ahash_def_finup_done2(void *data, int err)
{
	struct ahash_request *areq = data;

	if (err == -EINPROGRESS)
		return;

	ahash_restore_req(areq, err);

	ahash_request_complete(areq, err);
}

static int ahash_def_finup_finish1(struct ahash_request *req, int err)
{
	struct ahash_request *subreq = req->priv;

	if (err)
		goto out;

	subreq->base.complete = ahash_def_finup_done2;

	err = crypto_ahash_reqtfm(req)->final(subreq);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

out:
	ahash_restore_req(req, err);
	return err;
}

static void ahash_def_finup_done1(void *data, int err)
{
	struct ahash_request *areq = data;
	struct ahash_request *subreq;

	if (err == -EINPROGRESS)
		goto out;

	subreq = areq->priv;
	subreq->base.flags &= CRYPTO_TFM_REQ_MAY_BACKLOG;

	err = ahash_def_finup_finish1(areq, err);
	if (err == -EINPROGRESS || err == -EBUSY)
		return;

out:
	ahash_request_complete(areq, err);
}

static int ahash_def_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	int err;

	err = ahash_save_req(req, ahash_def_finup_done1, true);
	if (err)
		return err;

	err = tfm->update(req->priv);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

	return ahash_def_finup_finish1(req, err);
}

static void crypto_ahash_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_ahash *hash = __crypto_ahash_cast(tfm);
	struct ahash_alg *alg = crypto_ahash_alg(hash);

	alg->exit_tfm(hash);
}

static int crypto_ahash_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_ahash *hash = __crypto_ahash_cast(tfm);
	struct ahash_alg *alg = crypto_ahash_alg(hash);

	hash->setkey = ahash_nosetkey;

	crypto_ahash_set_statesize(hash, alg->halg.statesize);

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

	if (alg->exit_tfm)
		tfm->exit = crypto_ahash_exit_tfm;

	return alg->init_tfm ? alg->init_tfm(hash) : 0;
}

static unsigned int crypto_ahash_extsize(struct crypto_alg *alg)
{
	if (alg->cra_type != &crypto_ahash_type)
		return sizeof(struct crypto_shash *);

	return crypto_alg_extsize(alg);
}

static void crypto_ahash_free_instance(struct crypto_instance *inst)
{
	struct ahash_instance *ahash = ahash_instance(inst);

	ahash->free(ahash);
}

static int __maybe_unused crypto_ahash_report(
	struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_hash rhash;

	memset(&rhash, 0, sizeof(rhash));

	strscpy(rhash.type, "ahash", sizeof(rhash.type));

	rhash.blocksize = alg->cra_blocksize;
	rhash.digestsize = __crypto_hash_alg_common(alg)->digestsize;

	return nla_put(skb, CRYPTOCFGA_REPORT_HASH, sizeof(rhash), &rhash);
}

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

static int __maybe_unused crypto_ahash_report_stat(
	struct sk_buff *skb, struct crypto_alg *alg)
{
	return crypto_hash_report_stat(skb, alg, "ahash");
}

static const struct crypto_type crypto_ahash_type = {
	.extsize = crypto_ahash_extsize,
	.init_tfm = crypto_ahash_init_tfm,
	.free = crypto_ahash_free_instance,
#ifdef CONFIG_PROC_FS
	.show = crypto_ahash_show,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_USER)
	.report = crypto_ahash_report,
#endif
#ifdef CONFIG_CRYPTO_STATS
	.report_stat = crypto_ahash_report_stat,
#endif
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_AHASH_MASK,
	.type = CRYPTO_ALG_TYPE_AHASH,
	.tfmsize = offsetof(struct crypto_ahash, base),
};

int crypto_grab_ahash(struct crypto_ahash_spawn *spawn,
		      struct crypto_instance *inst,
		      const char *name, u32 type, u32 mask)
{
	spawn->base.frontend = &crypto_ahash_type;
	return crypto_grab_spawn(&spawn->base, inst, name, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_grab_ahash);

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

struct crypto_ahash *crypto_clone_ahash(struct crypto_ahash *hash)
{
	struct hash_alg_common *halg = crypto_hash_alg_common(hash);
	struct crypto_tfm *tfm = crypto_ahash_tfm(hash);
	struct crypto_ahash *nhash;
	struct ahash_alg *alg;
	int err;

	if (!crypto_hash_alg_has_setkey(halg)) {
		tfm = crypto_tfm_get(tfm);
		if (IS_ERR(tfm))
			return ERR_CAST(tfm);

		return hash;
	}

	nhash = crypto_clone_tfm(&crypto_ahash_type, tfm);

	if (IS_ERR(nhash))
		return nhash;

	nhash->init = hash->init;
	nhash->update = hash->update;
	nhash->final = hash->final;
	nhash->finup = hash->finup;
	nhash->digest = hash->digest;
	nhash->export = hash->export;
	nhash->import = hash->import;
	nhash->setkey = hash->setkey;
	nhash->reqsize = hash->reqsize;
	nhash->statesize = hash->statesize;

	if (tfm->__crt_alg->cra_type != &crypto_ahash_type)
		return crypto_clone_shash_ops_async(nhash, hash);

	err = -ENOSYS;
	alg = crypto_ahash_alg(hash);
	if (!alg->clone_tfm)
		goto out_free_nhash;

	err = alg->clone_tfm(nhash, hash);
	if (err)
		goto out_free_nhash;

	return nhash;

out_free_nhash:
	crypto_free_ahash(nhash);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(crypto_clone_ahash);

static int ahash_prepare_alg(struct ahash_alg *alg)
{
	struct crypto_alg *base = &alg->halg.base;
	int err;

	if (alg->halg.statesize == 0)
		return -EINVAL;

	err = hash_prepare_alg(&alg->halg);
	if (err)
		return err;

	base->cra_type = &crypto_ahash_type;
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

void crypto_unregister_ahash(struct ahash_alg *alg)
{
	crypto_unregister_alg(&alg->halg.base);
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

	if (WARN_ON(!inst->free))
		return -EINVAL;

	err = ahash_prepare_alg(&inst->alg);
	if (err)
		return err;

	return crypto_register_instance(tmpl, ahash_crypto_instance(inst));
}
EXPORT_SYMBOL_GPL(ahash_register_instance);

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
