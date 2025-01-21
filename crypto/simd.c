// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Shared crypto simd helpers
 *
 * Copyright (c) 2012 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 * Copyright (c) 2016 Herbert Xu <herbert@gondor.apana.org.au>
 * Copyright (c) 2019 Google LLC
 *
 * Based on aesni-intel_glue.c by:
 *  Copyright (C) 2008, Intel Corp.
 *    Author: Huang Ying <ying.huang@intel.com>
 */

/*
 * Shared crypto SIMD helpers.  These functions dynamically create and register
 * an skcipher or AEAD algorithm that wraps another, internal algorithm.  The
 * wrapper ensures that the internal algorithm is only executed in a context
 * where SIMD instructions are usable, i.e. where may_use_simd() returns true.
 * If SIMD is already usable, the wrapper directly calls the internal algorithm.
 * Otherwise it defers execution to a workqueue via cryptd.
 *
 * This is an alternative to the internal algorithm implementing a fallback for
 * the !may_use_simd() case itself.
 *
 * Note that the wrapper algorithm is asynchronous, i.e. it has the
 * CRYPTO_ALG_ASYNC flag set.  Therefore it won't be found by users who
 * explicitly allocate a synchronous algorithm.
 */

#include <crypto/cryptd.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <asm/simd.h>

/* skcipher support */

struct simd_skcipher_alg {
	const char *ialg_name;
	struct skcipher_alg alg;
};

struct simd_skcipher_ctx {
	struct cryptd_skcipher *cryptd_tfm;
};

static int simd_skcipher_setkey(struct crypto_skcipher *tfm, const u8 *key,
				unsigned int key_len)
{
	struct simd_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *child = &ctx->cryptd_tfm->base;

	crypto_skcipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(child, crypto_skcipher_get_flags(tfm) &
					 CRYPTO_TFM_REQ_MASK);
	return crypto_skcipher_setkey(child, key, key_len);
}

static int simd_skcipher_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct simd_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_request *subreq;
	struct crypto_skcipher *child;

	subreq = skcipher_request_ctx(req);
	*subreq = *req;

	if (!crypto_simd_usable() ||
	    (in_atomic() && cryptd_skcipher_queued(ctx->cryptd_tfm)))
		child = &ctx->cryptd_tfm->base;
	else
		child = cryptd_skcipher_child(ctx->cryptd_tfm);

	skcipher_request_set_tfm(subreq, child);

	return crypto_skcipher_encrypt(subreq);
}

static int simd_skcipher_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct simd_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_request *subreq;
	struct crypto_skcipher *child;

	subreq = skcipher_request_ctx(req);
	*subreq = *req;

	if (!crypto_simd_usable() ||
	    (in_atomic() && cryptd_skcipher_queued(ctx->cryptd_tfm)))
		child = &ctx->cryptd_tfm->base;
	else
		child = cryptd_skcipher_child(ctx->cryptd_tfm);

	skcipher_request_set_tfm(subreq, child);

	return crypto_skcipher_decrypt(subreq);
}

static void simd_skcipher_exit(struct crypto_skcipher *tfm)
{
	struct simd_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);

	cryptd_free_skcipher(ctx->cryptd_tfm);
}

static int simd_skcipher_init(struct crypto_skcipher *tfm)
{
	struct simd_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct cryptd_skcipher *cryptd_tfm;
	struct simd_skcipher_alg *salg;
	struct skcipher_alg *alg;
	unsigned reqsize;

	alg = crypto_skcipher_alg(tfm);
	salg = container_of(alg, struct simd_skcipher_alg, alg);

	cryptd_tfm = cryptd_alloc_skcipher(salg->ialg_name,
					   CRYPTO_ALG_INTERNAL,
					   CRYPTO_ALG_INTERNAL);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);

	ctx->cryptd_tfm = cryptd_tfm;

	reqsize = crypto_skcipher_reqsize(cryptd_skcipher_child(cryptd_tfm));
	reqsize = max(reqsize, crypto_skcipher_reqsize(&cryptd_tfm->base));
	reqsize += sizeof(struct skcipher_request);

	crypto_skcipher_set_reqsize(tfm, reqsize);

	return 0;
}

