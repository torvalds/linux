/*
 * Key-agreement Protocol Primitives (KPP)
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/crypto.h>
#include <crypto/algapi.h>
#include <linux/cryptouser.h>
#include <linux/compiler.h>
#include <net/netlink.h>
#include <crypto/kpp.h>
#include <crypto/internal/kpp.h>
#include "internal.h"

#ifdef CONFIG_NET
static int crypto_kpp_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_kpp rkpp;

	strncpy(rkpp.type, "kpp", sizeof(rkpp.type));

	if (nla_put(skb, CRYPTOCFGA_REPORT_KPP,
		    sizeof(struct crypto_report_kpp), &rkpp))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}
#else
static int crypto_kpp_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif

static void crypto_kpp_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;

static void crypto_kpp_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_puts(m, "type         : kpp\n");
}

static void crypto_kpp_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_kpp *kpp = __crypto_kpp_tfm(tfm);
	struct kpp_alg *alg = crypto_kpp_alg(kpp);

	alg->exit(kpp);
}

static int crypto_kpp_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_kpp *kpp = __crypto_kpp_tfm(tfm);
	struct kpp_alg *alg = crypto_kpp_alg(kpp);

	if (alg->exit)
		kpp->base.exit = crypto_kpp_exit_tfm;

	if (alg->init)
		return alg->init(kpp);

	return 0;
}

static const struct crypto_type crypto_kpp_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_kpp_init_tfm,
#ifdef CONFIG_PROC_FS
	.show = crypto_kpp_show,
#endif
	.report = crypto_kpp_report,
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_MASK,
	.type = CRYPTO_ALG_TYPE_KPP,
	.tfmsize = offsetof(struct crypto_kpp, base),
};

struct crypto_kpp *crypto_alloc_kpp(const char *alg_name, u32 type, u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_kpp_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_kpp);

static void kpp_prepare_alg(struct kpp_alg *alg)
{
	struct crypto_alg *base = &alg->base;

	base->cra_type = &crypto_kpp_type;
	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
	base->cra_flags |= CRYPTO_ALG_TYPE_KPP;
}

int crypto_register_kpp(struct kpp_alg *alg)
{
	struct crypto_alg *base = &alg->base;

	kpp_prepare_alg(alg);
	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_kpp);

void crypto_unregister_kpp(struct kpp_alg *alg)
{
	crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_kpp);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Key-agreement Protocol Primitives");
