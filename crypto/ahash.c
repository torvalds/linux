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
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/string_choices.h>
#include <net/netlink.h>

#include "hash.h"

#define CRYPTO_ALG_TYPE_AHASH_MASK	0x0000000e

struct crypto_hash_walk {
	const char *data;

	unsigned int offset;
	unsigned int flags;

	struct page *pg;
	unsigned int entrylen;

	unsigned int total;
	struct scatterlist *sg;
};

static int ahash_def_finup(struct ahash_request *req);

static inline bool crypto_ahash_block_only(struct crypto_ahash *tfm)
{
	return crypto_ahash_alg(tfm)->halg.base.cra_flags &
	       CRYPTO_AHASH_ALG_BLOCK_ONLY;
}

static inline bool crypto_ahash_final_nonzero(struct crypto_ahash *tfm)
{
	return crypto_ahash_alg(tfm)->halg.base.cra_flags &
	       CRYPTO_AHASH_ALG_FINAL_NONZERO;
}

static inline bool crypto_ahash_need_fallback(struct crypto_ahash *tfm)
{
	return crypto_ahash_alg(tfm)->halg.base.cra_flags &
	       CRYPTO_ALG_NEED_FALLBACK;
}

static inline void ahash_op_done(void *data, int err,
				 int (*finish)(struct ahash_request *, int))
{
	struct ahash_request *areq = data;
	crypto_completion_t compl;

	compl = areq->saved_complete;
	data = areq->saved_data;
	if (err == -EINPROGRESS)
		goto out;

	areq->base.flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	err = finish(areq, err);
	if (err == -EINPROGRESS || err == -EBUSY)
		return;

out:
	compl(data, err);
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
	walk->pg = nth_page(sg_page(walk->sg), (walk->offset >> PAGE_SHIFT));
	walk->offset = offset_in_page(walk->offset);
	walk->entrylen = sg->length;

	if (walk->entrylen > walk->total)
		walk->entrylen = walk->total;
	walk->total -= walk->entrylen;

	return hash_walk_next(walk);
}

static int crypto_hash_walk_first(struct ahash_request *req,
				  struct crypto_hash_walk *walk)
{
	walk->total = req->nbytes;
	walk->entrylen = 0;

	if (!walk->total)
		return 0;

	walk->flags = req->base.flags;

	if (ahash_request_isvirt(req)) {
		walk->data = req->svirt;
		walk->total = 0;
		return req->nbytes;
	}

	walk->sg = req->src;

	return hash_walk_new_entry(walk);
}

