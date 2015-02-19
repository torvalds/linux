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
#include <linux/notifier.h>
#include <linux/kobject.h>
#include <linux/cpu.h>
#include <crypto/pcrypt.h>

struct padata_pcrypt {
	struct padata_instance *pinst;
	struct workqueue_struct *wq;

	/*
	 * Cpumask for callback CPUs. It should be
	 * equal to serial cpumask of corresponding padata instance,
	 * so it is updated when padata notifies us about serial
	 * cpumask change.
	 *
	 * cb_cpumask is protected by RCU. This fact prevents us from
	 * using cpumask_var_t directly because the actual type of
	 * cpumsak_var_t depends on kernel configuration(particularly on
	 * CONFIG_CPUMASK_OFFSTACK macro). Depending on the configuration
	 * cpumask_var_t may be either a pointer to the struct cpumask
	 * or a variable allocated on the stack. Thus we can not safely use
	 * cpumask_var_t with RCU operations such as rcu_assign_pointer or
	 * rcu_dereference. So cpumask_var_t is wrapped with struct
	 * pcrypt_cpumask which makes possible to use it with RCU.
	 */
	struct pcrypt_cpumask {
		cpumask_var_t mask;
	} *cb_cpumask;
	struct notifier_block nblock;
};

static struct padata_pcrypt pencrypt;
static struct padata_pcrypt pdecrypt;
static struct kset           *pcrypt_kset;

struct pcrypt_instance_ctx {
	struct crypto_spawn spawn;
	unsigned int tfm_count;
};

struct pcrypt_aead_ctx {
	struct crypto_aead *child;
	unsigned int cb_cpu;
};

static int pcrypt_do_parallel(struct padata_priv *padata, unsigned int *cb_cpu,
			      struct padata_pcrypt *pcrypt)
{
	unsigned int cpu_index, cpu, i;
	struct pcrypt_cpumask *cpumask;

	cpu = *cb_cpu;

	rcu_read_lock_bh();
	cpumask = rcu_dereference_bh(pcrypt->cb_cpumask);
	if (cpumask_test_cpu(cpu, cpumask->mask))
			goto out;

	if (!cpumask_weight(cpumask->mask))
			goto out;

	cpu_index = cpu % cpumask_weight(cpumask->mask);

	cpu = cpumask_first(cpumask->mask);
	for (i = 0; i < cpu_index; i++)
		cpu = cpumask_next(cpu, cpumask->mask);

	*cb_cpu = cpu;

out:
	rcu_read_unlock_bh();
	return padata_do_parallel(pcrypt->pinst, padata, cpu);
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

	err = pcrypt_do_parallel(padata, &ctx->cb_cpu, &pencrypt);
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

	memset(padata, 0, sizeof(struct padata_priv));

	padata->parallel = pcrypt_aead_dec;
	padata->serial = pcrypt_aead_serial;

	aead_request_set_tfm(creq, ctx->child);
	aead_request_set_callback(creq, flags & ~CRYPTO_TFM_REQ_MAY_SLEEP,
				  pcrypt_aead_done, req);
	aead_request_set_crypt(creq, req->src, req->dst,
			       req->cryptlen, req->iv);
	aead_request_set_assoc(creq, req->assoc, req->assoclen);

	err = pcrypt_do_parallel(padata, &ctx->cb_cpu, &pdecrypt);
	if (!err)
		return -EINPROGRESS;

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

	err = pcrypt_do_parallel(padata, &ctx->cb_cpu, &pencrypt);
	if (!err)
		return -EINPROGRESS;

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

	cpu_index = ictx->tfm_count % cpumask_weight(cpu_online_mask);

