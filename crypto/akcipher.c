// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Public Key Encryption
 *
 * Copyright (c) 2015, Intel Corporation
 * Authors: Tadeusz Struk <tadeusz.struk@intel.com>
 */
#include <crypto/internal/akcipher.h>
#include <linux/cryptouser.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <net/netlink.h>

#include "internal.h"

#define CRYPTO_ALG_TYPE_AHASH_MASK	0x0000000e

static int __maybe_unused crypto_akcipher_report(
	struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_akcipher rakcipher;

	memset(&rakcipher, 0, sizeof(rakcipher));

	strscpy(rakcipher.type, "akcipher", sizeof(rakcipher.type));

	return nla_put(skb, CRYPTOCFGA_REPORT_AKCIPHER,
		       sizeof(rakcipher), &rakcipher);
}

static void crypto_akcipher_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;

static void crypto_akcipher_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_puts(m, "type         : akcipher\n");
}

static void crypto_akcipher_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_akcipher *akcipher = __crypto_akcipher_tfm(tfm);
	struct akcipher_alg *alg = crypto_akcipher_alg(akcipher);

	alg->exit(akcipher);
}

static int crypto_akcipher_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_akcipher *akcipher = __crypto_akcipher_tfm(tfm);
	struct akcipher_alg *alg = crypto_akcipher_alg(akcipher);

	if (alg->exit)
		akcipher->base.exit = crypto_akcipher_exit_tfm;

	if (alg->init)
		return alg->init(akcipher);

	return 0;
}

static void crypto_akcipher_free_instance(struct crypto_instance *inst)
{
	struct akcipher_instance *akcipher = akcipher_instance(inst);

	akcipher->free(akcipher);
}

static const struct crypto_type crypto_akcipher_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_akcipher_init_tfm,
	.free = crypto_akcipher_free_instance,
#ifdef CONFIG_PROC_FS
	.show = crypto_akcipher_show,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_USER)
	.report = crypto_akcipher_report,
#endif
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_AHASH_MASK,
	.type = CRYPTO_ALG_TYPE_AKCIPHER,
	.tfmsize = offsetof(struct crypto_akcipher, base),
};

