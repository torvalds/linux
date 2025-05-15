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
#include <linux/sched.h>
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

struct ahash_save_req_state {
	struct list_head head;
	struct ahash_request *req0;
	struct ahash_request *cur;
	int (*op)(struct ahash_request *req);
	crypto_completion_t compl;
	void *data;
	struct scatterlist sg;
	const u8 *src;
	u8 *page;
	unsigned int offset;
	unsigned int nbytes;
};

static void ahash_reqchain_done(void *data, int err);
static int ahash_save_req(struct ahash_request *req, crypto_completion_t cplt);
static void ahash_restore_req(struct ahash_request *req);
static void ahash_def_finup_done1(void *data, int err);
static int ahash_def_finup_finish1(struct ahash_request *req, int err);
static int ahash_def_finup(struct ahash_request *req);

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
	crt->reqsize = sizeof(struct shash_desc) + crypto_shash_descsize(shash);

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
		if (unlikely(err)) {
			ahash_set_needkey(tfm, alg);
			return err;
		}
	}
	crypto_ahash_clear_flags(tfm, CRYPTO_TFM_NEED_KEY);
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_ahash_setkey);

static bool ahash_request_hasvirt(struct ahash_request *req)
{
	return ahash_request_isvirt(req);
}

static int ahash_reqchain_virt(struct ahash_save_req_state *state,
			       int err, u32 mask)
{
	struct ahash_request *req = state->cur;

	for (;;) {
		unsigned len = state->nbytes;

		req->base.err = err;

		if (!state->offset)
			break;

		if (state->offset == len || err) {
			u8 *result = req->result;

			ahash_request_set_virt(req, state->src, result, len);
			state->offset = 0;
			break;
		}

		len -= state->offset;

		len = min(PAGE_SIZE, len);
		memcpy(state->page, state->src + state->offset, len);
		state->offset += len;
		req->nbytes = len;

		err = state->op(req);
		if (err == -EINPROGRESS) {
			if (!list_empty(&state->head) ||
			    state->offset < state->nbytes)
				err = -EBUSY;
			break;
		}

		if (err == -EBUSY)
			break;
	}

	return err;
}

static int ahash_reqchain_finish(struct ahash_request *req0,
				 struct ahash_save_req_state *state,
				 int err, u32 mask)
{
	struct ahash_request *req = state->cur;
	struct crypto_ahash *tfm;
	struct ahash_request *n;
	bool update;
	u8 *page;

	err = ahash_reqchain_virt(state, err, mask);
	if (err == -EINPROGRESS || err == -EBUSY)
		goto out;

	if (req != req0)
		list_add_tail(&req->base.list, &req0->base.list);

	tfm = crypto_ahash_reqtfm(req);
	update = state->op == crypto_ahash_alg(tfm)->update;

	list_for_each_entry_safe(req, n, &state->head, base.list) {
		list_del_init(&req->base.list);

		req->base.flags &= mask;
		req->base.complete = ahash_reqchain_done;
		req->base.data = state;
		state->cur = req;

		if (update && ahash_request_isvirt(req) && req->nbytes) {
			unsigned len = req->nbytes;
			u8 *result = req->result;

			state->src = req->svirt;
			state->nbytes = len;

			len = min(PAGE_SIZE, len);

			memcpy(state->page, req->svirt, len);
			state->offset = len;

			ahash_request_set_crypt(req, &state->sg, result, len);
		}

		err = state->op(req);

		if (err == -EINPROGRESS) {
			if (!list_empty(&state->head) ||
			    state->offset < state->nbytes)
				err = -EBUSY;
			goto out;
		}

		if (err == -EBUSY)
			goto out;

		err = ahash_reqchain_virt(state, err, mask);
		if (err == -EINPROGRESS || err == -EBUSY)
			goto out;

		list_add_tail(&req->base.list, &req0->base.list);
	}

	page = state->page;
	if (page) {
		memset(page, 0, PAGE_SIZE);
		free_page((unsigned long)page);
	}
	ahash_restore_req(req0);

out:
	return err;
}

static void ahash_reqchain_done(void *data, int err)
{
	struct ahash_save_req_state *state = data;
	crypto_completion_t compl = state->compl;

	data = state->data;

	if (err == -EINPROGRESS) {
		if (!list_empty(&state->head) || state->offset < state->nbytes)
			return;
		goto notify;
	}

	err = ahash_reqchain_finish(state->req0, state, err,
				    CRYPTO_TFM_REQ_MAY_BACKLOG);
	if (err == -EBUSY)
		return;

notify:
	compl(data, err);
}

