/*
 * pcrypt - Parallel crypto wrapper.
 *
 * Copyright (C) 2009 secunet Security Networks AG
 * Copyright (C) 2009 Steffen Klassert <steffen.klassert@secunet.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <crypto/algapi.h>
#include <crypto/internal/aead.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <crypto/pcrypt.h>

static struct padata_instance *pcrypt_enc_padata;
static struct padata_instance *pcrypt_dec_padata;
static struct workqueue_struct *encwq;
static struct workqueue_struct *decwq;

struct pcrypt_instance_ctx {
	struct crypto_spawn spawn;
	unsigned int tfm_count;
};

struct pcrypt_aead_ctx {
	struct crypto_aead *child;
	unsigned int cb_cpu;
};

static int pcrypt_do_parallel(struct padata_priv *padata, unsigned int *cb_cpu,
			      struct padata_instance *pinst)
{
	unsigned int cpu_index, cpu, i;

	cpu = *cb_cpu;

	if (cpumask_test_cpu(cpu, cpu_active_mask))
			goto out;

	cpu_index = cpu % cpumask_weight(cpu_active_mask);

	cpu = cpumask_first(cpu_active_mask);
	for (i = 0; i < cpu_index; i++)
		cpu = cpumask_next(cpu, cpu_active_mask);

	*cb_cpu = cpu;

out:
	return padata_do_parallel(pinst, padata, cpu);
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

static void pcrypt_aead_giv_serial(struct padata_priv *padata)
{
	struct pcrypt_request *preq = pcrypt_padata_request(padata);
	struct aead_givcrypt_request *req = pcrypt_request_ctx(preq);

	aead_request_complete(req->areq.base.data, padata->info);
}

static void pcrypt_aead_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;
	struct pcrypt_request *preq = aead_request_ctx(req);
	struct padata_priv *padata = pcrypt_request_padata(preq);

	padata->info = err;
	req->base.flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

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

	memset(padata, 0, sizeof(struct padata_priv));

	padata->parallel = pcrypt_aead_enc;
	padata->serial = pcrypt_aead_serial;

	aead_request_set_tfm(creq, ctx->child);
	aead_request_set_callback(creq, flags & ~CRYPTO_TFM_REQ_MAY_SLEEP,
				  pcrypt_aead_done, req);
	aead_request_set_crypt(creq, req->src, req->dst,
			       req->cryptlen, req->iv);
	aead_request_set_assoc(creq, req->assoc, req->assoclen);

	err = pcrypt_do_parallel(padata, &ctx->cb_cpu, pcrypt_enc_padata);
	if (err)
		return err;
	else
		err = crypto_aead_encrypt(creq);

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

	memset(padata, 0, sizeof(struct padata_priv));

	padata->parallel = pcrypt_aead_dec;
	padata->serial = pcrypt_aead_serial;

	aead_request_set_tfm(creq, ctx->child);
	aead_request_set_callback(creq, flags & ~CRYPTO_TFM_REQ_MAY_SLEEP,
				  pcrypt_aead_done, req);
	aead_request_set_crypt(creq, req->src, req->dst,
			       req->cryptlen, req->iv);
	aead_request_set_assoc(creq, req->assoc, req->assoclen);

	err = pcrypt_do_parallel(padata, &ctx->cb_cpu, pcrypt_dec_padata);
	if (err)
		return err;
	else
		err = crypto_aead_decrypt(creq);

	return err;
}

static void pcrypt_aead_givenc(struct padata_priv *padata)
{
	struct pcrypt_request *preq = pcrypt_padata_request(padata);
	struct aead_givcrypt_request *req = pcrypt_request_ctx(preq);

	padata->info = crypto_aead_givencrypt(req);

	if (padata->info == -EINPROGRESS)
		return;

	padata_do_serial(padata);
}

static int pcrypt_aead_givencrypt(struct aead_givcrypt_request *req)
{
	int err;
	struct aead_request *areq = &req->areq;
	struct pcrypt_request *preq = aead_request_ctx(areq);
	struct aead_givcrypt_request *creq = pcrypt_request_ctx(preq);
	struct padata_priv *padata = pcrypt_request_padata(preq);
	struct crypto_aead *aead = aead_givcrypt_reqtfm(req);
	struct pcrypt_aead_ctx *ctx = crypto_aead_ctx(aead);
	u32 flags = aead_request_flags(areq);

	memset(padata, 0, sizeof(struct padata_priv));

	padata->parallel = pcrypt_aead_givenc;
	padata->serial = pcrypt_aead_giv_serial;

	aead_givcrypt_set_tfm(creq, ctx->child);
	aead_givcrypt_set_callback(creq, flags & ~CRYPTO_TFM_REQ_MAY_SLEEP,
				   pcrypt_aead_done, areq);
	aead_givcrypt_set_crypt(creq, areq->src, areq->dst,
				areq->cryptlen, areq->iv);
	aead_givcrypt_set_assoc(creq, areq->assoc, areq->assoclen);
	aead_givcrypt_set_giv(creq, req->giv, req->seq);

	err = pcrypt_do_parallel(padata, &ctx->cb_cpu, pcrypt_enc_padata);
	if (err)
		return err;
	else
		err = crypto_aead_givencrypt(creq);

	return err;
}

static int pcrypt_aead_init_tfm(struct crypto_tfm *tfm)
{
	int cpu, cpu_index;
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct pcrypt_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct pcrypt_aead_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_aead *cipher;

	ictx->tfm_count++;

	cpu_index = ictx->tfm_count % cpumask_weight(cpu_active_mask);

	ctx->cb_cpu = cpumask_first(cpu_active_mask);
	for (cpu = 0; cpu < cpu_index; cpu++)
		ctx->cb_cpu = cpumask_next(ctx->cb_cpu, cpu_active_mask);

	cipher = crypto_spawn_aead(crypto_instance_ctx(inst));

	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;
	tfm->crt_aead.reqsize = sizeof(struct pcrypt_request)
		+ sizeof(struct aead_givcrypt_request)
		+ crypto_aead_reqsize(cipher);

	return 0;
}

static void pcrypt_aead_exit_tfm(struct crypto_tfm *tfm)
{
	struct pcrypt_aead_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_aead(ctx->child);
}

static struct crypto_instance *pcrypt_alloc_instance(struct crypto_alg *alg)
{
	struct crypto_instance *inst;
	struct pcrypt_instance_ctx *ctx;
	int err;

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst) {
		inst = ERR_PTR(-ENOMEM);
		goto out;
	}

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "pcrypt(%s)", alg->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto out_free_inst;

	memcpy(inst->alg.cra_name, alg->cra_name, CRYPTO_MAX_ALG_NAME);

	ctx = crypto_instance_ctx(inst);
	err = crypto_init_spawn(&ctx->spawn, alg, inst,
				CRYPTO_ALG_TYPE_MASK);
	if (err)
		goto out_free_inst;

	inst->alg.cra_priority = alg->cra_priority + 100;
	inst->alg.cra_blocksize = alg->cra_blocksize;
	inst->alg.cra_alignmask = alg->cra_alignmask;

out:
	return inst;

out_free_inst:
	kfree(inst);
	inst = ERR_PTR(err);
	goto out;
}

static struct crypto_instance *pcrypt_alloc_aead(struct rtattr **tb,
						 u32 type, u32 mask)
{
	struct crypto_instance *inst;
	struct crypto_alg *alg;

	alg = crypto_get_attr_alg(tb, type, (mask & CRYPTO_ALG_TYPE_MASK));
	if (IS_ERR(alg))
		return ERR_CAST(alg);

	inst = pcrypt_alloc_instance(alg);
	if (IS_ERR(inst))
		goto out_put_alg;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC;
	inst->alg.cra_type = &crypto_aead_type;

	inst->alg.cra_aead.ivsize = alg->cra_aead.ivsize;
	inst->alg.cra_aead.geniv = alg->cra_aead.geniv;
	inst->alg.cra_aead.maxauthsize = alg->cra_aead.maxauthsize;

	inst->alg.cra_ctxsize = sizeof(struct pcrypt_aead_ctx);

	inst->alg.cra_init = pcrypt_aead_init_tfm;
	inst->alg.cra_exit = pcrypt_aead_exit_tfm;

	inst->alg.cra_aead.setkey = pcrypt_aead_setkey;
	inst->alg.cra_aead.setauthsize = pcrypt_aead_setauthsize;
	inst->alg.cra_aead.encrypt = pcrypt_aead_encrypt;
	inst->alg.cra_aead.decrypt = pcrypt_aead_decrypt;
	inst->alg.cra_aead.givencrypt = pcrypt_aead_givencrypt;

out_put_alg:
	crypto_mod_put(alg);
	return inst;
}

static struct crypto_instance *pcrypt_alloc(struct rtattr **tb)
{
	struct crypto_attr_type *algt;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return ERR_CAST(algt);

	switch (algt->type & algt->mask & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_AEAD:
		return pcrypt_alloc_aead(tb, algt->type, algt->mask);
	}

	return ERR_PTR(-EINVAL);
}

static void pcrypt_free(struct crypto_instance *inst)
{
	struct pcrypt_instance_ctx *ctx = crypto_instance_ctx(inst);

	crypto_drop_spawn(&ctx->spawn);
	kfree(inst);
}

static struct crypto_template pcrypt_tmpl = {
	.name = "pcrypt",
	.alloc = pcrypt_alloc,
	.free = pcrypt_free,
	.module = THIS_MODULE,
};

static int __init pcrypt_init(void)
{
	encwq = create_workqueue("pencrypt");
	if (!encwq)
		goto err;

	decwq = create_workqueue("pdecrypt");
	if (!decwq)
		goto err_destroy_encwq;


	pcrypt_enc_padata = padata_alloc(cpu_possible_mask, encwq);
	if (!pcrypt_enc_padata)
		goto err_destroy_decwq;

	pcrypt_dec_padata = padata_alloc(cpu_possible_mask, decwq);
	if (!pcrypt_dec_padata)
		goto err_free_padata;

	padata_start(pcrypt_enc_padata);
	padata_start(pcrypt_dec_padata);

	return crypto_register_template(&pcrypt_tmpl);

err_free_padata:
	padata_free(pcrypt_enc_padata);

err_destroy_decwq:
	destroy_workqueue(decwq);

err_destroy_encwq:
	destroy_workqueue(encwq);

err:
	return -ENOMEM;
}

static void __exit pcrypt_exit(void)
{
	padata_stop(pcrypt_enc_padata);
	padata_stop(pcrypt_dec_padata);

	destroy_workqueue(encwq);
	destroy_workqueue(decwq);

	padata_free(pcrypt_enc_padata);
	padata_free(pcrypt_dec_padata);

	crypto_unregister_template(&pcrypt_tmpl);
}

module_init(pcrypt_init);
module_exit(pcrypt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Steffen Klassert <steffen.klassert@secunet.com>");
MODULE_DESCRIPTION("Parallel crypto wrapper");
