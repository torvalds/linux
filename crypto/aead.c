/*
 * AEAD: Authenticated Encryption with Associated Data
 *
 * This file provides API support for AEAD algorithms.
 *
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/geniv.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/cryptouser.h>
#include <net/netlink.h>

#include "internal.h"

struct compat_request_ctx {
	struct scatterlist src[2];
	struct scatterlist dst[2];
	struct scatterlist ivbuf[2];
	struct scatterlist *ivsg;
	struct aead_givcrypt_request subreq;
};

static int aead_null_givencrypt(struct aead_givcrypt_request *req);
static int aead_null_givdecrypt(struct aead_givcrypt_request *req);

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
	ret = tfm->setkey(tfm, alignbuffer, keylen);
	memset(alignbuffer, 0, keylen);
	kfree(buffer);
	return ret;
}

int crypto_aead_setkey(struct crypto_aead *tfm,
		       const u8 *key, unsigned int keylen)
{
	unsigned long alignmask = crypto_aead_alignmask(tfm);

	tfm = tfm->child;

	if ((unsigned long)key & alignmask)
		return setkey_unaligned(tfm, key, keylen);

	return tfm->setkey(tfm, key, keylen);
}
EXPORT_SYMBOL_GPL(crypto_aead_setkey);

int crypto_aead_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	int err;

	if (authsize > crypto_aead_maxauthsize(tfm))
		return -EINVAL;

	if (tfm->setauthsize) {
		err = tfm->setauthsize(tfm->child, authsize);
		if (err)
			return err;
	}

	tfm->child->authsize = authsize;
	tfm->authsize = authsize;
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_aead_setauthsize);

struct aead_old_request {
	struct scatterlist srcbuf[2];
	struct scatterlist dstbuf[2];
	struct aead_request subreq;
};

unsigned int crypto_aead_reqsize(struct crypto_aead *tfm)
{
	return tfm->reqsize + sizeof(struct aead_old_request);
}
EXPORT_SYMBOL_GPL(crypto_aead_reqsize);

static int old_crypt(struct aead_request *req,
		     int (*crypt)(struct aead_request *req))
{
	struct aead_old_request *nreq = aead_request_ctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct scatterlist *src, *dst;

	if (req->old)
		return crypt(req);

	src = scatterwalk_ffwd(nreq->srcbuf, req->src, req->assoclen);
	dst = req->src == req->dst ?
	      src : scatterwalk_ffwd(nreq->dstbuf, req->dst, req->assoclen);

	aead_request_set_tfm(&nreq->subreq, aead);
	aead_request_set_callback(&nreq->subreq, aead_request_flags(req),
				  req->base.complete, req->base.data);
	aead_request_set_crypt(&nreq->subreq, src, dst, req->cryptlen,
			       req->iv);
	aead_request_set_assoc(&nreq->subreq, req->src, req->assoclen);

	return crypt(&nreq->subreq);
}

static int old_encrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct old_aead_alg *alg = crypto_old_aead_alg(aead);

	return old_crypt(req, alg->encrypt);
}

static int old_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct old_aead_alg *alg = crypto_old_aead_alg(aead);

	return old_crypt(req, alg->decrypt);
}

static int no_givcrypt(struct aead_givcrypt_request *req)
{
	return -ENOSYS;
}

static int crypto_old_aead_init_tfm(struct crypto_tfm *tfm)
{
	struct old_aead_alg *alg = &tfm->__crt_alg->cra_aead;
	struct crypto_aead *crt = __crypto_aead_cast(tfm);

	if (max(alg->maxauthsize, alg->ivsize) > PAGE_SIZE / 8)
		return -EINVAL;

	crt->setkey = alg->setkey;
	crt->setauthsize = alg->setauthsize;
	crt->encrypt = old_encrypt;
	crt->decrypt = old_decrypt;
	if (alg->ivsize) {
		crt->givencrypt = alg->givencrypt ?: no_givcrypt;
		crt->givdecrypt = alg->givdecrypt ?: no_givcrypt;
	} else {
		crt->givencrypt = aead_null_givencrypt;
		crt->givdecrypt = aead_null_givdecrypt;
	}
	crt->child = __crypto_aead_cast(tfm);
	crt->authsize = alg->maxauthsize;

	return 0;
}

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

	if (crypto_old_aead_alg(aead)->encrypt)
		return crypto_old_aead_init_tfm(tfm);

	aead->setkey = alg->setkey;
	aead->setauthsize = alg->setauthsize;
	aead->encrypt = alg->encrypt;
	aead->decrypt = alg->decrypt;
	aead->child = __crypto_aead_cast(tfm);
	aead->authsize = alg->maxauthsize;

	if (alg->exit)
		aead->base.exit = crypto_aead_exit_tfm;

	if (alg->init)
		return alg->init(aead);

	return 0;
}

#ifdef CONFIG_NET
static int crypto_old_aead_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_aead raead;
	struct old_aead_alg *aead = &alg->cra_aead;

	strncpy(raead.type, "aead", sizeof(raead.type));
	strncpy(raead.geniv, aead->geniv ?: "<built-in>", sizeof(raead.geniv));

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
static int crypto_old_aead_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif

static void crypto_old_aead_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_old_aead_show(struct seq_file *m, struct crypto_alg *alg)
{
	struct old_aead_alg *aead = &alg->cra_aead;

	seq_printf(m, "type         : aead\n");
	seq_printf(m, "async        : %s\n", alg->cra_flags & CRYPTO_ALG_ASYNC ?
					     "yes" : "no");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "ivsize       : %u\n", aead->ivsize);
	seq_printf(m, "maxauthsize  : %u\n", aead->maxauthsize);
	seq_printf(m, "geniv        : %s\n", aead->geniv ?: "<built-in>");
}

const struct crypto_type crypto_aead_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_aead_init_tfm,
#ifdef CONFIG_PROC_FS
	.show = crypto_old_aead_show,
#endif
	.report = crypto_old_aead_report,
	.lookup = crypto_lookup_aead,
	.maskclear = ~(CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_GENIV),
	.maskset = CRYPTO_ALG_TYPE_MASK,
	.type = CRYPTO_ALG_TYPE_AEAD,
	.tfmsize = offsetof(struct crypto_aead, base),
};
EXPORT_SYMBOL_GPL(crypto_aead_type);

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
	__attribute__ ((unused));
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

static const struct crypto_type crypto_new_aead_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_aead_init_tfm,
#ifdef CONFIG_PROC_FS
	.show = crypto_aead_show,
#endif
	.report = crypto_aead_report,
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_MASK,
	.type = CRYPTO_ALG_TYPE_AEAD,
	.tfmsize = offsetof(struct crypto_aead, base),
};

static int aead_null_givencrypt(struct aead_givcrypt_request *req)
{
	return crypto_aead_encrypt(&req->areq);
}

static int aead_null_givdecrypt(struct aead_givcrypt_request *req)
{
	return crypto_aead_decrypt(&req->areq);
}

#ifdef CONFIG_NET
static int crypto_nivaead_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_aead raead;
	struct old_aead_alg *aead = &alg->cra_aead;

	strncpy(raead.type, "nivaead", sizeof(raead.type));
	strncpy(raead.geniv, aead->geniv, sizeof(raead.geniv));

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
static int crypto_nivaead_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif


static void crypto_nivaead_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_nivaead_show(struct seq_file *m, struct crypto_alg *alg)
{
	struct old_aead_alg *aead = &alg->cra_aead;

	seq_printf(m, "type         : nivaead\n");
	seq_printf(m, "async        : %s\n", alg->cra_flags & CRYPTO_ALG_ASYNC ?
					     "yes" : "no");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "ivsize       : %u\n", aead->ivsize);
	seq_printf(m, "maxauthsize  : %u\n", aead->maxauthsize);
	seq_printf(m, "geniv        : %s\n", aead->geniv);
}

const struct crypto_type crypto_nivaead_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_aead_init_tfm,
#ifdef CONFIG_PROC_FS
	.show = crypto_nivaead_show,
#endif
	.report = crypto_nivaead_report,
	.maskclear = ~(CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_GENIV),
	.maskset = CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_GENIV,
	.type = CRYPTO_ALG_TYPE_AEAD,
	.tfmsize = offsetof(struct crypto_aead, base),
};
EXPORT_SYMBOL_GPL(crypto_nivaead_type);

static int crypto_grab_nivaead(struct crypto_aead_spawn *spawn,
			       const char *name, u32 type, u32 mask)
{
	spawn->base.frontend = &crypto_nivaead_type;
	return crypto_grab_spawn(&spawn->base, name, type, mask);
}

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

static void compat_encrypt_complete2(struct aead_request *req, int err)
{
	struct compat_request_ctx *rctx = aead_request_ctx(req);
	struct aead_givcrypt_request *subreq = &rctx->subreq;
	struct crypto_aead *geniv;

	if (err == -EINPROGRESS)
		return;

	if (err)
		goto out;

	geniv = crypto_aead_reqtfm(req);
	scatterwalk_map_and_copy(subreq->giv, rctx->ivsg, 0,
				 crypto_aead_ivsize(geniv), 1);

out:
	kzfree(subreq->giv);
}

static void compat_encrypt_complete(struct crypto_async_request *base, int err)
{
	struct aead_request *req = base->data;

	compat_encrypt_complete2(req, err);
	aead_request_complete(req, err);
}

static int compat_encrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct aead_geniv_ctx *ctx = crypto_aead_ctx(geniv);
	struct compat_request_ctx *rctx = aead_request_ctx(req);
	struct aead_givcrypt_request *subreq = &rctx->subreq;
	unsigned int ivsize = crypto_aead_ivsize(geniv);
	struct scatterlist *src, *dst;
	crypto_completion_t compl;
	void *data;
	u8 *info;
	__be64 seq;
	int err;

	if (req->cryptlen < ivsize)
		return -EINVAL;

	compl = req->base.complete;
	data = req->base.data;

	rctx->ivsg = scatterwalk_ffwd(rctx->ivbuf, req->dst, req->assoclen);
	info = PageHighMem(sg_page(rctx->ivsg)) ? NULL : sg_virt(rctx->ivsg);

	if (!info) {
		info = kmalloc(ivsize, req->base.flags &
				       CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL:
								  GFP_ATOMIC);
		if (!info)
			return -ENOMEM;

		compl = compat_encrypt_complete;
		data = req;
	}

	memcpy(&seq, req->iv + ivsize - sizeof(seq), sizeof(seq));

	src = scatterwalk_ffwd(rctx->src, req->src, req->assoclen + ivsize);
	dst = req->src == req->dst ?
	      src : scatterwalk_ffwd(rctx->dst, rctx->ivsg, ivsize);

	aead_givcrypt_set_tfm(subreq, ctx->child);
	aead_givcrypt_set_callback(subreq, req->base.flags,
				   req->base.complete, req->base.data);
	aead_givcrypt_set_crypt(subreq, src, dst,
				req->cryptlen - ivsize, req->iv);
	aead_givcrypt_set_assoc(subreq, req->src, req->assoclen);
	aead_givcrypt_set_giv(subreq, info, be64_to_cpu(seq));

	err = crypto_aead_givencrypt(subreq);
	if (unlikely(PageHighMem(sg_page(rctx->ivsg))))
		compat_encrypt_complete2(req, err);
	return err;
}

static int compat_decrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct aead_geniv_ctx *ctx = crypto_aead_ctx(geniv);
	struct compat_request_ctx *rctx = aead_request_ctx(req);
	struct aead_request *subreq = &rctx->subreq.areq;
	unsigned int ivsize = crypto_aead_ivsize(geniv);
	struct scatterlist *src, *dst;
	crypto_completion_t compl;
	void *data;

	if (req->cryptlen < ivsize)
		return -EINVAL;

	aead_request_set_tfm(subreq, ctx->child);

	compl = req->base.complete;
	data = req->base.data;

	src = scatterwalk_ffwd(rctx->src, req->src, req->assoclen + ivsize);
	dst = req->src == req->dst ?
	      src : scatterwalk_ffwd(rctx->dst, req->dst,
				     req->assoclen + ivsize);

	aead_request_set_callback(subreq, req->base.flags, compl, data);
	aead_request_set_crypt(subreq, src, dst,
			       req->cryptlen - ivsize, req->iv);
	aead_request_set_assoc(subreq, req->src, req->assoclen);

	scatterwalk_map_and_copy(req->iv, req->src, req->assoclen, ivsize, 0);

	return crypto_aead_decrypt(subreq);
}

static int compat_encrypt_first(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct aead_geniv_ctx *ctx = crypto_aead_ctx(geniv);
	int err = 0;

	spin_lock_bh(&ctx->lock);
	if (geniv->encrypt != compat_encrypt_first)
		goto unlock;

	geniv->encrypt = compat_encrypt;

unlock:
	spin_unlock_bh(&ctx->lock);

	if (err)
		return err;

	return compat_encrypt(req);
}

static int aead_geniv_init_compat(struct crypto_tfm *tfm)
{
	struct crypto_aead *geniv = __crypto_aead_cast(tfm);
	struct aead_geniv_ctx *ctx = crypto_aead_ctx(geniv);
	int err;

	spin_lock_init(&ctx->lock);

	crypto_aead_set_reqsize(geniv, sizeof(struct compat_request_ctx));

	err = aead_geniv_init(tfm);

	ctx->child = geniv->child;
	geniv->child = geniv;

	return err;
}

static void aead_geniv_exit_compat(struct crypto_tfm *tfm)
{
	struct crypto_aead *geniv = __crypto_aead_cast(tfm);
	struct aead_geniv_ctx *ctx = crypto_aead_ctx(geniv);

	crypto_free_aead(ctx->child);
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

	if ((algt->type ^ (CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_GENIV)) &
	    algt->mask)
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
	err = (algt->mask & CRYPTO_ALG_GENIV) ?
	      crypto_grab_nivaead(spawn, name, type, mask) :
	      crypto_grab_aead(spawn, name, type, mask);
	if (err)
		goto err_free_inst;

	alg = crypto_spawn_aead_alg(spawn);

	ivsize = crypto_aead_alg_ivsize(alg);
	maxauthsize = crypto_aead_alg_maxauthsize(alg);

	err = -EINVAL;
	if (ivsize < sizeof(u64))
		goto err_drop_alg;

	/*
	 * This is only true if we're constructing an algorithm with its
	 * default IV generator.  For the default generator we elide the
	 * template name and double-check the IV generator.
	 */
	if (algt->mask & CRYPTO_ALG_GENIV) {
		if (!alg->base.cra_aead.encrypt)
			goto err_drop_alg;
		if (strcmp(tmpl->name, alg->base.cra_aead.geniv))
			goto err_drop_alg;

		memcpy(inst->alg.base.cra_name, alg->base.cra_name,
		       CRYPTO_MAX_ALG_NAME);
		memcpy(inst->alg.base.cra_driver_name,
		       alg->base.cra_driver_name, CRYPTO_MAX_ALG_NAME);

		inst->alg.base.cra_flags = CRYPTO_ALG_TYPE_AEAD |
					   CRYPTO_ALG_GENIV;
		inst->alg.base.cra_flags |= alg->base.cra_flags &
					    CRYPTO_ALG_ASYNC;
		inst->alg.base.cra_priority = alg->base.cra_priority;
		inst->alg.base.cra_blocksize = alg->base.cra_blocksize;
		inst->alg.base.cra_alignmask = alg->base.cra_alignmask;
		inst->alg.base.cra_type = &crypto_aead_type;

		inst->alg.base.cra_aead.ivsize = ivsize;
		inst->alg.base.cra_aead.maxauthsize = maxauthsize;

		inst->alg.base.cra_aead.setkey = alg->base.cra_aead.setkey;
		inst->alg.base.cra_aead.setauthsize =
			alg->base.cra_aead.setauthsize;
		inst->alg.base.cra_aead.encrypt = alg->base.cra_aead.encrypt;
		inst->alg.base.cra_aead.decrypt = alg->base.cra_aead.decrypt;

		goto out;
	}

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

	inst->alg.encrypt = compat_encrypt_first;
	inst->alg.decrypt = compat_decrypt;

	inst->alg.base.cra_init = aead_geniv_init_compat;
	inst->alg.base.cra_exit = aead_geniv_exit_compat;

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

