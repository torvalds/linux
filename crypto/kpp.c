// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Key-agreement Protocol Primitives (KPP)
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 */

#include <crypto/internal/kpp.h>
#include <linux/cryptouser.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <net/netlink.h>

#include "internal.h"

static int __maybe_unused crypto_kpp_report(
	struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_kpp rkpp;

	memset(&rkpp, 0, sizeof(rkpp));

	strscpy(rkpp.type, "kpp", sizeof(rkpp.type));

	return nla_put(skb, CRYPTOCFGA_REPORT_KPP, sizeof(rkpp), &rkpp);
}

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

static void crypto_kpp_free_instance(struct crypto_instance *inst)
{
	struct kpp_instance *kpp = kpp_instance(inst);

	kpp->free(kpp);
}

static const struct crypto_type crypto_kpp_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_kpp_init_tfm,
	.free = crypto_kpp_free_instance,
#ifdef CONFIG_PROC_FS
	.show = crypto_kpp_show,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_USER)
	.report = crypto_kpp_report,
#endif
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

int crypto_grab_kpp(struct crypto_kpp_spawn *spawn,
		    struct crypto_instance *inst,
		    const char *name, u32 type, u32 mask)
{
	spawn->base.frontend = &crypto_kpp_type;
	return crypto_grab_spawn(&spawn->base, inst, name, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_grab_kpp);

int crypto_has_kpp(const char *alg_name, u32 type, u32 mask)
{
	return crypto_type_has_alg(alg_name, &crypto_kpp_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_has_kpp);

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

int kpp_register_instance(struct crypto_template *tmpl,
			  struct kpp_instance *inst)
{
	if (WARN_ON(!inst->free))
		return -EINVAL;

	kpp_prepare_alg(&inst->alg);

	return crypto_register_instance(tmpl, kpp_crypto_instance(inst));
}
EXPORT_SYMBOL_GPL(kpp_register_instance);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Key-agreement Protocol Primitives");
