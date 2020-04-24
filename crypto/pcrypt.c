// SPDX-License-Identifier: GPL-2.0-only
/*
 * pcrypt - Parallel crypto wrapper.
 *
 * Copyright (C) 2009 secunet Security Networks AG
 * Copyright (C) 2009 Steffen Klassert <steffen.klassert@secunet.com>
 */

#include <crypto/algapi.h>
#include <crypto/internal/aead.h>
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kobject.h>
#include <linux/cpu.h>
#include <crypto/pcrypt.h>

static struct padata_instance *pencrypt;
static struct padata_instance *pdecrypt;
static struct kset           *pcrypt_kset;

struct pcrypt_instance_ctx {
	struct crypto_aead_spawn spawn;
	struct padata_shell *psenc;
	struct padata_shell *psdec;
	atomic_t tfm_count;
};

struct pcrypt_aead_ctx {
	struct crypto_aead *child;
	unsigned int cb_cpu;
};

static inline struct pcrypt_instance_ctx *pcrypt_tfm_ictx(
	struct crypto_aead *tfm)
{
	return aead_instance_ctx(aead_alg_instance(tfm));
}

static int pcrypt_aead_setkey(struct crypto_aead *parent,
			      const u8 *key, unsigned int keylen)
{
	struct pcrypt_aead_ctx *ctx = crypto_aead_ctx(parent);

	return crypto_aead_setkey(ctx->child, key, keylen);
}

static int pcrypt_aead_setauthsize(struct crypto_aead *parent,
				   unsigned int authsize)
{
	struct pcrypt_aead_ctx *ctx = crypto_aead_ctx(parent);

	return crypto_aead_setauthsize(ctx->child, authsize);
}

static void pcrypt_aead_serial(struct padata_priv *padata)
{
	struct pcrypt_request *preq = pcrypt_padata_request(padata);
	struct aead_request *req = pcrypt_request_ctx(preq);

	aead_request_complete(req->base.data, padata->info);
}

static void pcrypt_aead_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;
	struct pcrypt_request *preq = aead_request_ctx(req);
	struct padata_priv *padata = pcrypt_request_padata(preq);

	padata->info = err;

	padata_do_serial(padata);
}

static void pcrypt_aead_enc(struct padata_priv *padata)
{
	struct pcrypt_request *preq = pcrypt_padata_request(padata);
	struct aead_request *req = pcrypt_request_ctx(preq);

	padata->info = crypto_aead_encrypt(req);

	if (padata->info == -EINPROGRESS)
		return;

	padata_do_serial(padata);
}

static int pcrypt_aead_encrypt(struct aead_request *req)
{
	int err;
	struct pcrypt_request *preq = aead_request_ctx(req);
	struct aead_request *creq = pcrypt_request_ctx(preq);
	struct padata_priv *padata = pcrypt_request_padata(preq);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct pcrypt_aead_ctx *ctx = crypto_aead_ctx(aead);
	u32 flags = aead_request_flags(req);
	struct pcrypt_instance_ctx *ictx;

	ictx = pcrypt_tfm_ictx(aead);

	memset(padata, 0, sizeof(struct padata_priv));

	padata->parallel = pcrypt_aead_enc;
	padata->serial = pcrypt_aead_serial;

	aead_request_set_tfm(creq, ctx->child);
	aead_request_set_callback(creq, flags & ~CRYPTO_TFM_REQ_MAY_SLEEP,
				  pcrypt_aead_done, req);
	aead_request_set_crypt(creq, req->src, req->dst,
			       req->cryptlen, req->iv);
	aead_request_set_ad(creq, req->assoclen);

	err = padata_do_parallel(ictx->psenc, padata, &ctx->cb_cpu);
	if (!err)
		return -EINPROGRESS;

	return err;
}

