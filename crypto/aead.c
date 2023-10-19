// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AEAD: Authenticated Encryption with Associated Data
 *
 * This file provides API support for AEAD algorithms.
 *
 * Copyright (c) 2007-2015 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/internal/aead.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/cryptouser.h>
#include <net/netlink.h>

#include "internal.h"

static int setkey_unaligned(struct crypto_aead *tfm, const u8 *key,
			    unsigned int keylen)
{
	unsigned long alignmask = crypto_aead_alignmask(tfm);
	int ret;
	u8 *buffer, *alignbuffer;
	unsigned long absize;

	absize = keylen + alignmask;
	buffer = kmalloc(absize, GFP_ATOMIC);
	if (!buffer)
		return -ENOMEM;

	alignbuffer = (u8 *)ALIGN((unsigned long)buffer, alignmask + 1);
	memcpy(alignbuffer, key, keylen);
	ret = crypto_aead_alg(tfm)->setkey(tfm, alignbuffer, keylen);
	memset(alignbuffer, 0, keylen);
	kfree(buffer);
	return ret;
}

int crypto_aead_setkey(struct crypto_aead *tfm,
		       const u8 *key, unsigned int keylen)
{
	unsigned long alignmask = crypto_aead_alignmask(tfm);
	int err;

	if ((unsigned long)key & alignmask)
		err = setkey_unaligned(tfm, key, keylen);
	else
		err = crypto_aead_alg(tfm)->setkey(tfm, key, keylen);

	if (unlikely(err)) {
		crypto_aead_set_flags(tfm, CRYPTO_TFM_NEED_KEY);
		return err;
	}

	crypto_aead_clear_flags(tfm, CRYPTO_TFM_NEED_KEY);
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_aead_setkey);

int crypto_aead_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	int err;

	if ((!authsize && crypto_aead_maxauthsize(tfm)) ||
	    authsize > crypto_aead_maxauthsize(tfm))
		return -EINVAL;

	if (crypto_aead_alg(tfm)->setauthsize) {
		err = crypto_aead_alg(tfm)->setauthsize(tfm, authsize);
		if (err)
			return err;
	}

	tfm->authsize = authsize;
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_aead_setauthsize);

int crypto_aead_encrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_alg *alg = aead->base.__crt_alg;
	unsigned int cryptlen = req->cryptlen;
	int ret;

	crypto_stats_get(alg);
	if (crypto_aead_get_flags(aead) & CRYPTO_TFM_NEED_KEY)
		ret = -ENOKEY;
	else
		ret = crypto_aead_alg(aead)->encrypt(req);
	crypto_stats_aead_encrypt(cryptlen, alg, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(crypto_aead_encrypt);

int crypto_aead_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_alg *alg = aead->base.__crt_alg;
	unsigned int cryptlen = req->cryptlen;
	int ret;

	crypto_stats_get(alg);
	if (crypto_aead_get_flags(aead) & CRYPTO_TFM_NEED_KEY)
		ret = -ENOKEY;
	else if (req->cryptlen < crypto_aead_authsize(aead))
		ret = -EINVAL;
	else
		ret = crypto_aead_alg(aead)->decrypt(req);
	crypto_stats_aead_decrypt(cryptlen, alg, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(crypto_aead_decrypt);

static void crypto_aead_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_aead *aead = __crypto_aead_cast(tfm);
	struct aead_alg *alg = crypto_aead_alg(aead);

	alg->exit(aead);
}

static int crypto_aead_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_aead *aead = __crypto_aead_cast(tfm);
	struct aead_alg *alg = crypto_aead_alg(aead);

	crypto_aead_set_flags(aead, CRYPTO_TFM_NEED_KEY);

	aead->authsize = alg->maxauthsize;

	if (alg->exit)
		aead->base.exit = crypto_aead_exit_tfm;

	if (alg->init)
		return alg->init(aead);

	return 0;
}