int crypto_grab_akcipher(struct crypto_akcipher_spawn *spawn,
			 struct crypto_instance *inst,
			 const char *name, u32 type, u32 mask)
{
	spawn->base.frontend = &crypto_akcipher_type;
	return crypto_grab_spawn(&spawn->base, inst, name, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_grab_akcipher);

struct crypto_akcipher *crypto_alloc_akcipher(const char *alg_name, u32 type,
					      u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_akcipher_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_akcipher);

static void akcipher_prepare_alg(struct akcipher_alg *alg)
{
	struct crypto_alg *base = &alg->base;

	base->cra_type = &crypto_akcipher_type;
	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
	base->cra_flags |= CRYPTO_ALG_TYPE_AKCIPHER;
}

static int akcipher_default_op(struct akcipher_request *req)
{
	return -ENOSYS;
}

static int akcipher_default_set_key(struct crypto_akcipher *tfm,
				     const void *key, unsigned int keylen)
{
	return -ENOSYS;
}

int crypto_register_akcipher(struct akcipher_alg *alg)
{
	struct crypto_alg *base = &alg->base;

	if (!alg->sign)
		alg->sign = akcipher_default_op;
	if (!alg->verify)
		alg->verify = akcipher_default_op;
	if (!alg->encrypt)
		alg->encrypt = akcipher_default_op;
	if (!alg->decrypt)
		alg->decrypt = akcipher_default_op;
	if (!alg->set_priv_key)
		alg->set_priv_key = akcipher_default_set_key;

	akcipher_prepare_alg(alg);
	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_akcipher);

void crypto_unregister_akcipher(struct akcipher_alg *alg)
{
	crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_akcipher);

int akcipher_register_instance(struct crypto_template *tmpl,
			       struct akcipher_instance *inst)
{
	if (WARN_ON(!inst->free))
		return -EINVAL;
	akcipher_prepare_alg(&inst->alg);
	return crypto_register_instance(tmpl, akcipher_crypto_instance(inst));
}
EXPORT_SYMBOL_GPL(akcipher_register_instance);

int crypto_akcipher_sync_prep(struct crypto_akcipher_sync_data *data)
{
	unsigned int reqsize = crypto_akcipher_reqsize(data->tfm);
	struct akcipher_request *req;
	struct scatterlist *sg;
	unsigned int mlen;
	unsigned int len;
	u8 *buf;

	if (data->dst)
		mlen = max(data->slen, data->dlen);
	else
		mlen = data->slen + data->dlen;

	len = sizeof(*req) + reqsize + mlen;
	if (len < mlen)
		return -EOVERFLOW;

	req = kzalloc(len, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	data->req = req;
	akcipher_request_set_tfm(req, data->tfm);

	buf = (u8 *)(req + 1) + reqsize;
	data->buf = buf;
	memcpy(buf, data->src, data->slen);

	sg = &data->sg;
	sg_init_one(sg, buf, mlen);
	akcipher_request_set_crypt(req, sg, data->dst ? sg : NULL,
				   data->slen, data->dlen);

	crypto_init_wait(&data->cwait);
	akcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, &data->cwait);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_akcipher_sync_prep);

int crypto_akcipher_sync_post(struct crypto_akcipher_sync_data *data, int err)
{
	err = crypto_wait_req(err, &data->cwait);
	if (data->dst)
		memcpy(data->dst, data->buf, data->dlen);
	data->dlen = data->req->dst_len;
	kfree_sensitive(data->req);
	return err;
}
EXPORT_SYMBOL_GPL(crypto_akcipher_sync_post);

int crypto_akcipher_sync_encrypt(struct crypto_akcipher *tfm,
				 const void *src, unsigned int slen,
				 void *dst, unsigned int dlen)
{
	struct crypto_akcipher_sync_data data = {
		.tfm = tfm,
		.src = src,
		.dst = dst,
		.slen = slen,
		.dlen = dlen,
	};

	return crypto_akcipher_sync_prep(&data) ?:
	       crypto_akcipher_sync_post(&data,
					 crypto_akcipher_encrypt(data.req));
}
EXPORT_SYMBOL_GPL(crypto_akcipher_sync_encrypt);

int crypto_akcipher_sync_decrypt(struct crypto_akcipher *tfm,
				 const void *src, unsigned int slen,
				 void *dst, unsigned int dlen)
{
	struct crypto_akcipher_sync_data data = {
		.tfm = tfm,
		.src = src,
		.dst = dst,
		.slen = slen,
		.dlen = dlen,
	};

	return crypto_akcipher_sync_prep(&data) ?:
	       crypto_akcipher_sync_post(&data,
					 crypto_akcipher_decrypt(data.req)) ?:
	       data.dlen;
}
EXPORT_SYMBOL_GPL(crypto_akcipher_sync_decrypt);

static void crypto_exit_akcipher_ops_sig(struct crypto_tfm *tfm)
{
	struct crypto_akcipher **ctx = crypto_tfm_ctx(tfm);

	crypto_free_akcipher(*ctx);
}

int crypto_init_akcipher_ops_sig(struct crypto_tfm *tfm)
{
	struct crypto_akcipher **ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *calg = tfm->__crt_alg;
	struct crypto_akcipher *akcipher;

	if (!crypto_mod_get(calg))
		return -EAGAIN;

	akcipher = crypto_create_tfm(calg, &crypto_akcipher_type);
	if (IS_ERR(akcipher)) {
		crypto_mod_put(calg);
		return PTR_ERR(akcipher);
	}

	*ctx = akcipher;
	tfm->exit = crypto_exit_akcipher_ops_sig;

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_init_akcipher_ops_sig);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic public key cipher type");