static void pcrypt_aead_dec(struct padata_priv *padata)
{
	struct pcrypt_request *preq = pcrypt_padata_request(padata);
	struct aead_request *req = pcrypt_request_ctx(preq);

	padata->info = crypto_aead_decrypt(req);

	if (padata->info == -EINPROGRESS)
		return;

	padata_do_serial(padata);
}

static int pcrypt_aead_decrypt(struct aead_request *req)
{
	int err;
	struct pcrypt_request *preq = aead_request_ctx(req);
	struct aead_request *creq = pcrypt_request_ctx(preq);
	struct padata_priv *padata = pcrypt_request_padata(preq);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct pcrypt_aead_ctx *ctx = crypto_aead_ctx(aead);
	u32 flags = aead_request_flags(req);
	struct pcrypt_instance_ctx *ictx;

	ictx = pcrypt_tfm_ictx(aead);

	memset(padata, 0, sizeof(struct padata_priv));

	padata->parallel = pcrypt_aead_dec;
	padata->serial = pcrypt_aead_serial;

	aead_request_set_tfm(creq, ctx->child);
	aead_request_set_callback(creq, flags & ~CRYPTO_TFM_REQ_MAY_SLEEP,
				  pcrypt_aead_done, req);
	aead_request_set_crypt(creq, req->src, req->dst,
			       req->cryptlen, req->iv);
	aead_request_set_ad(creq, req->assoclen);

	err = padata_do_parallel(ictx->psdec, padata, &ctx->cb_cpu);
	if (!err)
		return -EINPROGRESS;

	return err;
}

static int pcrypt_aead_init_tfm(struct crypto_aead *tfm)
{
	int cpu, cpu_index;
	struct aead_instance *inst = aead_alg_instance(tfm);
	struct pcrypt_instance_ctx *ictx = aead_instance_ctx(inst);
	struct pcrypt_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_aead *cipher;

	cpu_index = (unsigned int)atomic_inc_return(&ictx->tfm_count) %
		    cpumask_weight(cpu_online_mask);

	ctx->cb_cpu = cpumask_first(cpu_online_mask);
	for (cpu = 0; cpu < cpu_index; cpu++)
		ctx->cb_cpu = cpumask_next(ctx->cb_cpu, cpu_online_mask);

	cipher = crypto_spawn_aead(&ictx->spawn);

	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;
	crypto_aead_set_reqsize(tfm, sizeof(struct pcrypt_request) +
				     sizeof(struct aead_request) +
				     crypto_aead_reqsize(cipher));

	return 0;
}

static void pcrypt_aead_exit_tfm(struct crypto_aead *tfm)
{
	struct pcrypt_aead_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_aead(ctx->child);
}

static void pcrypt_free(struct aead_instance *inst)
{
	struct pcrypt_instance_ctx *ctx = aead_instance_ctx(inst);

	crypto_drop_aead(&ctx->spawn);
	padata_free_shell(ctx->psdec);
	padata_free_shell(ctx->psenc);
	kfree(inst);
}

static int pcrypt_init_instance(struct crypto_instance *inst,
				struct crypto_alg *alg)
{
	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "pcrypt(%s)", alg->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	memcpy(inst->alg.cra_name, alg->cra_name, CRYPTO_MAX_ALG_NAME);

	inst->alg.cra_priority = alg->cra_priority + 100;
	inst->alg.cra_blocksize = alg->cra_blocksize;
	inst->alg.cra_alignmask = alg->cra_alignmask;

	return 0;
}

