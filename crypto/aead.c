/*
 * AEAD: Authenticated Encryption with Associated Data
 *
 * This file provides API support for AEAD algorithms.
 *
 * Copyright (c) 2007-2015 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/geniv.h>
#include <crypto/internal/rng.h>
#include <crypto/null.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/cryptouser.h>
#include <linux/compiler.h>
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

	if (authsize > crypto_aead_maxauthsize(tfm))
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

	strncpy(raead.type, "aead", sizeof(raead.type));
	strncpy(raead.geniv, "<none>", sizeof(raead.geniv));

	raead.blocksize = alg->cra_blocksize;
	raead.maxauthsize = aead->maxauthsize;
	raead.ivsize = aead->ivsize;

	if (nla_put(skb, CRYPTOCFGA_REPORT_AEAD,
		    sizeof(struct crypto_report_aead), &raead))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
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

	if (!aead->free) {
		inst->tmpl->free(inst);
		return;
	}

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

static int aead_geniv_setkey(struct crypto_aead *tfm,
			     const u8 *key, unsigned int keylen)
{
	struct aead_geniv_ctx *ctx = crypto_aead_ctx(tfm);

	return crypto_aead_setkey(ctx->child, key, keylen);
}

static int aead_geniv_setauthsize(struct crypto_aead *tfm,
				  unsigned int authsize)
{
	struct aead_geniv_ctx *ctx = crypto_aead_ctx(tfm);

	return crypto_aead_setauthsize(ctx->child, authsize);
}

struct aead_instance *aead_geniv_alloc(struct crypto_template *tmpl,
				       struct rtattr **tb, u32 type, u32 mask)
{
	const char *name;
	struct crypto_aead_spawn *spawn;
	struct crypto_attr_type *algt;
	struct aead_instance *inst;
	struct aead_alg *alg;
	unsigned int ivsize;
	unsigned int maxauthsize;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return ERR_CAST(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return ERR_PTR(-EINVAL);

	name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(name))
		return ERR_CAST(name);

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return ERR_PTR(-ENOMEM);

	spawn = aead_instance_ctx(inst);

	/* Ignore async algorithms if necessary. */
	mask |= crypto_requires_sync(algt->type, algt->mask);

	crypto_set_aead_spawn(spawn, aead_crypto_instance(inst));
	err = crypto_grab_aead(spawn, name, type, mask);
	if (err)
		goto err_free_inst;

	alg = crypto_spawn_aead_alg(spawn);

	ivsize = crypto_aead_alg_ivsize(alg);
	maxauthsize = crypto_aead_alg_maxauthsize(alg);

	err = -EINVAL;
	if (ivsize < sizeof(u64))
		goto err_drop_alg;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "%s(%s)", tmpl->name, alg->base.cra_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto err_drop_alg;
	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "%s(%s)", tmpl->name, alg->base.cra_driver_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto err_drop_alg;

	inst->alg.base.cra_flags = alg->base.cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.base.cra_priority = alg->base.cra_priority;
	inst->alg.base.cra_blocksize = alg->base.cra_blocksize;
	inst->alg.base.cra_alignmask = alg->base.cra_alignmask;
	inst->alg.base.cra_ctxsize = sizeof(struct aead_geniv_ctx);

	inst->alg.setkey = aead_geniv_setkey;
	inst->alg.setauthsize = aead_geniv_setauthsize;

	inst->alg.ivsize = ivsize;
	inst->alg.maxauthsize = maxauthsize;

out:
	return inst;

err_drop_alg:
	crypto_drop_aead(spawn);
err_free_inst:
	kfree(inst);
	inst = ERR_PTR(err);
	goto out;
}
EXPORT_SYMBOL_GPL(aead_geniv_alloc);

void aead_geniv_free(struct aead_instance *inst)
{
	crypto_drop_aead(aead_instance_ctx(inst));
	kfree(inst);
}
EXPORT_SYMBOL_GPL(aead_geniv_free);

int aead_init_geniv(struct crypto_aead *aead)
{
	struct aead_geniv_ctx *ctx = crypto_aead_ctx(aead);
	struct aead_instance *inst = aead_alg_instance(aead);
	struct crypto_aead *child;
	int err;

	spin_lock_init(&ctx->lock);

	err = crypto_get_default_rng();
	if (err)
		goto out;

	err = crypto_rng_get_bytes(crypto_default_rng, ctx->salt,
				   crypto_aead_ivsize(aead));
	crypto_put_default_rng();
	if (err)
		goto out;

	ctx->sknull = crypto_get_default_null_skcipher();
	err = PTR_ERR(ctx->sknull);
	if (IS_ERR(ctx->sknull))
		goto out;

	child = crypto_spawn_aead(aead_instance_ctx(inst));
	err = PTR_ERR(child);
	if (IS_ERR(child))
		goto drop_null;

	ctx->child = child;
	crypto_aead_set_reqsize(aead, crypto_aead_reqsize(child) +
				      sizeof(struct aead_request));

	err = 0;

out:
	return err;

drop_null:
	crypto_put_default_null_skcipher();
	goto out;
}
EXPORT_SYMBOL_GPL(aead_init_geniv);

void aead_exit_geniv(struct crypto_aead *tfm)
{
	struct aead_geniv_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_aead(ctx->child);
	crypto_put_default_null_skcipher();
}
EXPORT_SYMBOL_GPL(aead_exit_geniv);

int crypto_grab_aead(struct crypto_aead_spawn *spawn, const char *name,
		     u32 type, u32 mask)
{
	spawn->base.frontend = &crypto_aead_type;
	return crypto_grab_spawn(&spawn->base, name, type, mask);
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

	err = aead_prepare_alg(&inst->alg);
	if (err)
		return err;

	return crypto_register_instance(tmpl, aead_crypto_instance(inst));
}
EXPORT_SYMBOL_GPL(aead_register_instance);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Authenticated Encryption with Associated Data (AEAD)");
