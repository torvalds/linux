// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * geniv: Shared IV generator code
 *
 * This file provides common code to IV generators such as seqiv.
 *
 * Copyright (c) 2007-2019 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/internal/geniv.h>
#include <crypto/internal/rng.h>
#include <crypto/null.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>

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

static void aead_geniv_free(struct aead_instance *inst)
{
	crypto_drop_aead(aead_instance_ctx(inst));
	kfree(inst);
}

struct aead_instance *aead_geniv_alloc(struct crypto_template *tmpl,
				       struct rtattr **tb)
{
	struct crypto_aead_spawn *spawn;
	struct aead_instance *inst;
	struct aead_alg *alg;
	unsigned int ivsize;
	unsigned int maxauthsize;
	u32 mask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_AEAD, &mask);
	if (err)
		return ERR_PTR(err);

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return ERR_PTR(-ENOMEM);

	spawn = aead_instance_ctx(inst);

	err = crypto_grab_aead(spawn, aead_crypto_instance(inst),
			       crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;

	alg = crypto_spawn_aead_alg(spawn);

	ivsize = crypto_aead_alg_ivsize(alg);
	maxauthsize = crypto_aead_alg_maxauthsize(alg);

	err = -EINVAL;
	if (ivsize < sizeof(u64))
		goto err_free_inst;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "%s(%s)", tmpl->name, alg->base.cra_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;
	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "%s(%s)", tmpl->name, alg->base.cra_driver_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	inst->alg.base.cra_priority = alg->base.cra_priority;
	inst->alg.base.cra_blocksize = alg->base.cra_blocksize;
	inst->alg.base.cra_alignmask = alg->base.cra_alignmask;
	inst->alg.base.cra_ctxsize = sizeof(struct aead_geniv_ctx);

	inst->alg.setkey = aead_geniv_setkey;
	inst->alg.setauthsize = aead_geniv_setauthsize;

	inst->alg.ivsize = ivsize;
	inst->alg.maxauthsize = maxauthsize;

	inst->free = aead_geniv_free;

out:
	return inst;

err_free_inst:
	aead_geniv_free(inst);
	inst = ERR_PTR(err);
	goto out;
}
EXPORT_SYMBOL_GPL(aead_geniv_alloc);

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

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Shared IV generator code");