	ctx->cb_cpu = cpumask_first(cpu_online_mask);
	for (cpu = 0; cpu < cpu_index; cpu++)
		ctx->cb_cpu = cpumask_next(ctx->cb_cpu, cpu_online_mask);

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

static int pcrypt_cpumask_change_notify(struct notifier_block *self,
					unsigned long val, void *data)
{
	struct padata_pcrypt *pcrypt;
	struct pcrypt_cpumask *new_mask, *old_mask;
	struct padata_cpumask *cpumask = (struct padata_cpumask *)data;

	if (!(val & PADATA_CPU_SERIAL))
		return 0;

	pcrypt = container_of(self, struct padata_pcrypt, nblock);
	new_mask = kmalloc(sizeof(*new_mask), GFP_KERNEL);
	if (!new_mask)
		return -ENOMEM;
	if (!alloc_cpumask_var(&new_mask->mask, GFP_KERNEL)) {
		kfree(new_mask);
		return -ENOMEM;
	}

	old_mask = pcrypt->cb_cpumask;

	cpumask_copy(new_mask->mask, cpumask->cbcpu);
	rcu_assign_pointer(pcrypt->cb_cpumask, new_mask);
	synchronize_rcu_bh();

	free_cpumask_var(old_mask->mask);
	kfree(old_mask);
	return 0;
}

static int pcrypt_sysfs_add(struct padata_instance *pinst, const char *name)
{
	int ret;

	pinst->kobj.kset = pcrypt_kset;
	ret = kobject_add(&pinst->kobj, NULL, name);
	if (!ret)
		kobject_uevent(&pinst->kobj, KOBJ_ADD);

	return ret;
}

static int pcrypt_init_padata(struct padata_pcrypt *pcrypt,
			      const char *name)
{
	int ret = -ENOMEM;
	struct pcrypt_cpumask *mask;

	get_online_cpus();

	pcrypt->wq = alloc_workqueue("%s", WQ_MEM_RECLAIM | WQ_CPU_INTENSIVE,
				     1, name);
	if (!pcrypt->wq)
		goto err;

	pcrypt->pinst = padata_alloc_possible(pcrypt->wq);
	if (!pcrypt->pinst)
		goto err_destroy_workqueue;

	mask = kmalloc(sizeof(*mask), GFP_KERNEL);
	if (!mask)
		goto err_free_padata;
	if (!alloc_cpumask_var(&mask->mask, GFP_KERNEL)) {
		kfree(mask);
		goto err_free_padata;
	}

	cpumask_and(mask->mask, cpu_possible_mask, cpu_online_mask);
	rcu_assign_pointer(pcrypt->cb_cpumask, mask);

	pcrypt->nblock.notifier_call = pcrypt_cpumask_change_notify;
	ret = padata_register_cpumask_notifier(pcrypt->pinst, &pcrypt->nblock);
	if (ret)
		goto err_free_cpumask;

	ret = pcrypt_sysfs_add(pcrypt->pinst, name);
	if (ret)
		goto err_unregister_notifier;

	put_online_cpus();

	return ret;

err_unregister_notifier:
	padata_unregister_cpumask_notifier(pcrypt->pinst, &pcrypt->nblock);
err_free_cpumask:
	free_cpumask_var(mask->mask);
	kfree(mask);
err_free_padata:
	padata_free(pcrypt->pinst);
err_destroy_workqueue:
	destroy_workqueue(pcrypt->wq);
err:
	put_online_cpus();

	return ret;
}

static void pcrypt_fini_padata(struct padata_pcrypt *pcrypt)
{
	free_cpumask_var(pcrypt->cb_cpumask->mask);
	kfree(pcrypt->cb_cpumask);

	padata_stop(pcrypt->pinst);
	padata_unregister_cpumask_notifier(pcrypt->pinst, &pcrypt->nblock);
	destroy_workqueue(pcrypt->wq);
	padata_free(pcrypt->pinst);
}

static struct crypto_template pcrypt_tmpl = {
	.name = "pcrypt",
	.alloc = pcrypt_alloc,
	.free = pcrypt_free,
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

	padata_start(pencrypt.pinst);
	padata_start(pdecrypt.pinst);

	return crypto_register_template(&pcrypt_tmpl);

err_deinit_pencrypt:
	pcrypt_fini_padata(&pencrypt);
err_unreg_kset:
	kset_unregister(pcrypt_kset);
err:
	return err;
}

static void __exit pcrypt_exit(void)
{
	pcrypt_fini_padata(&pencrypt);
	pcrypt_fini_padata(&pdecrypt);

	kset_unregister(pcrypt_kset);
	crypto_unregister_template(&pcrypt_tmpl);
}

module_init(pcrypt_init);
module_exit(pcrypt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Steffen Klassert <steffen.klassert@secunet.com>");
MODULE_DESCRIPTION("Parallel crypto wrapper");
MODULE_ALIAS_CRYPTO("pcrypt");