static int ahash_do_req_chain(struct ahash_request *req,
			      int (*op)(struct ahash_request *req))
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	bool update = op == crypto_ahash_alg(tfm)->update;
	struct ahash_save_req_state *state;
	struct ahash_save_req_state state0;
	u8 *page = NULL;
	int err;

	if (crypto_ahash_req_chain(tfm) ||
	    (!ahash_request_chained(req) &&
	     (!update || !ahash_request_isvirt(req))))
		return op(req);

	if (update && ahash_request_hasvirt(req)) {
		gfp_t gfp;
		u32 flags;

		flags = ahash_request_flags(req);
		gfp = (flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		      GFP_KERNEL : GFP_ATOMIC;
		page = (void *)__get_free_page(gfp);
		err = -ENOMEM;
		if (!page)
			goto out_set_chain;
	}

	state = &state0;
	if (ahash_is_async(tfm)) {
		err = ahash_save_req(req, ahash_reqchain_done);
		if (err)
			goto out_free_page;

		state = req->base.data;
	}

	state->op = op;
	state->cur = req;
	state->page = page;
	state->offset = 0;
	state->nbytes = 0;
	INIT_LIST_HEAD(&state->head);

	if (page)
		sg_init_one(&state->sg, page, PAGE_SIZE);

	if (update && ahash_request_isvirt(req) && req->nbytes) {
		unsigned len = req->nbytes;
		u8 *result = req->result;

		state->src = req->svirt;
		state->nbytes = len;

		len = min(PAGE_SIZE, len);

		memcpy(page, req->svirt, len);
		state->offset = len;

		ahash_request_set_crypt(req, &state->sg, result, len);
	}

	err = op(req);
	if (err == -EBUSY || err == -EINPROGRESS)
		return -EBUSY;

	return ahash_reqchain_finish(req, state, err, ~0);

out_free_page:
	free_page((unsigned long)page);

out_set_chain:
	req->base.err = err;
	return err;
}

int crypto_ahash_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (likely(tfm->using_shash)) {
		int err;

		err = crypto_shash_init(prepare_shash_desc(req, tfm));
		req->base.err = err;
		return err;
	}

	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	return ahash_do_req_chain(req, crypto_ahash_alg(tfm)->init);
}
EXPORT_SYMBOL_GPL(crypto_ahash_init);

static int ahash_save_req(struct ahash_request *req, crypto_completion_t cplt)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ahash_save_req_state *state;
	gfp_t gfp;
	u32 flags;

	if (!ahash_is_async(tfm))
		return 0;

	flags = ahash_request_flags(req);
	gfp = (flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?  GFP_KERNEL : GFP_ATOMIC;
	state = kmalloc(sizeof(*state), gfp);
	if (!state)
		return -ENOMEM;

	state->compl = req->base.complete;
	state->data = req->base.data;
	req->base.complete = cplt;
	req->base.data = state;
	state->req0 = req;

	return 0;
}

static void ahash_restore_req(struct ahash_request *req)
{
	struct ahash_save_req_state *state;
	struct crypto_ahash *tfm;

	tfm = crypto_ahash_reqtfm(req);
	if (!ahash_is_async(tfm))
		return;

	state = req->base.data;

	req->base.complete = state->compl;
	req->base.data = state->data;
	kfree(state);
}

int crypto_ahash_update(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (likely(tfm->using_shash)) {
		int err;

		err = shash_ahash_update(req, ahash_request_ctx(req));
		req->base.err = err;
		return err;
	}

	return ahash_do_req_chain(req, crypto_ahash_alg(tfm)->update);
}
EXPORT_SYMBOL_GPL(crypto_ahash_update);

int crypto_ahash_final(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (likely(tfm->using_shash)) {
		int err;

		err = crypto_shash_final(ahash_request_ctx(req), req->result);
		req->base.err = err;
		return err;
	}

	return ahash_do_req_chain(req, crypto_ahash_alg(tfm)->final);
}
EXPORT_SYMBOL_GPL(crypto_ahash_final);

int crypto_ahash_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (likely(tfm->using_shash)) {
		int err;

		err = shash_ahash_finup(req, ahash_request_ctx(req));
		req->base.err = err;
		return err;
	}

	if (!crypto_ahash_alg(tfm)->finup ||
	    (!crypto_ahash_req_chain(tfm) && ahash_request_hasvirt(req)))
		return ahash_def_finup(req);

	return ahash_do_req_chain(req, crypto_ahash_alg(tfm)->finup);
}
EXPORT_SYMBOL_GPL(crypto_ahash_finup);

static int ahash_def_digest_finish(struct ahash_request *req, int err)
{
	struct crypto_ahash *tfm;

	if (err)
		goto out;

	tfm = crypto_ahash_reqtfm(req);
	if (ahash_is_async(tfm))
		req->base.complete = ahash_def_finup_done1;

	err = crypto_ahash_update(req);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

	return ahash_def_finup_finish1(req, err);

out:
	ahash_restore_req(req);
	return err;
}