int aead_geniv_init(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_aead *child;
	struct crypto_aead *aead;

	aead = __crypto_aead_cast(tfm);

	child = crypto_spawn_aead(crypto_instance_ctx(inst));
	if (IS_ERR(child))
		return PTR_ERR(child);

	aead->child = child;
	aead->reqsize += crypto_aead_reqsize(child);

	return 0;
}
EXPORT_SYMBOL_GPL(aead_geniv_init);

void aead_geniv_exit(struct crypto_tfm *tfm)
{
	crypto_free_aead(__crypto_aead_cast(tfm)->child);
}
EXPORT_SYMBOL_GPL(aead_geniv_exit);

static int crypto_nivaead_default(struct crypto_alg *alg, u32 type, u32 mask)
{
	struct rtattr *tb[3];
	struct {
		struct rtattr attr;
		struct crypto_attr_type data;
	} ptype;
	struct {
		struct rtattr attr;
		struct crypto_attr_alg data;
	} palg;
	struct crypto_template *tmpl;
	struct crypto_instance *inst;
	struct crypto_alg *larval;
	const char *geniv;
	int err;

	larval = crypto_larval_lookup(alg->cra_driver_name,
				      CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_GENIV,
				      CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_GENIV);
	err = PTR_ERR(larval);
	if (IS_ERR(larval))
		goto out;

	err = -EAGAIN;
	if (!crypto_is_larval(larval))
		goto drop_larval;

	ptype.attr.rta_len = sizeof(ptype);
	ptype.attr.rta_type = CRYPTOA_TYPE;
	ptype.data.type = type | CRYPTO_ALG_GENIV;
	/* GENIV tells the template that we're making a default geniv. */
	ptype.data.mask = mask | CRYPTO_ALG_GENIV;
	tb[0] = &ptype.attr;

	palg.attr.rta_len = sizeof(palg);
	palg.attr.rta_type = CRYPTOA_ALG;
	/* Must use the exact name to locate ourselves. */
	memcpy(palg.data.name, alg->cra_driver_name, CRYPTO_MAX_ALG_NAME);
	tb[1] = &palg.attr;

	tb[2] = NULL;

	geniv = alg->cra_aead.geniv;

	tmpl = crypto_lookup_template(geniv);
	err = -ENOENT;
	if (!tmpl)
		goto kill_larval;

	if (tmpl->create) {
		err = tmpl->create(tmpl, tb);
		if (err)
			goto put_tmpl;
		goto ok;
	}

	inst = tmpl->alloc(tb);
	err = PTR_ERR(inst);
	if (IS_ERR(inst))
		goto put_tmpl;

	err = crypto_register_instance(tmpl, inst);
	if (err) {
		tmpl->free(inst);
		goto put_tmpl;
	}

ok:
	/* Redo the lookup to use the instance we just registered. */
	err = -EAGAIN;

put_tmpl:
	crypto_tmpl_put(tmpl);
kill_larval:
	crypto_larval_kill(larval);
drop_larval:
	crypto_mod_put(larval);
out:
	crypto_mod_put(alg);
	return err;
}