static int crypto_hash_walk_done(struct crypto_hash_walk *walk, int err)
{
	if ((walk->flags & CRYPTO_AHASH_REQ_VIRT))
		return err;

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

static inline int crypto_hash_walk_last(struct crypto_hash_walk *walk)
{
	return !(walk->entrylen | walk->total);
}

/*
 * For an ahash tfm that is using an shash algorithm (instead of an ahash
 * algorithm), this returns the underlying shash tfm.
 */
static inline struct crypto_shash *ahash_to_shash(struct crypto_ahash *tfm)
{
	return *(struct crypto_shash **)crypto_ahash_ctx(tfm);
}

static inline struct shash_desc *prepare_shash_desc(struct ahash_request *req,
						    struct crypto_ahash *tfm)
{
	struct shash_desc *desc = ahash_request_ctx(req);

	desc->tfm = ahash_to_shash(tfm);
	return desc;
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

int shash_ahash_digest(struct ahash_request *req, struct shash_desc *desc)
{
	unsigned int nbytes = req->nbytes;
	struct scatterlist *sg;
	unsigned int offset;
	struct page *page;
	const u8 *data;
	int err;

	data = req->svirt;
	if (!nbytes || ahash_request_isvirt(req))
		return crypto_shash_digest(desc, data, nbytes, req->result);

	sg = req->src;
	if (nbytes > sg->length)
		return crypto_shash_init(desc) ?:
		       shash_ahash_finup(req, desc);

	page = sg_page(sg);
	offset = sg->offset;
	data = lowmem_page_address(page) + offset;
	if (!IS_ENABLED(CONFIG_HIGHMEM))
		return crypto_shash_digest(desc, data, nbytes, req->result);

	page = nth_page(page, offset >> PAGE_SHIFT);
	offset = offset_in_page(offset);

	if (nbytes > (unsigned int)PAGE_SIZE - offset)
		return crypto_shash_init(desc) ?:
		       shash_ahash_finup(req, desc);

	data = kmap_local_page(page);
	err = crypto_shash_digest(desc, data + offset, nbytes,
				  req->result);
	kunmap_local(data);
	return err;
}
EXPORT_SYMBOL_GPL(shash_ahash_digest);

static void crypto_exit_ahash_using_shash(struct crypto_tfm *tfm)
{
	struct crypto_shash **ctx = crypto_tfm_ctx(tfm);

	crypto_free_shash(*ctx);
}

static int crypto_init_ahash_using_shash(struct crypto_tfm *tfm)
{
	struct crypto_alg *calg = tfm->__crt_alg;
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

	crt->using_shash = true;
	*ctx = shash;
	tfm->exit = crypto_exit_ahash_using_shash;

	crypto_ahash_set_flags(crt, crypto_shash_get_flags(shash) &
				    CRYPTO_TFM_NEED_KEY);

	return 0;
}

static int ahash_nosetkey(struct crypto_ahash *tfm, const u8 *key,
			  unsigned int keylen)
{
	return -ENOSYS;
}

static void ahash_set_needkey(struct crypto_ahash *tfm, struct ahash_alg *alg)
{
	if (alg->setkey != ahash_nosetkey &&
	    !(alg->halg.base.cra_flags & CRYPTO_ALG_OPTIONAL_KEY))
		crypto_ahash_set_flags(tfm, CRYPTO_TFM_NEED_KEY);
}

int crypto_ahash_setkey(struct crypto_ahash *tfm, const u8 *key,
			unsigned int keylen)
{
	if (likely(tfm->using_shash)) {
		struct crypto_shash *shash = ahash_to_shash(tfm);
		int err;

		err = crypto_shash_setkey(shash, key, keylen);
		if (unlikely(err)) {
			crypto_ahash_set_flags(tfm,
					       crypto_shash_get_flags(shash) &
					       CRYPTO_TFM_NEED_KEY);
			return err;
		}
	} else {
		struct ahash_alg *alg = crypto_ahash_alg(tfm);
		int err;

		err = alg->setkey(tfm, key, keylen);
		if (!err && crypto_ahash_need_fallback(tfm))
			err = crypto_ahash_setkey(crypto_ahash_fb(tfm),
						  key, keylen);
		if (unlikely(err)) {
			ahash_set_needkey(tfm, alg);
			return err;
		}
	}
	crypto_ahash_clear_flags(tfm, CRYPTO_TFM_NEED_KEY);
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_ahash_setkey);

static int ahash_do_req_chain(struct ahash_request *req,
			      int (*const *op)(struct ahash_request *req))
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	int err;

	if (crypto_ahash_req_virt(tfm) || !ahash_request_isvirt(req))
		return (*op)(req);

	if (crypto_ahash_statesize(tfm) > HASH_MAX_STATESIZE)
		return -ENOSYS;

	{
		u8 state[HASH_MAX_STATESIZE];

		if (op == &crypto_ahash_alg(tfm)->digest) {
			ahash_request_set_tfm(req, crypto_ahash_fb(tfm));
			err = crypto_ahash_digest(req);
			goto out_no_state;
		}

		err = crypto_ahash_export(req, state);
		ahash_request_set_tfm(req, crypto_ahash_fb(tfm));
		err = err ?: crypto_ahash_import(req, state);

		if (op == &crypto_ahash_alg(tfm)->finup) {
			err = err ?: crypto_ahash_finup(req);
			goto out_no_state;
		}

		err = err ?:
		      crypto_ahash_update(req) ?:
		      crypto_ahash_export(req, state);

		ahash_request_set_tfm(req, tfm);
		return err ?: crypto_ahash_import(req, state);

out_no_state:
		ahash_request_set_tfm(req, tfm);
		return err;
	}
}

int crypto_ahash_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (likely(tfm->using_shash))
		return crypto_shash_init(prepare_shash_desc(req, tfm));
	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;
	if (ahash_req_on_stack(req) && ahash_is_async(tfm))
		return -EAGAIN;
	if (crypto_ahash_block_only(tfm)) {
		u8 *buf = ahash_request_ctx(req);

		buf += crypto_ahash_reqsize(tfm) - 1;
		*buf = 0;
	}
	return crypto_ahash_alg(tfm)->init(req);
}
EXPORT_SYMBOL_GPL(crypto_ahash_init);