static void ahash_def_digest_done(void *data, int err)
{
	struct ahash_save_req_state *state0 = data;
	struct ahash_save_req_state state;
	struct ahash_request *areq;

	state = *state0;
	areq = state.req0;
	if (err == -EINPROGRESS)
		goto out;

	areq->base.flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	err = ahash_def_digest_finish(areq, err);
	if (err == -EINPROGRESS || err == -EBUSY)
		return;

out:
	state.compl(state.data, err);
}

static int ahash_def_digest(struct ahash_request *req)
{
	int err;

	err = ahash_save_req(req, ahash_def_digest_done);
	if (err)
		return err;

	err = crypto_ahash_init(req);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

	return ahash_def_digest_finish(req, err);
}

int crypto_ahash_digest(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (likely(tfm->using_shash)) {
		int err;

		err = shash_ahash_digest(req, prepare_shash_desc(req, tfm));
		req->base.err = err;
		return err;
	}

	if (!crypto_ahash_req_chain(tfm) && ahash_request_hasvirt(req))
		return ahash_def_digest(req);

	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	return ahash_do_req_chain(req, crypto_ahash_alg(tfm)->digest);
}
EXPORT_SYMBOL_GPL(crypto_ahash_digest);

static void ahash_def_finup_done2(void *data, int err)
{
	struct ahash_save_req_state *state = data;
	struct ahash_request *areq = state->req0;

	if (err == -EINPROGRESS)
		return;

	ahash_restore_req(areq);
	ahash_request_complete(areq, err);
}

static int ahash_def_finup_finish1(struct ahash_request *req, int err)
{
	struct crypto_ahash *tfm;

	if (err)
		goto out;

	tfm = crypto_ahash_reqtfm(req);
	if (ahash_is_async(tfm))
		req->base.complete = ahash_def_finup_done2;

	err = crypto_ahash_final(req);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

out:
	ahash_restore_req(req);
	return err;
}

static void ahash_def_finup_done1(void *data, int err)
{
	struct ahash_save_req_state *state0 = data;
	struct ahash_save_req_state state;
	struct ahash_request *areq;

	state = *state0;
	areq = state.req0;
	if (err == -EINPROGRESS)
		goto out;

	areq->base.flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	err = ahash_def_finup_finish1(areq, err);
	if (err == -EINPROGRESS || err == -EBUSY)
		return;

out:
	state.compl(state.data, err);
}

static int ahash_def_finup(struct ahash_request *req)
{
	int err;

	err = ahash_save_req(req, ahash_def_finup_done1);
	if (err)
		return err;

	err = crypto_ahash_update(req);
	if (err == -EINPROGRESS || err == -EBUSY)
		return err;

	return ahash_def_finup_finish1(req, err);
}

int crypto_ahash_export(struct ahash_request *req, void *out)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (likely(tfm->using_shash))
		return crypto_shash_export(ahash_request_ctx(req), out);
	return crypto_ahash_alg(tfm)->export(req, out);
}
EXPORT_SYMBOL_GPL(crypto_ahash_export);

int crypto_ahash_import(struct ahash_request *req, const void *in)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (likely(tfm->using_shash))
		return crypto_shash_import(prepare_shash_desc(req, tfm), in);
	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;
	return crypto_ahash_alg(tfm)->import(req, in);
}
EXPORT_SYMBOL_GPL(crypto_ahash_import);

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

	crypto_ahash_set_statesize(hash, alg->halg.statesize);
	crypto_ahash_set_reqsize(hash, alg->reqsize);

	if (tfm->__crt_alg->cra_type == &crypto_shash_type)
		return crypto_init_ahash_using_shash(tfm);

	ahash_set_needkey(hash, alg);

	if (alg->exit_tfm)
		tfm->exit = crypto_ahash_exit_tfm;

	return alg->init_tfm ? alg->init_tfm(hash) : 0;
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

static bool crypto_hash_alg_has_setkey(struct hash_alg_common *halg)
{
	struct crypto_alg *alg = &halg->base;

	if (alg->cra_type == &crypto_shash_type)
		return crypto_shash_alg_has_setkey(__crypto_shash_alg(alg));

	return __crypto_ahash_alg(alg)->setkey != ahash_nosetkey;
}

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
		nhash->using_shash = true;
		*nctx = shash;
		return nhash;
	}

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

	if (alg->reqsize && alg->reqsize < alg->halg.statesize)
		return -EINVAL;

	err = hash_prepare_alg(&alg->halg);
	if (err)
		return err;

	base->cra_type = &crypto_ahash_type;
	base->cra_flags |= CRYPTO_ALG_TYPE_AHASH;

	if (!alg->setkey)
		alg->setkey = ahash_nosetkey;

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

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Asynchronous cryptographic hash type");