struct crypto_alg *crypto_lookup_aead(const char *name, u32 type, u32 mask)
{
	struct crypto_alg *alg;

	alg = crypto_alg_mod_lookup(name, type, mask);
	if (IS_ERR(alg))
		return alg;

	if (alg->cra_type == &crypto_aead_type)
		return alg;

	if (!alg->cra_aead.ivsize)
		return alg;

	crypto_mod_put(alg);
	alg = crypto_alg_mod_lookup(name, type | CRYPTO_ALG_TESTED,
				    mask & ~CRYPTO_ALG_TESTED);
	if (IS_ERR(alg))
		return alg;

	if (alg->cra_type == &crypto_aead_type) {
		if (~alg->cra_flags & (type ^ ~mask) & CRYPTO_ALG_TESTED) {
			crypto_mod_put(alg);
			alg = ERR_PTR(-ENOENT);
		}
		return alg;
	}

	BUG_ON(!alg->cra_aead.ivsize);

	return ERR_PTR(crypto_nivaead_default(alg, type, mask));
}
EXPORT_SYMBOL_GPL(crypto_lookup_aead);

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

	if (max(alg->maxauthsize, alg->ivsize) > PAGE_SIZE / 8)
		return -EINVAL;

	base->cra_type = &crypto_new_aead_type;
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