static void ahash_save_req(struct ahash_request *req, crypto_completion_t cplt)
{
	req->saved_complete = req->base.complete;
	req->saved_data = req->base.data;
	req->base.complete = cplt;
	req->base.data = req;
}

static void ahash_restore_req(struct ahash_request *req)
{
	req->base.complete = req->saved_complete;
	req->base.data = req->saved_data;
}

static int ahash_update_finish(struct ahash_request *req, int err)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	bool nonzero = crypto_ahash_final_nonzero(tfm);
	int bs = crypto_ahash_blocksize(tfm);
	u8 *blenp = ahash_request_ctx(req);
	int blen;
	u8 *buf;

	blenp += crypto_ahash_reqsize(tfm) - 1;
	blen = *blenp;
	buf = blenp - bs;

	if (blen) {
		req->src = req->sg_head + 1;
		if (sg_is_chain(req->src))
			req->src = sg_chain_ptr(req->src);
	}

	req->nbytes += nonzero - blen;

	blen = err < 0 ? 0 : err + nonzero;
	if (ahash_request_isvirt(req))
		memcpy(buf, req->svirt + req->nbytes - blen, blen);
	else
		memcpy_from_sglist(buf, req->src, req->nbytes - blen, blen);
	*blenp = blen;

	ahash_restore_req(req);

	return err;
}

static void ahash_update_done(void *data, int err)
{
	ahash_op_done(data, err, ahash_update_finish);
}

int crypto_ahash_update(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	bool nonzero = crypto_ahash_final_nonzero(tfm);
	int bs = crypto_ahash_blocksize(tfm);
	u8 *blenp = ahash_request_ctx(req);
	int blen, err;
	u8 *buf;

	if (likely(tfm->using_shash))
		return shash_ahash_update(req, ahash_request_ctx(req));
	if (ahash_req_on_stack(req) && ahash_is_async(tfm))
		return -EAGAIN;
	if (!crypto_ahash_block_only(tfm))
		return ahash_do_req_chain(req, &crypto_ahash_alg(tfm)->update);

	blenp += crypto_ahash_reqsize(tfm) - 1;
	blen = *blenp;
	buf = blenp - bs;

	if (blen + req->nbytes < bs + nonzero) {
		if (ahash_request_isvirt(req))
			memcpy(buf + blen, req->svirt, req->nbytes);
		else
			memcpy_from_sglist(buf + blen, req->src, 0,
					   req->nbytes);

		*blenp += req->nbytes;
		return 0;
	}

	if (blen) {
		memset(req->sg_head, 0, sizeof(req->sg_head[0]));
		sg_set_buf(req->sg_head, buf, blen);
		if (req->src != req->sg_head + 1)
			sg_chain(req->sg_head, 2, req->src);
		req->src = req->sg_head;
		req->nbytes += blen;
	}
	req->nbytes -= nonzero;

	ahash_save_req(req, ahash_update_done);

	err = ahash_do_req_chain(req, &crypto_ahash_alg(tfm)->update);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

	return ahash_update_finish(req, err);
}
EXPORT_SYMBOL_GPL(crypto_ahash_update);

static int ahash_finup_finish(struct ahash_request *req, int err)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	u8 *blenp = ahash_request_ctx(req);
	int blen;

	blenp += crypto_ahash_reqsize(tfm) - 1;
	blen = *blenp;

	if (blen) {
		if (sg_is_last(req->src))
			req->src = NULL;
		else {
			req->src = req->sg_head + 1;
			if (sg_is_chain(req->src))
				req->src = sg_chain_ptr(req->src);
		}
		req->nbytes -= blen;
	}

	ahash_restore_req(req);

	return err;
}

static void ahash_finup_done(void *data, int err)
{
	ahash_op_done(data, err, ahash_finup_finish);
}