struct simd_skcipher_alg *simd_skcipher_create_compat(struct skcipher_alg *ialg,
						      const char *algname,
						      const char *drvname,
						      const char *basename)
{
	struct simd_skcipher_alg *salg;
	struct skcipher_alg *alg;
	int err;

	salg = kzalloc(sizeof(*salg), GFP_KERNEL);
	if (!salg) {
		salg = ERR_PTR(-ENOMEM);
		goto out;
	}

	salg->ialg_name = basename;
	alg = &salg->alg;

	err = -ENAMETOOLONG;
	if (snprintf(alg->base.cra_name, CRYPTO_MAX_ALG_NAME, "%s", algname) >=
	    CRYPTO_MAX_ALG_NAME)
		goto out_free_salg;

	if (snprintf(alg->base.cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		     drvname) >= CRYPTO_MAX_ALG_NAME)
		goto out_free_salg;

	alg->base.cra_flags = CRYPTO_ALG_ASYNC |
		(ialg->base.cra_flags & CRYPTO_ALG_INHERITED_FLAGS);
	alg->base.cra_priority = ialg->base.cra_priority;
	alg->base.cra_blocksize = ialg->base.cra_blocksize;
	alg->base.cra_alignmask = ialg->base.cra_alignmask;
	alg->base.cra_module = ialg->base.cra_module;
	alg->base.cra_ctxsize = sizeof(struct simd_skcipher_ctx);

	alg->ivsize = ialg->ivsize;
	alg->chunksize = ialg->chunksize;
	alg->min_keysize = ialg->min_keysize;
	alg->max_keysize = ialg->max_keysize;

	alg->init = simd_skcipher_init;
	alg->exit = simd_skcipher_exit;

	alg->setkey = simd_skcipher_setkey;
	alg->encrypt = simd_skcipher_encrypt;
	alg->decrypt = simd_skcipher_decrypt;

	err = crypto_register_skcipher(alg);
	if (err)
		goto out_free_salg;

out:
	return salg;

out_free_salg:
	kfree(salg);
	salg = ERR_PTR(err);
	goto out;
}
EXPORT_SYMBOL_GPL(simd_skcipher_create_compat);

void simd_skcipher_free(struct simd_skcipher_alg *salg)
{
	crypto_unregister_skcipher(&salg->alg);
	kfree(salg);
}
EXPORT_SYMBOL_GPL(simd_skcipher_free);

int simd_register_skciphers_compat(struct skcipher_alg *algs, int count,
				   struct simd_skcipher_alg **simd_algs)
{
	int err;
	int i;
	const char *algname;
	const char *drvname;
	const char *basename;
	struct simd_skcipher_alg *simd;

	err = crypto_register_skciphers(algs, count);
	if (err)
		return err;

	for (i = 0; i < count; i++) {
		WARN_ON(strncmp(algs[i].base.cra_name, "__", 2));
		WARN_ON(strncmp(algs[i].base.cra_driver_name, "__", 2));
		algname = algs[i].base.cra_name + 2;
		drvname = algs[i].base.cra_driver_name + 2;
		basename = algs[i].base.cra_driver_name;
		simd = simd_skcipher_create_compat(algs + i, algname, drvname, basename);
		err = PTR_ERR(simd);
		if (IS_ERR(simd))
			goto err_unregister;
		simd_algs[i] = simd;
	}
	return 0;

err_unregister:
	simd_unregister_skciphers(algs, count, simd_algs);
	return err;
}
EXPORT_SYMBOL_GPL(simd_register_skciphers_compat);

void simd_unregister_skciphers(struct skcipher_alg *algs, int count,
			       struct simd_skcipher_alg **simd_algs)
{
	int i;

	crypto_unregister_skciphers(algs, count);

	for (i = 0; i < count; i++) {
		if (simd_algs[i]) {
			simd_skcipher_free(simd_algs[i]);
			simd_algs[i] = NULL;
		}
	}
}
EXPORT_SYMBOL_GPL(simd_unregister_skciphers);

/* AEAD support */

struct simd_aead_alg {
	const char *ialg_name;
	struct aead_alg alg;
};

struct simd_aead_ctx {
	struct cryptd_aead *cryptd_tfm;
};

static int simd_aead_setkey(struct crypto_aead *tfm, const u8 *key,
				unsigned int key_len)
{
	struct simd_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_aead *child = &ctx->cryptd_tfm->base;

	crypto_aead_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(child, crypto_aead_get_flags(tfm) &
				     CRYPTO_TFM_REQ_MASK);
	return crypto_aead_setkey(child, key, key_len);
}

static int simd_aead_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	struct simd_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_aead *child = &ctx->cryptd_tfm->base;

	return crypto_aead_setauthsize(child, authsize);
}

static int simd_aead_encrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct simd_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_request *subreq;
	struct crypto_aead *child;

	subreq = aead_request_ctx(req);
	*subreq = *req;

	if (!crypto_simd_usable() ||
	    (in_atomic() && cryptd_aead_queued(ctx->cryptd_tfm)))
		child = &ctx->cryptd_tfm->base;
	else
		child = cryptd_aead_child(ctx->cryptd_tfm);

	aead_request_set_tfm(subreq, child);

	return crypto_aead_encrypt(subreq);
}

static int simd_aead_decrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct simd_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_request *subreq;
	struct crypto_aead *child;

	subreq = aead_request_ctx(req);
	*subreq = *req;

	if (!crypto_simd_usable() ||
	    (in_atomic() && cryptd_aead_queued(ctx->cryptd_tfm)))
		child = &ctx->cryptd_tfm->base;
	else
		child = cryptd_aead_child(ctx->cryptd_tfm);

	aead_request_set_tfm(subreq, child);

	return crypto_aead_decrypt(subreq);
}

