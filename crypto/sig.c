/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Public Key Signature Algorithm
 *
 * Copyright (c) 2023 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/internal/sig.h>
#include <linux/cryptouser.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <net/netlink.h>

#include "internal.h"

static void crypto_sig_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_sig *sig = __crypto_sig_tfm(tfm);
	struct sig_alg *alg = crypto_sig_alg(sig);

	alg->exit(sig);
}

static int crypto_sig_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_sig *sig = __crypto_sig_tfm(tfm);
	struct sig_alg *alg = crypto_sig_alg(sig);

	if (alg->exit)
		sig->base.exit = crypto_sig_exit_tfm;

	if (alg->init)
		return alg->init(sig);

	return 0;
}

static void crypto_sig_free_instance(struct crypto_instance *inst)
{
	struct sig_instance *sig = sig_instance(inst);

	sig->free(sig);
}

static void __maybe_unused crypto_sig_show(struct seq_file *m,
					   struct crypto_alg *alg)
{
	seq_puts(m, "type         : sig\n");
}

static int __maybe_unused crypto_sig_report(struct sk_buff *skb,
					    struct crypto_alg *alg)
{
	struct crypto_report_sig rsig = {};

	strscpy(rsig.type, "sig", sizeof(rsig.type));

	return nla_put(skb, CRYPTOCFGA_REPORT_SIG, sizeof(rsig), &rsig);
}

static const struct crypto_type crypto_sig_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_sig_init_tfm,
	.free = crypto_sig_free_instance,
#ifdef CONFIG_PROC_FS
	.show = crypto_sig_show,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_USER)
	.report = crypto_sig_report,
#endif
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_MASK,
	.type = CRYPTO_ALG_TYPE_SIG,
	.tfmsize = offsetof(struct crypto_sig, base),
};

struct crypto_sig *crypto_alloc_sig(const char *alg_name, u32 type, u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_sig_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_sig);

static int sig_default_sign(struct crypto_sig *tfm,
			    const void *src, unsigned int slen,
			    void *dst, unsigned int dlen)
{
	return -ENOSYS;
}

static int sig_default_verify(struct crypto_sig *tfm,
			      const void *src, unsigned int slen,
			      const void *dst, unsigned int dlen)
{
	return -ENOSYS;
}

static int sig_default_set_key(struct crypto_sig *tfm,
			       const void *key, unsigned int keylen)
{
	return -ENOSYS;
}

static unsigned int sig_default_size(struct crypto_sig *tfm)
{
	return DIV_ROUND_UP_POW2(crypto_sig_keysize(tfm), BITS_PER_BYTE);
}

static int sig_prepare_alg(struct sig_alg *alg)
{
	struct crypto_alg *base = &alg->base;

	if (!alg->sign)
		alg->sign = sig_default_sign;
	if (!alg->verify)
		alg->verify = sig_default_verify;
	if (!alg->set_priv_key)
		alg->set_priv_key = sig_default_set_key;
	if (!alg->set_pub_key)
		return -EINVAL;
	if (!alg->key_size)
		return -EINVAL;
	if (!alg->max_size)
		alg->max_size = sig_default_size;
	if (!alg->digest_size)
		alg->digest_size = sig_default_size;

	base->cra_type = &crypto_sig_type;
	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
	base->cra_flags |= CRYPTO_ALG_TYPE_SIG;

	return 0;
}

int crypto_register_sig(struct sig_alg *alg)
{
	struct crypto_alg *base = &alg->base;
	int err;

	err = sig_prepare_alg(alg);
	if (err)
		return err;

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_sig);

void crypto_unregister_sig(struct sig_alg *alg)
{
	crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_sig);

int sig_register_instance(struct crypto_template *tmpl,
			  struct sig_instance *inst)
{
	int err;

	if (WARN_ON(!inst->free))
		return -EINVAL;

	err = sig_prepare_alg(&inst->alg);
	if (err)
		return err;

	return crypto_register_instance(tmpl, sig_crypto_instance(inst));
}
EXPORT_SYMBOL_GPL(sig_register_instance);

int crypto_grab_sig(struct crypto_sig_spawn *spawn,
		    struct crypto_instance *inst,
		    const char *name, u32 type, u32 mask)
{
	spawn->base.frontend = &crypto_sig_type;
	return crypto_grab_spawn(&spawn->base, inst, name, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_grab_sig);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Public Key Signature Algorithms");