int crypto_ahash_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	int bs = crypto_ahash_blocksize(tfm);
	u8 *blenp = ahash_request_ctx(req);
	int blen, err;
	u8 *buf;

	if (likely(tfm->using_shash))
		return shash_ahash_finup(req, ahash_request_ctx(req));
	if (ahash_req_on_stack(req) && ahash_is_async(tfm))
		return -EAGAIN;
	if (!crypto_ahash_alg(tfm)->finup)
		return ahash_def_finup(req);
	if (!crypto_ahash_block_only(tfm))
		return ahash_do_req_chain(req, &crypto_ahash_alg(tfm)->finup);

	blenp += crypto_ahash_reqsize(tfm) - 1;
	blen = *blenp;
	buf = blenp - bs;

	if (blen) {
		memset(req->sg_head, 0, sizeof(req->sg_head[0]));
		sg_set_buf(req->sg_head, buf, blen);
		if (!req->src)
			sg_mark_end(req->sg_head);
		else if (req->src != req->sg_head + 1)
			sg_chain(req->sg_head, 2, req->src);
		req->src = req->sg_head;
		req->nbytes += blen;
	}

	ahash_save_req(req, ahash_finup_done);

	err = ahash_do_req_chain(req, &crypto_ahash_alg(tfm)->finup);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

	return ahash_finup_finish(req, err);
}
EXPORT_SYMBOL_GPL(crypto_ahash_finup);

int crypto_ahash_digest(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (likely(tfm->using_shash))
		return shash_ahash_digest(req, prepare_shash_desc(req, tfm));
	if (ahash_req_on_stack(req) && ahash_is_async(tfm))
		return -EAGAIN;
	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;
	return ahash_do_req_chain(req, &crypto_ahash_alg(tfm)->digest);
}
EXPORT_SYMBOL_GPL(crypto_ahash_digest);

static void ahash_def_finup_done2(void *data, int err)
{
	struct ahash_request *areq = data;

	if (err == -EINPROGRESS)
		return;

	ahash_restore_req(areq);
	ahash_request_complete(areq, err);
}

static int ahash_def_finup_finish1(struct ahash_request *req, int err)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (err)
		goto out;

	req->base.complete = ahash_def_finup_done2;

	err = crypto_ahash_alg(tfm)->final(req);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

out:
	ahash_restore_req(req);
	return err;
}

static void ahash_def_finup_done1(void *data, int err)
{
	ahash_op_done(data, err, ahash_def_finup_finish1);
}

static int ahash_def_finup(struct ahash_request *req)
{
	int err;

	ahash_save_req(req, ahash_def_finup_done1);

	err = crypto_ahash_update(req);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

	return ahash_def_finup_finish1(req, err);
}

int crypto_ahash_export_core(struct ahash_request *req, void *out)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (likely(tfm->using_shash))
		return crypto_shash_export_core(ahash_request_ctx(req), out);
	return crypto_ahash_alg(tfm)->export_core(req, out);
}
EXPORT_SYMBOL_GPL(crypto_ahash_export_core);

int crypto_ahash_export(struct ahash_request *req, void *out)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (likely(tfm->using_shash))
		return crypto_shash_export(ahash_request_ctx(req), out);
	if (crypto_ahash_block_only(tfm)) {
		unsigned int plen = crypto_ahash_blocksize(tfm) + 1;
		unsigned int reqsize = crypto_ahash_reqsize(tfm);
		unsigned int ss = crypto_ahash_statesize(tfm);
		u8 *buf = ahash_request_ctx(req);

		memcpy(out + ss - plen, buf + reqsize - plen, plen);
	}
	return crypto_ahash_alg(tfm)->export(req, out);
}
EXPORT_SYMBOL_GPL(crypto_ahash_export);

int crypto_ahash_import_core(struct ahash_request *req, const void *in)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (likely(tfm->using_shash))
		return crypto_shash_import_core(prepare_shash_desc(req, tfm),
						in);
	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;
	return crypto_ahash_alg(tfm)->import_core(req, in);
}
EXPORT_SYMBOL_GPL(crypto_ahash_import_core);

int crypto_ahash_import(struct ahash_request *req, const void *in)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (likely(tfm->using_shash))
		return crypto_shash_import(prepare_shash_desc(req, tfm), in);
	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;
	if (crypto_ahash_block_only(tfm)) {
		unsigned int reqsize = crypto_ahash_reqsize(tfm);
		u8 *buf = ahash_request_ctx(req);

		buf[reqsize - 1] = 0;
	}
	return crypto_ahash_alg(tfm)->import(req, in);
}
EXPORT_SYMBOL_GPL(crypto_ahash_import);

