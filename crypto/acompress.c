// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Asynchronous Compression operations
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Weigang Li <weigang.li@intel.com>
 *          Giovanni Cabiddu <giovanni.cabiddu@intel.com>
 */

#include <crypto/internal/acompress.h>
#include <linux/cryptouser.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <net/netlink.h>

#include "compress.h"

struct crypto_scomp;

static const struct crypto_type crypto_acomp_type;

static void acomp_reqchain_done(void *data, int err);

static inline struct acomp_alg *__crypto_acomp_alg(struct crypto_alg *alg)
{
	return container_of(alg, struct acomp_alg, calg.base);
}

static inline struct acomp_alg *crypto_acomp_alg(struct crypto_acomp *tfm)
{
	return __crypto_acomp_alg(crypto_acomp_tfm(tfm)->__crt_alg);
}

static int __maybe_unused crypto_acomp_report(
	struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_acomp racomp;

	memset(&racomp, 0, sizeof(racomp));

	strscpy(racomp.type, "acomp", sizeof(racomp.type));

	return nla_put(skb, CRYPTOCFGA_REPORT_ACOMP, sizeof(racomp), &racomp);
}

static void crypto_acomp_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;

static void crypto_acomp_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_puts(m, "type         : acomp\n");
}

static void crypto_acomp_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_acomp *acomp = __crypto_acomp_tfm(tfm);
	struct acomp_alg *alg = crypto_acomp_alg(acomp);

	alg->exit(acomp);
}

static int crypto_acomp_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_acomp *acomp = __crypto_acomp_tfm(tfm);
	struct acomp_alg *alg = crypto_acomp_alg(acomp);

	if (tfm->__crt_alg->cra_type != &crypto_acomp_type)
		return crypto_init_scomp_ops_async(tfm);

	acomp->compress = alg->compress;
	acomp->decompress = alg->decompress;
	acomp->dst_free = alg->dst_free;
	acomp->reqsize = alg->reqsize;

	if (alg->exit)
		acomp->base.exit = crypto_acomp_exit_tfm;

	if (alg->init)
		return alg->init(acomp);

	return 0;
}

static unsigned int crypto_acomp_extsize(struct crypto_alg *alg)
{
	int extsize = crypto_alg_extsize(alg);

	if (alg->cra_type != &crypto_acomp_type)
		extsize += sizeof(struct crypto_scomp *);

	return extsize;
}

static const struct crypto_type crypto_acomp_type = {
	.extsize = crypto_acomp_extsize,
	.init_tfm = crypto_acomp_init_tfm,
#ifdef CONFIG_PROC_FS
	.show = crypto_acomp_show,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_USER)
	.report = crypto_acomp_report,
#endif
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_ACOMPRESS_MASK,
	.type = CRYPTO_ALG_TYPE_ACOMPRESS,
	.tfmsize = offsetof(struct crypto_acomp, base),
};

struct crypto_acomp *crypto_alloc_acomp(const char *alg_name, u32 type,
					u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_acomp_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_acomp);

struct crypto_acomp *crypto_alloc_acomp_node(const char *alg_name, u32 type,
					u32 mask, int node)
{
	return crypto_alloc_tfm_node(alg_name, &crypto_acomp_type, type, mask,
				node);
}
EXPORT_SYMBOL_GPL(crypto_alloc_acomp_node);

static bool acomp_request_has_nondma(struct acomp_req *req)
{
	struct acomp_req *r2;

	if (acomp_request_isnondma(req))
		return true;

	list_for_each_entry(r2, &req->base.list, base.list)
		if (acomp_request_isnondma(r2))
			return true;

	return false;
}

static void acomp_save_req(struct acomp_req *req, crypto_completion_t cplt)
{
	struct crypto_acomp *tfm = crypto_acomp_reqtfm(req);
	struct acomp_req_chain *state = &req->chain;

	if (!acomp_is_async(tfm))
		return;

	state->compl = req->base.complete;
	state->data = req->base.data;
	req->base.complete = cplt;
	req->base.data = state;
	state->req0 = req;
}

static void acomp_restore_req(struct acomp_req_chain *state)
{
	struct acomp_req *req = state->req0;
	struct crypto_acomp *tfm;

	tfm = crypto_acomp_reqtfm(req);
	if (!acomp_is_async(tfm))
		return;

	req->base.complete = state->compl;
	req->base.data = state->data;
}

static void acomp_reqchain_virt(struct acomp_req_chain *state, int err)
{
	struct acomp_req *req = state->cur;
	unsigned int slen = req->slen;
	unsigned int dlen = req->dlen;

	req->base.err = err;
	state = &req->chain;

	if (state->src)
		acomp_request_set_src_dma(req, state->src, slen);
	if (state->dst)
		acomp_request_set_dst_dma(req, state->dst, dlen);
	state->src = NULL;
	state->dst = NULL;
}