static int pcrypt_create_aead(struct crypto_template *tmpl, struct rtattr **tb,
			      u32 type, u32 mask)
{
	struct pcrypt_instance_ctx *ctx;
	struct crypto_attr_type *algt;
	struct aead_instance *inst;
	struct aead_alg *alg;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	err = -ENOMEM;

	ctx = aead_instance_ctx(inst);
	ctx->psenc = padata_alloc_shell(pencrypt);
	if (!ctx->psenc)
		goto err_free_inst;

	ctx->psdec = padata_alloc_shell(pdecrypt);
	if (!ctx->psdec)
		goto err_free_inst;

	err = crypto_grab_aead(&ctx->spawn, aead_crypto_instance(inst),
			       crypto_attr_alg_name(tb[1]), 0, 0);
	if (err)
		goto err_free_inst;

	alg = crypto_spawn_aead_alg(&ctx->spawn);
	err = pcrypt_init_instance(aead_crypto_instance(inst), &alg->base);
	if (err)
		goto err_free_inst;

	inst->alg.base.cra_flags = CRYPTO_ALG_ASYNC;

	inst->alg.ivsize = crypto_aead_alg_ivsize(alg);
	inst->alg.maxauthsize = crypto_aead_alg_maxauthsize(alg);

	inst->alg.base.cra_ctxsize = sizeof(struct pcrypt_aead_ctx);

	inst->alg.init = pcrypt_aead_init_tfm;
	inst->alg.exit = pcrypt_aead_exit_tfm;

	inst->alg.setkey = pcrypt_aead_setkey;
	inst->alg.setauthsize = pcrypt_aead_setauthsize;
	inst->alg.encrypt = pcrypt_aead_encrypt;
	inst->alg.decrypt = pcrypt_aead_decrypt;

	inst->free = pcrypt_free;

	err = aead_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		pcrypt_free(inst);
	}
	return err;
}

static int pcrypt_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct crypto_attr_type *algt;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	switch (algt->type & algt->mask & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_AEAD:
		return pcrypt_create_aead(tmpl, tb, algt->type, algt->mask);
	}

	return -EINVAL;
}

static int pcrypt_sysfs_add(struct padata_instance *pinst, const char *name)
{
	int ret;

	pinst->kobj.kset = pcrypt_kset;
	ret = kobject_add(&pinst->kobj, NULL, "%s", name);
	if (!ret)
		kobject_uevent(&pinst->kobj, KOBJ_ADD);

	return ret;
}

static int pcrypt_init_padata(struct padata_instance **pinst, const char *name)
{
	int ret = -ENOMEM;

	*pinst = padata_alloc_possible(name);
	if (!*pinst)
		return ret;

	ret = pcrypt_sysfs_add(*pinst, name);
	if (ret)
		padata_free(*pinst);

	return ret;
}

static void pcrypt_fini_padata(struct padata_instance *pinst)
{
	padata_stop(pinst);
	padata_free(pinst);
}

static struct crypto_template pcrypt_tmpl = {
	.name = "pcrypt",
	.create = pcrypt_create,
	.module = THIS_MODULE,
};

static int __init pcrypt_init(void)
{
	int err = -ENOMEM;

	pcrypt_kset = kset_create_and_add("pcrypt", NULL, kernel_kobj);
	if (!pcrypt_kset)
		goto err;

	err = pcrypt_init_padata(&pencrypt, "pencrypt");
	if (err)
		goto err_unreg_kset;

	err = pcrypt_init_padata(&pdecrypt, "pdecrypt");
	if (err)
		goto err_deinit_pencrypt;

	padata_start(pencrypt);
	padata_start(pdecrypt);

	return crypto_register_template(&pcrypt_tmpl);

err_deinit_pencrypt:
	pcrypt_fini_padata(pencrypt);
err_unreg_kset:
	kset_unregister(pcrypt_kset);
err:
	return err;
}

static void __exit pcrypt_exit(void)
{
	crypto_unregister_template(&pcrypt_tmpl);

	pcrypt_fini_padata(pencrypt);
	pcrypt_fini_padata(pdecrypt);

	kset_unregister(pcrypt_kset);
}

subsys_initcall(pcrypt_init);
module_exit(pcrypt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Steffen Klassert <steffen.klassert@secunet.com>");
MODULE_DESCRIPTION("Parallel crypto wrapper");
MODULE_ALIAS_CRYPTO("pcrypt");