static void crypto_ahash_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_ahash *hash = __crypto_ahash_cast(tfm);
	struct ahash_alg *alg = crypto_ahash_alg(hash);

	if (alg->exit_tfm)
		alg->exit_tfm(hash);
	else if (tfm->__crt_alg->cra_exit)
		tfm->__crt_alg->cra_exit(tfm);

	if (crypto_ahash_need_fallback(hash))
		crypto_free_ahash(crypto_ahash_fb(hash));
}

static int crypto_ahash_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_ahash *hash = __crypto_ahash_cast(tfm);
	struct ahash_alg *alg = crypto_ahash_alg(hash);
	struct crypto_ahash *fb = NULL;
	int err;

	crypto_ahash_set_statesize(hash, alg->halg.statesize);
	crypto_ahash_set_reqsize(hash, crypto_tfm_alg_reqsize(tfm));

	if (tfm->__crt_alg->cra_type == &crypto_shash_type)
		return crypto_init_ahash_using_shash(tfm);

	if (crypto_ahash_need_fallback(hash)) {
		fb = crypto_alloc_ahash(crypto_ahash_alg_name(hash),
					CRYPTO_ALG_REQ_VIRT,
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_REQ_VIRT |
					CRYPTO_AHASH_ALG_NO_EXPORT_CORE);
		if (IS_ERR(fb))
			return PTR_ERR(fb);

		tfm->fb = crypto_ahash_tfm(fb);
	}

	ahash_set_needkey(hash, alg);

	tfm->exit = crypto_ahash_exit_tfm;

	if (alg->init_tfm)
		err = alg->init_tfm(hash);
	else if (tfm->__crt_alg->cra_init)
		err = tfm->__crt_alg->cra_init(tfm);
	else
		return 0;

	if (err)
		goto out_free_sync_hash;

	if (!ahash_is_async(hash) && crypto_ahash_reqsize(hash) >
				     MAX_SYNC_HASH_REQSIZE)
		goto out_exit_tfm;

	BUILD_BUG_ON(HASH_MAX_DESCSIZE > MAX_SYNC_HASH_REQSIZE);
	if (crypto_ahash_reqsize(hash) < HASH_MAX_DESCSIZE)
		crypto_ahash_set_reqsize(hash, HASH_MAX_DESCSIZE);

	return 0;

out_exit_tfm:
	if (alg->exit_tfm)
		alg->exit_tfm(hash);
	else if (tfm->__crt_alg->cra_exit)
		tfm->__crt_alg->cra_exit(tfm);
	err = -EINVAL;
out_free_sync_hash:
	crypto_free_ahash(fb);
	return err;
}

static unsigned int crypto_ahash_extsize(struct crypto_alg *alg)
{
	if (alg->cra_type == &crypto_shash_type)
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
	seq_printf(m, "async        : %s\n",
		   str_yes_no(alg->cra_flags & CRYPTO_ALG_ASYNC));
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "digestsize   : %u\n",
		   __crypto_hash_alg_common(alg)->digestsize);
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
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_AHASH_MASK,
	.type = CRYPTO_ALG_TYPE_AHASH,
	.tfmsize = offsetof(struct crypto_ahash, base),
	.algsize = offsetof(struct ahash_alg, halg.base),
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

bool crypto_hash_alg_has_setkey(struct hash_alg_common *halg)
{
	struct crypto_alg *alg = &halg->base;

	if (alg->cra_type == &crypto_shash_type)
		return crypto_shash_alg_has_setkey(__crypto_shash_alg(alg));

	return __crypto_ahash_alg(alg)->setkey != ahash_nosetkey;
}
EXPORT_SYMBOL_GPL(crypto_hash_alg_has_setkey);

struct crypto_ahash *crypto_clone_ahash(struct crypto_ahash *hash)
{
	struct hash_alg_common *halg = crypto_hash_alg_common(hash);
	struct crypto_tfm *tfm = crypto_ahash_tfm(hash);
	struct crypto_ahash *fb = NULL;
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

	nhash->reqsize = hash->reqsize;
	nhash->statesize = hash->statesize;

