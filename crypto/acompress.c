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

static inline int __crypto_acomp_report_stat(struct sk_buff *skb,
					     struct crypto_alg *alg)
{
	struct comp_alg_common *calg = __crypto_comp_alg_common(alg);
	struct crypto_istat_compress *istat = comp_get_stat(calg);
	struct crypto_stat_compress racomp;

	memset(&racomp, 0, sizeof(racomp));

	strscpy(racomp.type, "acomp", sizeof(racomp.type));
	racomp.stat_compress_cnt = atomic64_read(&istat->compress_cnt);
	racomp.stat_compress_tlen = atomic64_read(&istat->compress_tlen);
	racomp.stat_decompress_cnt =  atomic64_read(&istat->decompress_cnt);
	racomp.stat_decompress_tlen = atomic64_read(&istat->decompress_tlen);
	racomp.stat_err_cnt = atomic64_read(&istat->err_cnt);

	return nla_put(skb, CRYPTOCFGA_STAT_ACOMP, sizeof(racomp), &racomp);
}

#ifdef CONFIG_CRYPTO_STATS
int crypto_acomp_report_stat(struct sk_buff *skb, struct crypto_alg *alg)
{
	return __crypto_acomp_report_stat(skb, alg);
}
#endif

static const struct crypto_type crypto_acomp_type = {
	.extsize = crypto_acomp_extsize,
	.init_tfm = crypto_acomp_init_tfm,
#ifdef CONFIG_PROC_FS
	.show = crypto_acomp_show,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_USER)
	.report = crypto_acomp_report,
#endif
#ifdef CONFIG_CRYPTO_STATS
	.report_stat = crypto_acomp_report_stat,
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

struct acomp_req *acomp_request_alloc(struct crypto_acomp *acomp)
{
	struct crypto_tfm *tfm = crypto_acomp_tfm(acomp);
	struct acomp_req *req;

	req = __acomp_request_alloc(acomp);
	if (req && (tfm->__crt_alg->cra_type != &crypto_acomp_type))
		return crypto_acomp_scomp_alloc_ctx(req);

	return req;
}
EXPORT_SYMBOL_GPL(acomp_request_alloc);

void acomp_request_free(struct acomp_req *req)
{
	struct crypto_acomp *acomp = crypto_acomp_reqtfm(req);
	struct crypto_tfm *tfm = crypto_acomp_tfm(acomp);

	if (tfm->__crt_alg->cra_type != &crypto_acomp_type)
		crypto_acomp_scomp_free_ctx(req);

	if (req->flags & CRYPTO_ACOMP_ALLOC_OUTPUT) {
		acomp->dst_free(req->dst);
		req->dst = NULL;
	}

	__acomp_request_free(req);
}
EXPORT_SYMBOL_GPL(acomp_request_free);

void comp_prepare_alg(struct comp_alg_common *alg)
{
	struct crypto_istat_compress *istat = comp_get_stat(alg);
	struct crypto_alg *base = &alg->base;

	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;

	if (IS_ENABLED(CONFIG_CRYPTO_STATS))
		memset(istat, 0, sizeof(*istat));
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
