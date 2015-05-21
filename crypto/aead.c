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

#include <crypto/internal/aead.h>
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

static int aead_null_givencrypt(struct aead_givcrypt_request *req);
static int aead_null_givdecrypt(struct aead_givcrypt_request *req);

static int setkey_unaligned(struct crypto_aead *tfm, const u8 *key,
			    unsigned int keylen)
{
	struct old_aead_alg *aead = crypto_old_aead_alg(tfm);
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
	ret = aead->setkey(tfm, alignbuffer, keylen);
	memset(alignbuffer, 0, keylen);
	kfree(buffer);
	return ret;
}

int crypto_aead_setkey(struct crypto_aead *tfm,
		       const u8 *key, unsigned int keylen)
{
	struct old_aead_alg *aead = crypto_old_aead_alg(tfm);
	unsigned long alignmask = crypto_aead_alignmask(tfm);

	tfm = tfm->child;

	if ((unsigned long)key & alignmask)
		return setkey_unaligned(tfm, key, keylen);

	return aead->setkey(tfm, key, keylen);
}
EXPORT_SYMBOL_GPL(crypto_aead_setkey);

int crypto_aead_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	int err;

	if (authsize > crypto_old_aead_alg(tfm)->maxauthsize)
		return -EINVAL;

	if (crypto_old_aead_alg(tfm)->setauthsize) {
		err = crypto_old_aead_alg(tfm)->setauthsize(
			tfm->child, authsize);
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

	src = scatterwalk_ffwd(nreq->srcbuf, req->src,
			       req->assoclen + req->cryptoff);
	dst = scatterwalk_ffwd(nreq->dstbuf, req->dst,
			       req->assoclen + req->cryptoff);

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

static int crypto_aead_init_tfm(struct crypto_tfm *tfm)
{
	struct old_aead_alg *alg = &tfm->__crt_alg->cra_aead;
	struct crypto_aead *crt = __crypto_aead_cast(tfm);

	if (max(alg->maxauthsize, alg->ivsize) > PAGE_SIZE / 8)
		return -EINVAL;

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
	crt->ivsize = alg->ivsize;
	crt->authsize = alg->maxauthsize;

	return 0;
}

#ifdef CONFIG_NET
static int crypto_aead_report(struct sk_buff *skb, struct crypto_alg *alg)
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
static int crypto_aead_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif

static void crypto_aead_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_aead_show(struct seq_file *m, struct crypto_alg *alg)
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
	.show = crypto_aead_show,
#endif
	.report = crypto_aead_report,
	.lookup = crypto_lookup_aead,
	.maskclear = ~(CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_GENIV),
	.maskset = CRYPTO_ALG_TYPE_MASK,
	.type = CRYPTO_ALG_TYPE_AEAD,
	.tfmsize = offsetof(struct crypto_aead, base),
};
EXPORT_SYMBOL_GPL(crypto_aead_type);

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

struct crypto_instance *aead_geniv_alloc(struct crypto_template *tmpl,
					 struct rtattr **tb, u32 type,
					 u32 mask)
{
	const char *name;
	struct crypto_aead_spawn *spawn;
	struct crypto_attr_type *algt;
	struct crypto_instance *inst;
	struct crypto_alg *alg;
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

	spawn = crypto_instance_ctx(inst);

	/* Ignore async algorithms if necessary. */
	mask |= crypto_requires_sync(algt->type, algt->mask);

	crypto_set_aead_spawn(spawn, inst);
	err = crypto_grab_nivaead(spawn, name, type, mask);
	if (err)
		goto err_free_inst;

	alg = crypto_aead_spawn_alg(spawn);

	err = -EINVAL;
	if (!alg->cra_aead.ivsize)
		goto err_drop_alg;

	/*
	 * This is only true if we're constructing an algorithm with its
	 * default IV generator.  For the default generator we elide the
	 * template name and double-check the IV generator.
	 */
	if (algt->mask & CRYPTO_ALG_GENIV) {
		if (strcmp(tmpl->name, alg->cra_aead.geniv))
			goto err_drop_alg;

		memcpy(inst->alg.cra_name, alg->cra_name, CRYPTO_MAX_ALG_NAME);
		memcpy(inst->alg.cra_driver_name, alg->cra_driver_name,
		       CRYPTO_MAX_ALG_NAME);
	} else {
		err = -ENAMETOOLONG;
		if (snprintf(inst->alg.cra_name, CRYPTO_MAX_ALG_NAME,
			     "%s(%s)", tmpl->name, alg->cra_name) >=
		    CRYPTO_MAX_ALG_NAME)
			goto err_drop_alg;
		if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
			     "%s(%s)", tmpl->name, alg->cra_driver_name) >=
		    CRYPTO_MAX_ALG_NAME)
			goto err_drop_alg;
	}

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_GENIV;
	inst->alg.cra_flags |= alg->cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = alg->cra_blocksize;
	inst->alg.cra_alignmask = alg->cra_alignmask;
	inst->alg.cra_type = &crypto_aead_type;

	inst->alg.cra_aead.ivsize = alg->cra_aead.ivsize;
	inst->alg.cra_aead.maxauthsize = alg->cra_aead.maxauthsize;
	inst->alg.cra_aead.geniv = alg->cra_aead.geniv;

	inst->alg.cra_aead.setkey = alg->cra_aead.setkey;
	inst->alg.cra_aead.setauthsize = alg->cra_aead.setauthsize;
	inst->alg.cra_aead.encrypt = alg->cra_aead.encrypt;
	inst->alg.cra_aead.decrypt = alg->cra_aead.decrypt;

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

void aead_geniv_free(struct crypto_instance *inst)
{
	crypto_drop_aead(crypto_instance_ctx(inst));
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

	inst = tmpl->alloc(tb);
	err = PTR_ERR(inst);
	if (IS_ERR(inst))
		goto put_tmpl;

	err = crypto_register_instance(tmpl, inst);
	if (err) {
		tmpl->free(inst);
		goto put_tmpl;
	}

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

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Authenticated Encryption with Associated Data (AEAD)");
