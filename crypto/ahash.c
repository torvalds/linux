// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Asynchronous Cryptographic Hash operations.
 *
 * This is the implementation of the ahash (asynchronous hash) API.  It differs
 * from shash (synchronous hash) in that ahash supports asynchronous operations,
 * and it hashes data from scatterlists instead of virtually addressed buffers.
 *
 * The ahash API provides access to both ahash and shash algorithms.  The shash
 * API only provides access to shash algorithms.
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

#define CRYPTO_ALG_TYPE_AHASH_MASK	0x0000000e

static const struct crypto_type crypto_ahash_type;

static int shash_async_setkey(struct crypto_ahash *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct crypto_shash **ctx = crypto_ahash_ctx(tfm);

	return crypto_shash_setkey(*ctx, key, keylen);
}

static int shash_async_init(struct ahash_request *req)
{
	struct crypto_shash **ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct shash_desc *desc = ahash_request_ctx(req);

	desc->tfm = *ctx;

	return crypto_shash_init(desc);
}

int shash_ahash_update(struct ahash_request *req, struct shash_desc *desc)
{
	struct crypto_hash_walk walk;
	int nbytes;

	for (nbytes = crypto_hash_walk_first(req, &walk); nbytes > 0;
	     nbytes = crypto_hash_walk_done(&walk, nbytes))
		nbytes = crypto_shash_update(desc, walk.data, nbytes);

	return nbytes;
}
EXPORT_SYMBOL_GPL(shash_ahash_update);

static int shash_async_update(struct ahash_request *req)
{
	return shash_ahash_update(req, ahash_request_ctx(req));
}

static int shash_async_final(struct ahash_request *req)
{
	return crypto_shash_final(ahash_request_ctx(req), req->result);
}

int shash_ahash_finup(struct ahash_request *req, struct shash_desc *desc)
{
	struct crypto_hash_walk walk;
	int nbytes;

	nbytes = crypto_hash_walk_first(req, &walk);
	if (!nbytes)
		return crypto_shash_final(desc, req->result);

	do {
		nbytes = crypto_hash_walk_last(&walk) ?
			 crypto_shash_finup(desc, walk.data, nbytes,
					    req->result) :
			 crypto_shash_update(desc, walk.data, nbytes);
		nbytes = crypto_hash_walk_done(&walk, nbytes);
	} while (nbytes > 0);

	return nbytes;
}
EXPORT_SYMBOL_GPL(shash_ahash_finup);

static int shash_async_finup(struct ahash_request *req)
{
	struct crypto_shash **ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct shash_desc *desc = ahash_request_ctx(req);

	desc->tfm = *ctx;

	return shash_ahash_finup(req, desc);
}

int shash_ahash_digest(struct ahash_request *req, struct shash_desc *desc)
{
	unsigned int nbytes = req->nbytes;
	struct scatterlist *sg;
	unsigned int offset;
	int err;

	if (nbytes &&
	    (sg = req->src, offset = sg->offset,
	     nbytes <= min(sg->length, ((unsigned int)(PAGE_SIZE)) - offset))) {
		void *data;

		data = kmap_local_page(sg_page(sg));
		err = crypto_shash_digest(desc, data + offset, nbytes,
					  req->result);
		kunmap_local(data);
	} else
		err = crypto_shash_init(desc) ?:
		      shash_ahash_finup(req, desc);

	return err;
}
EXPORT_SYMBOL_GPL(shash_ahash_digest);

static int shash_async_digest(struct ahash_request *req)
{
	struct crypto_shash **ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct shash_desc *desc = ahash_request_ctx(req);

	desc->tfm = *ctx;

	return shash_ahash_digest(req, desc);
}

static int shash_async_export(struct ahash_request *req, void *out)
{
	return crypto_shash_export(ahash_request_ctx(req), out);
}

static int shash_async_import(struct ahash_request *req, const void *in)
{
	struct crypto_shash **ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct shash_desc *desc = ahash_request_ctx(req);

	desc->tfm = *ctx;

	return crypto_shash_import(desc, in);
}