	if (likely(hash->using_shash)) {
		struct crypto_shash **nctx = crypto_ahash_ctx(nhash);
		struct crypto_shash *shash;

		shash = crypto_clone_shash(ahash_to_shash(hash));
		if (IS_ERR(shash)) {
			err = PTR_ERR(shash);
			goto out_free_nhash;
		}
		crypto_ahash_tfm(nhash)->exit = crypto_exit_ahash_using_shash;
		nhash->using_shash = true;
		*nctx = shash;
		return nhash;
	}

	if (crypto_ahash_need_fallback(hash)) {
		fb = crypto_clone_ahash(crypto_ahash_fb(hash));
		err = PTR_ERR(fb);
		if (IS_ERR(fb))
			goto out_free_nhash;

		crypto_ahash_tfm(nhash)->fb = crypto_ahash_tfm(fb);
	}

	err = -ENOSYS;
	alg = crypto_ahash_alg(hash);
	if (!alg->clone_tfm)
		goto out_free_fb;

	err = alg->clone_tfm(nhash, hash);
	if (err)
		goto out_free_fb;

	crypto_ahash_tfm(nhash)->exit = crypto_ahash_exit_tfm;

	return nhash;

out_free_fb:
	crypto_free_ahash(fb);
out_free_nhash:
	crypto_free_ahash(nhash);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(crypto_clone_ahash);

static int ahash_default_export_core(struct ahash_request *req, void *out)
{
	return -ENOSYS;
}

static int ahash_default_import_core(struct ahash_request *req, const void *in)
{
	return -ENOSYS;
}

static int ahash_prepare_alg(struct ahash_alg *alg)
{
	struct crypto_alg *base = &alg->halg.base;
	int err;

	if (alg->halg.statesize == 0)
		return -EINVAL;

	if (base->cra_reqsize && base->cra_reqsize < alg->halg.statesize)
		return -EINVAL;

	if (!(base->cra_flags & CRYPTO_ALG_ASYNC) &&
	    base->cra_reqsize > MAX_SYNC_HASH_REQSIZE)
		return -EINVAL;

	err = hash_prepare_alg(&alg->halg);
	if (err)
		return err;

	base->cra_type = &crypto_ahash_type;
	base->cra_flags |= CRYPTO_ALG_TYPE_AHASH;

	if ((base->cra_flags ^ CRYPTO_ALG_REQ_VIRT) &
	    (CRYPTO_ALG_ASYNC | CRYPTO_ALG_REQ_VIRT))
		base->cra_flags |= CRYPTO_ALG_NEED_FALLBACK;

	if (!alg->setkey)
		alg->setkey = ahash_nosetkey;

	if (base->cra_flags & CRYPTO_AHASH_ALG_BLOCK_ONLY) {
		BUILD_BUG_ON(MAX_ALGAPI_BLOCKSIZE >= 256);
		if (!alg->finup)
			return -EINVAL;

		base->cra_reqsize += base->cra_blocksize + 1;
		alg->halg.statesize += base->cra_blocksize + 1;
		alg->export_core = alg->export;
		alg->import_core = alg->import;
	} else if (!alg->export_core || !alg->import_core) {
		alg->export_core = ahash_default_export_core;
		alg->import_core = ahash_default_import_core;
		base->cra_flags |= CRYPTO_AHASH_ALG_NO_EXPORT_CORE;
	}

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

void ahash_request_free(struct ahash_request *req)
{
	if (unlikely(!req))
		return;

	if (!ahash_req_on_stack(req)) {
		kfree(req);
		return;
	}

	ahash_request_zero(req);
}
EXPORT_SYMBOL_GPL(ahash_request_free);

int crypto_hash_digest(struct crypto_ahash *tfm, const u8 *data,
		       unsigned int len, u8 *out)
{
	HASH_REQUEST_ON_STACK(req, crypto_ahash_fb(tfm));
	int err;

	ahash_request_set_callback(req, 0, NULL, NULL);
	ahash_request_set_virt(req, data, out, len);
	err = crypto_ahash_digest(req);

	ahash_request_zero(req);

	return err;
}
EXPORT_SYMBOL_GPL(crypto_hash_digest);

void ahash_free_singlespawn_instance(struct ahash_instance *inst)
{
	crypto_drop_spawn(ahash_instance_ctx(inst));
	kfree(inst);
}
EXPORT_SYMBOL_GPL(ahash_free_singlespawn_instance);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Asynchronous cryptographic hash type");