#ifdef CONFIG_NET
static int crypto_aead_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_aead raead;
	struct aead_alg *aead = container_of(alg, struct aead_alg, base);

	memset(&raead, 0, sizeof(raead));

	strscpy(raead.type, "aead", sizeof(raead.type));
	strscpy(raead.geniv, "<none>", sizeof(raead.geniv));

	raead.blocksize = alg->cra_blocksize;
	raead.maxauthsize = aead->maxauthsize;
	raead.ivsize = aead->ivsize;

	return nla_put(skb, CRYPTOCFGA_REPORT_AEAD, sizeof(raead), &raead);
}
#else
static int crypto_aead_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif

static void crypto_aead_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;
static void crypto_aead_show(struct seq_file *m, struct crypto_alg *alg)
{
	struct aead_alg *aead = container_of(alg, struct aead_alg, base);

	seq_printf(m, "type         : aead\n");
	seq_printf(m, "async        : %s\n", alg->cra_flags & CRYPTO_ALG_ASYNC ?
					     "yes" : "no");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "ivsize       : %u\n", aead->ivsize);
	seq_printf(m, "maxauthsize  : %u\n", aead->maxauthsize);
	seq_printf(m, "geniv        : <none>\n");
}

static void crypto_aead_free_instance(struct crypto_instance *inst)
{
	struct aead_instance *aead = aead_instance(inst);

	aead->free(aead);
}

static const struct crypto_type crypto_aead_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_aead_init_tfm,
	.free = crypto_aead_free_instance,
#ifdef CONFIG_PROC_FS
	.show = crypto_aead_show,
#endif
	.report = crypto_aead_report,
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_MASK,
	.type = CRYPTO_ALG_TYPE_AEAD,
	.tfmsize = offsetof(struct crypto_aead, base),
};

int crypto_grab_aead(struct crypto_aead_spawn *spawn,
		     struct crypto_instance *inst,
		     const char *name, u32 type, u32 mask)
{
	spawn->base.frontend = &crypto_aead_type;
	return crypto_grab_spawn(&spawn->base, inst, name, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_grab_aead);

struct crypto_aead *crypto_alloc_aead(const char *alg_name, u32 type, u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_aead_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_aead);

static int aead_prepare_alg(struct aead_alg *alg)
{
	struct crypto_alg *base = &alg->base;

	if (max3(alg->maxauthsize, alg->ivsize, alg->chunksize) >
	    PAGE_SIZE / 8)
		return -EINVAL;

	if (!alg->chunksize)
		alg->chunksize = base->cra_blocksize;

	base->cra_type = &crypto_aead_type;
	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
	base->cra_flags |= CRYPTO_ALG_TYPE_AEAD;

	return 0;
}

int crypto_register_aead(struct aead_alg *alg)
{
	struct crypto_alg *base = &alg->base;
	int err;

	err = aead_prepare_alg(alg);
	if (err)
		return err;

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_aead);

void crypto_unregister_aead(struct aead_alg *alg)
{
	crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_aead);

int crypto_register_aeads(struct aead_alg *algs, int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = crypto_register_aead(&algs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (--i; i >= 0; --i)
		crypto_unregister_aead(&algs[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_register_aeads);

void crypto_unregister_aeads(struct aead_alg *algs, int count)
{
	int i;

	for (i = count - 1; i >= 0; --i)
		crypto_unregister_aead(&algs[i]);
}
EXPORT_SYMBOL_GPL(crypto_unregister_aeads);

int aead_register_instance(struct crypto_template *tmpl,
			   struct aead_instance *inst)
{
	int err;

	if (WARN_ON(!inst->free))
		return -EINVAL;

	err = aead_prepare_alg(&inst->alg);
	if (err)
		return err;

	return crypto_register_instance(tmpl, aead_crypto_instance(inst));
}
EXPORT_SYMBOL_GPL(aead_register_instance);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Authenticated Encryption with Associated Data (AEAD)");