static void acomp_virt_to_sg(struct acomp_req *req)
{
	struct acomp_req_chain *state = &req->chain;

	if (acomp_request_src_isvirt(req)) {
		unsigned int slen = req->slen;
		const u8 *svirt = req->svirt;

		state->src = svirt;
		sg_init_one(&state->ssg, svirt, slen);
		acomp_request_set_src_sg(req, &state->ssg, slen);
	}

	if (acomp_request_dst_isvirt(req)) {
		unsigned int dlen = req->dlen;
		u8 *dvirt = req->dvirt;

		state->dst = dvirt;
		sg_init_one(&state->dsg, dvirt, dlen);
		acomp_request_set_dst_sg(req, &state->dsg, dlen);
	}
}

static int acomp_reqchain_finish(struct acomp_req_chain *state,
				 int err, u32 mask)
{
	struct acomp_req *req0 = state->req0;
	struct acomp_req *req = state->cur;
	struct acomp_req *n;

	acomp_reqchain_virt(state, err);

	if (req != req0)
		list_add_tail(&req->base.list, &req0->base.list);

	list_for_each_entry_safe(req, n, &state->head, base.list) {
		list_del_init(&req->base.list);

		req->base.flags &= mask;
		req->base.complete = acomp_reqchain_done;
		req->base.data = state;
		state->cur = req;

		acomp_virt_to_sg(req);
		err = state->op(req);

		if (err == -EINPROGRESS) {
			if (!list_empty(&state->head))
				err = -EBUSY;
			goto out;
		}

		if (err == -EBUSY)
			goto out;

		acomp_reqchain_virt(state, err);
		list_add_tail(&req->base.list, &req0->base.list);
	}

	acomp_restore_req(state);

out:
	return err;
}

static void acomp_reqchain_done(void *data, int err)
{
	struct acomp_req_chain *state = data;
	crypto_completion_t compl = state->compl;

	data = state->data;

	if (err == -EINPROGRESS) {
		if (!list_empty(&state->head))
			return;
		goto notify;
	}

	err = acomp_reqchain_finish(state, err, CRYPTO_TFM_REQ_MAY_BACKLOG);
	if (err == -EBUSY)
		return;

notify:
	compl(data, err);
}

static int acomp_do_req_chain(struct acomp_req *req,
			      int (*op)(struct acomp_req *req))
{
	struct crypto_acomp *tfm = crypto_acomp_reqtfm(req);
	struct acomp_req_chain *state = &req->chain;
	int err;

	if (crypto_acomp_req_chain(tfm) ||
	    (!acomp_request_chained(req) && !acomp_request_isvirt(req)))
		return op(req);

	/*
	 * There are no in-kernel users that do this.  If and ever
	 * such users come into being then we could add a fall-back
	 * path.
	 */
	if (acomp_request_has_nondma(req))
		return -EINVAL;

	if (acomp_is_async(tfm)) {
		acomp_save_req(req, acomp_reqchain_done);
		state = req->base.data;
	}

	state->op = op;
	state->cur = req;
	state->src = NULL;
	INIT_LIST_HEAD(&state->head);
	list_splice_init(&req->base.list, &state->head);

	acomp_virt_to_sg(req);
	err = op(req);
	if (err == -EBUSY || err == -EINPROGRESS)
		return -EBUSY;

	return acomp_reqchain_finish(state, err, ~0);
}

int crypto_acomp_compress(struct acomp_req *req)
{
	return acomp_do_req_chain(req, crypto_acomp_reqtfm(req)->compress);
}
EXPORT_SYMBOL_GPL(crypto_acomp_compress);

int crypto_acomp_decompress(struct acomp_req *req)
{
	return acomp_do_req_chain(req, crypto_acomp_reqtfm(req)->decompress);
}
EXPORT_SYMBOL_GPL(crypto_acomp_decompress);

void comp_prepare_alg(struct comp_alg_common *alg)
{
	struct crypto_alg *base = &alg->base;

	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
}

int crypto_register_acomp(struct acomp_alg *alg)
{
	struct crypto_alg *base = &alg->calg.base;

	comp_prepare_alg(&alg->calg);

	base->cra_type = &crypto_acomp_type;
	base->cra_flags |= CRYPTO_ALG_TYPE_ACOMPRESS;

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_acomp);

void crypto_unregister_acomp(struct acomp_alg *alg)
{
	crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_acomp);

int crypto_register_acomps(struct acomp_alg *algs, int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = crypto_register_acomp(&algs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (--i; i >= 0; --i)
		crypto_unregister_acomp(&algs[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_register_acomps);

void crypto_unregister_acomps(struct acomp_alg *algs, int count)
{
	int i;

	for (i = count - 1; i >= 0; --i)
		crypto_unregister_acomp(&algs[i]);
}
EXPORT_SYMBOL_GPL(crypto_unregister_acomps);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Asynchronous compression type");