static void simd_aead_exit(struct crypto_aead *tfm)
{
	struct simd_aead_ctx *ctx = crypto_aead_ctx(tfm);

	cryptd_free_aead(ctx->cryptd_tfm);
}

static int simd_aead_init(struct crypto_aead *tfm)
{
	struct simd_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct cryptd_aead *cryptd_tfm;
	struct simd_aead_alg *salg;
	struct aead_alg *alg;
	unsigned reqsize;

	alg = crypto_aead_alg(tfm);
	salg = container_of(alg, struct simd_aead_alg, alg);

	cryptd_tfm = cryptd_alloc_aead(salg->ialg_name, CRYPTO_ALG_INTERNAL,
				       CRYPTO_ALG_INTERNAL);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);

	ctx->cryptd_tfm = cryptd_tfm;

	reqsize = crypto_aead_reqsize(cryptd_aead_child(cryptd_tfm));
	reqsize = max(reqsize, crypto_aead_reqsize(&cryptd_tfm->base));
	reqsize += sizeof(struct aead_request);

	crypto_aead_set_reqsize(tfm, reqsize);

	return 0;
}

static struct simd_aead_alg *simd_aead_create_compat(struct aead_alg *ialg,
						     const char *algname,
						     const char *drvname,
						     const char *basename)
{
	struct simd_aead_alg *salg;
	struct aead_alg *alg;
	int err;

	salg = kzalloc(sizeof(*salg), GFP_KERNEL);
	if (!salg) {
		salg = ERR_PTR(-ENOMEM);
		goto out;
	}

	salg->ialg_name = basename;
	alg = &salg->alg;

	err = -ENAMETOOLONG;
	if (snprintf(alg->base.cra_name, CRYPTO_MAX_ALG_NAME, "%s", algname) >=
	    CRYPTO_MAX_ALG_NAME)
		goto out_free_salg;

	if (snprintf(alg->base.cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		     drvname) >= CRYPTO_MAX_ALG_NAME)
		goto out_free_salg;

	alg->base.cra_flags = CRYPTO_ALG_ASYNC |
		(ialg->base.cra_flags & CRYPTO_ALG_INHERITED_FLAGS);
	alg->base.cra_priority = ialg->base.cra_priority;
	alg->base.cra_blocksize = ialg->base.cra_blocksize;
	alg->base.cra_alignmask = ialg->base.cra_alignmask;
	alg->base.cra_module = ialg->base.cra_module;
	alg->base.cra_ctxsize = sizeof(struct simd_aead_ctx);

	alg->ivsize = ialg->ivsize;
	alg->maxauthsize = ialg->maxauthsize;
	alg->chunksize = ialg->chunksize;

	alg->init = simd_aead_init;
	alg->exit = simd_aead_exit;

	alg->setkey = simd_aead_setkey;
	alg->setauthsize = simd_aead_setauthsize;
	alg->encrypt = simd_aead_encrypt;
	alg->decrypt = simd_aead_decrypt;

	err = crypto_register_aead(alg);
	if (err)
		goto out_free_salg;

out:
	return salg;

out_free_salg:
	kfree(salg);
	salg = ERR_PTR(err);
	goto out;
}

static void simd_aead_free(struct simd_aead_alg *salg)
{
	crypto_unregister_aead(&salg->alg);
	kfree(salg);
}

int simd_register_aeads_compat(struct aead_alg *algs, int count,
			       struct simd_aead_alg **simd_algs)
{
	int err;
	int i;
	const char *algname;
	const char *drvname;
	const char *basename;
	struct simd_aead_alg *simd;

	err = crypto_register_aeads(algs, count);
	if (err)
		return err;

	for (i = 0; i < count; i++) {
		WARN_ON(strncmp(algs[i].base.cra_name, "__", 2));
		WARN_ON(strncmp(algs[i].base.cra_driver_name, "__", 2));
		algname = algs[i].base.cra_name + 2;
		drvname = algs[i].base.cra_driver_name + 2;
		basename = algs[i].base.cra_driver_name;
		simd = simd_aead_create_compat(algs + i, algname, drvname, basename);
		err = PTR_ERR(simd);
		if (IS_ERR(simd))
			goto err_unregister;
		simd_algs[i] = simd;
	}
	return 0;

err_unregister:
	simd_unregister_aeads(algs, count, simd_algs);
	return err;
}
EXPORT_SYMBOL_GPL(simd_register_aeads_compat);

void simd_unregister_aeads(struct aead_alg *algs, int count,
			   struct simd_aead_alg **simd_algs)
{
	int i;

	crypto_unregister_aeads(algs, count);

	for (i = 0; i < count; i++) {
		if (simd_algs[i]) {
			simd_aead_free(simd_algs[i]);
			simd_algs[i] = NULL;
		}
	}
}
EXPORT_SYMBOL_GPL(simd_unregister_aeads);

MODULE_DESCRIPTION("Shared crypto SIMD helpers");
MODULE_LICENSE("GPL");