static void crypto_exit_shash_ops_async(struct crypto_tfm *tfm)
{
	struct crypto_shash **ctx = crypto_tfm_ctx(tfm);

	crypto_free_shash(*ctx);
}

static int crypto_init_shash_ops_async(struct crypto_tfm *tfm)
{
	struct crypto_alg *calg = tfm->__crt_alg;
	struct shash_alg *alg = __crypto_shash_alg(calg);
	struct crypto_ahash *crt = __crypto_ahash_cast(tfm);
	struct crypto_shash **ctx = crypto_tfm_ctx(tfm);
	struct crypto_shash *shash;

	if (!crypto_mod_get(calg))
		return -EAGAIN;

	shash = crypto_create_tfm(calg, &crypto_shash_type);
	if (IS_ERR(shash)) {
		crypto_mod_put(calg);
		return PTR_ERR(shash);
	}

	*ctx = shash;
	tfm->exit = crypto_exit_shash_ops_async;

	crt->init = shash_async_init;
	crt->update = shash_async_update;
	crt->final = shash_async_final;
	crt->finup = shash_async_finup;
	crt->digest = shash_async_digest;
	if (crypto_shash_alg_has_setkey(alg))
		crt->setkey = shash_async_setkey;

	crypto_ahash_set_flags(crt, crypto_shash_get_flags(shash) &
				    CRYPTO_TFM_NEED_KEY);

	crt->export = shash_async_export;
	crt->import = shash_async_import;

	crt->reqsize = sizeof(struct shash_desc) + crypto_shash_descsize(shash);

	return 0;
}

static struct crypto_ahash *
crypto_clone_shash_ops_async(struct crypto_ahash *nhash,
			     struct crypto_ahash *hash)
{
	struct crypto_shash **nctx = crypto_ahash_ctx(nhash);
	struct crypto_shash **ctx = crypto_ahash_ctx(hash);
	struct crypto_shash *shash;

	shash = crypto_clone_shash(*ctx);
	if (IS_ERR(shash)) {
		crypto_free_ahash(nhash);
		return ERR_CAST(shash);
	}

	*nctx = shash;

	return nhash;
}

static int hash_walk_next(struct crypto_hash_walk *walk)
{
	unsigned int offset = walk->offset;
	unsigned int nbytes = min(walk->entrylen,
				  ((unsigned int)(PAGE_SIZE)) - offset);

	walk->data = kmap_local_page(walk->pg);
	walk->data += offset;
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
	walk->data -= walk->offset;

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

	walk->sg = req->src;
	walk->flags = req->base.flags;

	return hash_walk_new_entry(walk);
}
EXPORT_SYMBOL_GPL(crypto_hash_walk_first);

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
	int err = tfm->setkey(tfm, key, keylen);

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

	flags = ahash_request_flags(req);
	gfp = (flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?  GFP_KERNEL : GFP_ATOMIC;
	subreq = kmalloc(subreq_size, gfp);
	if (!subreq)
		return -ENOMEM;

	ahash_request_set_tfm(subreq, tfm);
	ahash_request_set_callback(subreq, flags, cplt, req);

	result = (u8 *)(subreq + 1) + reqsize;

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

int crypto_ahash_final(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct hash_alg_common *alg = crypto_hash_alg_common(tfm);

	if (IS_ENABLED(CONFIG_CRYPTO_STATS))
		atomic64_inc(&hash_get_stat(alg)->hash_cnt);

	return crypto_hash_errstat(alg, tfm->final(req));
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

	return crypto_hash_errstat(alg, tfm->finup(req));
}
EXPORT_SYMBOL_GPL(crypto_ahash_finup);

int crypto_ahash_digest(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct hash_alg_common *alg = crypto_hash_alg_common(tfm);
	int err;

	if (IS_ENABLED(CONFIG_CRYPTO_STATS)) {
		struct crypto_istat_hash *istat = hash_get_stat(alg);

		atomic64_inc(&istat->hash_cnt);
		atomic64_add(req->nbytes, &istat->hash_tlen);
	}

	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		err = -ENOKEY;
	else
		err = tfm->digest(req);

	return crypto_hash_errstat(alg, err);
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
