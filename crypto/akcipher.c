// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Public Key Encryption
 *
 * Copyright (c) 2015, Intel Corporation
 * Authors: Tadeusz Struk <tadeusz.struk@intel.com>
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/crypto.h>
#include <linux/compiler.h>
#include <crypto/algapi.h>
#include <linux/cryptouser.h>
#include <net/netlink.h>
#include <crypto/akcipher.h>
#include <crypto/internal/akcipher.h>
#include "internal.h"

#ifdef CONFIG_NET
static int crypto_akcipher_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_akcipher rakcipher;

	memset(&rakcipher, 0, sizeof(rakcipher));

	strscpy(rakcipher.type, "akcipher", sizeof(rakcipher.type));

	return nla_put(skb, CRYPTOCFGA_REPORT_AKCIPHER,
		       sizeof(rakcipher), &rakcipher);
}
#else
static int crypto_akcipher_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif

static void crypto_akcipher_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;

static void crypto_akcipher_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_puts(m, "type         : akcipher\n");
}

static void crypto_akcipher_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_akcipher *akcipher = __crypto_akcipher_tfm(tfm);
	struct akcipher_alg *alg = crypto_akcipher_alg(akcipher);

	alg->exit(akcipher);
}

static int crypto_akcipher_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_akcipher *akcipher = __crypto_akcipher_tfm(tfm);
	struct akcipher_alg *alg = crypto_akcipher_alg(akcipher);

	if (alg->exit)
		akcipher->base.exit = crypto_akcipher_exit_tfm;

	if (alg->init)
		return alg->init(akcipher);

	return 0;
}

static void crypto_akcipher_free_instance(struct crypto_instance *inst)
{
	struct akcipher_instance *akcipher = akcipher_instance(inst);

	akcipher->free(akcipher);
}

static const struct crypto_type crypto_akcipher_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_akcipher_init_tfm,
	.free = crypto_akcipher_free_instance,
#ifdef CONFIG_PROC_FS
	.show = crypto_akcipher_show,
#endif
	.report = crypto_akcipher_report,
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_MASK,
	.type = CRYPTO_ALG_TYPE_AKCIPHER,
	.tfmsize = offsetof(struct crypto_akcipher, base),
};

int crypto_grab_akcipher(struct crypto_akcipher_spawn *spawn, const char *name,
			 u32 type, u32 mask)
{
	spawn->base.frontend = &crypto_akcipher_type;
	return crypto_grab_spawn(&spawn->base, name, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_grab_akcipher);

struct crypto_akcipher *crypto_alloc_akcipher(const char *alg_name, u32 type,
					      u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_akcipher_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_akcipher);

static void akcipher_prepare_alg(struct akcipher_alg *alg)
{
	struct crypto_alg *base = &alg->base;

	base->cra_type = &crypto_akcipher_type;
	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
	base->cra_flags |= CRYPTO_ALG_TYPE_AKCIPHER;
}

static int akcipher_default_op(struct akcipher_request *req)
{
	return -ENOSYS;
}

int crypto_register_akcipher(struct akcipher_alg *alg)
{
	struct crypto_alg *base = &alg->base;

	if (!alg->sign)
		alg->sign = akcipher_default_op;
	if (!alg->verify)
		alg->verify = akcipher_default_op;
	if (!alg->encrypt)
		alg->encrypt = akcipher_default_op;
	if (!alg->decrypt)
		alg->decrypt = akcipher_default_op;

	akcipher_prepare_alg(alg);
	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_akcipher);

void crypto_unregister_akcipher(struct akcipher_alg *alg)
{
	crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_akcipher);

int akcipher_register_instance(struct crypto_template *tmpl,
			       struct akcipher_instance *inst)
{
	akcipher_prepare_alg(&inst->alg);
	return crypto_register_instance(tmpl, akcipher_crypto_instance(inst));
}
EXPORT_SYMBOL_GPL(akcipher_register_instance);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic public key cipher type");
