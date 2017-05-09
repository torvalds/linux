/*
 * Symmetric key cipher operations.
 *
 * Generic encrypt/decrypt wrapper for ciphers, handles operations across
 * multiple page boundaries by using temporary blocks.  In user context,
 * the kernel is given a chance to schedule us once per page.
 *
 * Copyright (c) 2015 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/skcipher.h>
#include <linux/bug.h>
#include <linux/cryptouser.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/seq_file.h>
#include <net/netlink.h>

#include "internal.h"

static unsigned int crypto_skcipher_extsize(struct crypto_alg *alg)
{
	if (alg->cra_type == &crypto_blkcipher_type)
		return sizeof(struct crypto_blkcipher *);

	if (alg->cra_type == &crypto_ablkcipher_type ||
	    alg->cra_type == &crypto_givcipher_type)
		return sizeof(struct crypto_ablkcipher *);

	return crypto_alg_extsize(alg);
}

static int skcipher_setkey_blkcipher(struct crypto_skcipher *tfm,
				     const u8 *key, unsigned int keylen)
{
	struct crypto_blkcipher **ctx = crypto_skcipher_ctx(tfm);
	struct crypto_blkcipher *blkcipher = *ctx;
	int err;

	crypto_blkcipher_clear_flags(blkcipher, ~0);
	crypto_blkcipher_set_flags(blkcipher, crypto_skcipher_get_flags(tfm) &
					      CRYPTO_TFM_REQ_MASK);
	err = crypto_blkcipher_setkey(blkcipher, key, keylen);
	crypto_skcipher_set_flags(tfm, crypto_blkcipher_get_flags(blkcipher) &
				       CRYPTO_TFM_RES_MASK);

	return err;
}

static int skcipher_crypt_blkcipher(struct skcipher_request *req,
				    int (*crypt)(struct blkcipher_desc *,
						 struct scatterlist *,
						 struct scatterlist *,
						 unsigned int))
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_blkcipher **ctx = crypto_skcipher_ctx(tfm);
	struct blkcipher_desc desc = {
		.tfm = *ctx,
		.info = req->iv,
		.flags = req->base.flags,
	};


	return crypt(&desc, req->dst, req->src, req->cryptlen);
}

static int skcipher_encrypt_blkcipher(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(skcipher);
	struct blkcipher_alg *alg = &tfm->__crt_alg->cra_blkcipher;

	return skcipher_crypt_blkcipher(req, alg->encrypt);
}

static int skcipher_decrypt_blkcipher(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(skcipher);
	struct blkcipher_alg *alg = &tfm->__crt_alg->cra_blkcipher;

	return skcipher_crypt_blkcipher(req, alg->decrypt);
}

static void crypto_exit_skcipher_ops_blkcipher(struct crypto_tfm *tfm)
{
	struct crypto_blkcipher **ctx = crypto_tfm_ctx(tfm);

	crypto_free_blkcipher(*ctx);
}

static int crypto_init_skcipher_ops_blkcipher(struct crypto_tfm *tfm)
{
	struct crypto_alg *calg = tfm->__crt_alg;
	struct crypto_skcipher *skcipher = __crypto_skcipher_cast(tfm);
	struct crypto_blkcipher **ctx = crypto_tfm_ctx(tfm);
	struct crypto_blkcipher *blkcipher;
	struct crypto_tfm *btfm;

	if (!crypto_mod_get(calg))
		return -EAGAIN;

	btfm = __crypto_alloc_tfm(calg, CRYPTO_ALG_TYPE_BLKCIPHER,
					CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(btfm)) {
		crypto_mod_put(calg);
		return PTR_ERR(btfm);
	}

	blkcipher = __crypto_blkcipher_cast(btfm);
	*ctx = blkcipher;
	tfm->exit = crypto_exit_skcipher_ops_blkcipher;

	skcipher->setkey = skcipher_setkey_blkcipher;
	skcipher->encrypt = skcipher_encrypt_blkcipher;
	skcipher->decrypt = skcipher_decrypt_blkcipher;

	skcipher->ivsize = crypto_blkcipher_ivsize(blkcipher);
	skcipher->keysize = calg->cra_blkcipher.max_keysize;

	return 0;
}

static int skcipher_setkey_ablkcipher(struct crypto_skcipher *tfm,
				      const u8 *key, unsigned int keylen)
{
	struct crypto_ablkcipher **ctx = crypto_skcipher_ctx(tfm);
	struct crypto_ablkcipher *ablkcipher = *ctx;
	int err;

	crypto_ablkcipher_clear_flags(ablkcipher, ~0);
	crypto_ablkcipher_set_flags(ablkcipher,
				    crypto_skcipher_get_flags(tfm) &
				    CRYPTO_TFM_REQ_MASK);
	err = crypto_ablkcipher_setkey(ablkcipher, key, keylen);
	crypto_skcipher_set_flags(tfm,
				  crypto_ablkcipher_get_flags(ablkcipher) &
				  CRYPTO_TFM_RES_MASK);

	return err;
}

static int skcipher_crypt_ablkcipher(struct skcipher_request *req,
				     int (*crypt)(struct ablkcipher_request *))
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_ablkcipher **ctx = crypto_skcipher_ctx(tfm);
	struct ablkcipher_request *subreq = skcipher_request_ctx(req);

	ablkcipher_request_set_tfm(subreq, *ctx);
	ablkcipher_request_set_callback(subreq, skcipher_request_flags(req),
					req->base.complete, req->base.data);
	ablkcipher_request_set_crypt(subreq, req->src, req->dst, req->cryptlen,
				     req->iv);

	return crypt(subreq);
}

static int skcipher_encrypt_ablkcipher(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(skcipher);
	struct ablkcipher_alg *alg = &tfm->__crt_alg->cra_ablkcipher;

	return skcipher_crypt_ablkcipher(req, alg->encrypt);
}

static int skcipher_decrypt_ablkcipher(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(skcipher);
	struct ablkcipher_alg *alg = &tfm->__crt_alg->cra_ablkcipher;

	return skcipher_crypt_ablkcipher(req, alg->decrypt);
}

static void crypto_exit_skcipher_ops_ablkcipher(struct crypto_tfm *tfm)
{
	struct crypto_ablkcipher **ctx = crypto_tfm_ctx(tfm);

	crypto_free_ablkcipher(*ctx);
}

static int crypto_init_skcipher_ops_ablkcipher(struct crypto_tfm *tfm)
{
	struct crypto_alg *calg = tfm->__crt_alg;
	struct crypto_skcipher *skcipher = __crypto_skcipher_cast(tfm);
	struct crypto_ablkcipher **ctx = crypto_tfm_ctx(tfm);
	struct crypto_ablkcipher *ablkcipher;
	struct crypto_tfm *abtfm;

	if (!crypto_mod_get(calg))
		return -EAGAIN;

	abtfm = __crypto_alloc_tfm(calg, 0, 0);
	if (IS_ERR(abtfm)) {
		crypto_mod_put(calg);
		return PTR_ERR(abtfm);
	}

	ablkcipher = __crypto_ablkcipher_cast(abtfm);
	*ctx = ablkcipher;
	tfm->exit = crypto_exit_skcipher_ops_ablkcipher;

	skcipher->setkey = skcipher_setkey_ablkcipher;
	skcipher->encrypt = skcipher_encrypt_ablkcipher;
	skcipher->decrypt = skcipher_decrypt_ablkcipher;

	skcipher->ivsize = crypto_ablkcipher_ivsize(ablkcipher);
	skcipher->reqsize = crypto_ablkcipher_reqsize(ablkcipher) +
			    sizeof(struct ablkcipher_request);
	skcipher->keysize = calg->cra_ablkcipher.max_keysize;

	return 0;
}

static int skcipher_setkey_unaligned(struct crypto_skcipher *tfm,
				     const u8 *key, unsigned int keylen)
{
	unsigned long alignmask = crypto_skcipher_alignmask(tfm);
	struct skcipher_alg *cipher = crypto_skcipher_alg(tfm);
	u8 *buffer, *alignbuffer;
	unsigned long absize;
	int ret;

	absize = keylen + alignmask;
	buffer = kmalloc(absize, GFP_ATOMIC);
	if (!buffer)
		return -ENOMEM;

	alignbuffer = (u8 *)ALIGN((unsigned long)buffer, alignmask + 1);
	memcpy(alignbuffer, key, keylen);
	ret = cipher->setkey(tfm, alignbuffer, keylen);
	kzfree(buffer);
	return ret;
}

static int skcipher_setkey(struct crypto_skcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	struct skcipher_alg *cipher = crypto_skcipher_alg(tfm);
	unsigned long alignmask = crypto_skcipher_alignmask(tfm);

	if (keylen < cipher->min_keysize || keylen > cipher->max_keysize) {
		crypto_skcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	if ((unsigned long)key & alignmask)
		return skcipher_setkey_unaligned(tfm, key, keylen);

	return cipher->setkey(tfm, key, keylen);
}

static void crypto_skcipher_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_skcipher *skcipher = __crypto_skcipher_cast(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(skcipher);

	alg->exit(skcipher);
}

static int crypto_skcipher_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_skcipher *skcipher = __crypto_skcipher_cast(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(skcipher);

	if (tfm->__crt_alg->cra_type == &crypto_blkcipher_type)
		return crypto_init_skcipher_ops_blkcipher(tfm);

	if (tfm->__crt_alg->cra_type == &crypto_ablkcipher_type ||
	    tfm->__crt_alg->cra_type == &crypto_givcipher_type)
		return crypto_init_skcipher_ops_ablkcipher(tfm);

	skcipher->setkey = skcipher_setkey;
	skcipher->encrypt = alg->encrypt;
	skcipher->decrypt = alg->decrypt;
	skcipher->ivsize = alg->ivsize;
	skcipher->keysize = alg->max_keysize;

	if (alg->exit)
		skcipher->base.exit = crypto_skcipher_exit_tfm;

	if (alg->init)
		return alg->init(skcipher);

	return 0;
}

static void crypto_skcipher_free_instance(struct crypto_instance *inst)
{
	struct skcipher_instance *skcipher =
		container_of(inst, struct skcipher_instance, s.base);

	skcipher->free(skcipher);
}

static void crypto_skcipher_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_skcipher_show(struct seq_file *m, struct crypto_alg *alg)
{
	struct skcipher_alg *skcipher = container_of(alg, struct skcipher_alg,
						     base);

	seq_printf(m, "type         : skcipher\n");
	seq_printf(m, "async        : %s\n",
		   alg->cra_flags & CRYPTO_ALG_ASYNC ?  "yes" : "no");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "min keysize  : %u\n", skcipher->min_keysize);
	seq_printf(m, "max keysize  : %u\n", skcipher->max_keysize);
	seq_printf(m, "ivsize       : %u\n", skcipher->ivsize);
	seq_printf(m, "chunksize    : %u\n", skcipher->chunksize);
}

#ifdef CONFIG_NET
static int crypto_skcipher_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_blkcipher rblkcipher;
	struct skcipher_alg *skcipher = container_of(alg, struct skcipher_alg,
						     base);

	strncpy(rblkcipher.type, "skcipher", sizeof(rblkcipher.type));
	strncpy(rblkcipher.geniv, "<none>", sizeof(rblkcipher.geniv));

	rblkcipher.blocksize = alg->cra_blocksize;
	rblkcipher.min_keysize = skcipher->min_keysize;
	rblkcipher.max_keysize = skcipher->max_keysize;
	rblkcipher.ivsize = skcipher->ivsize;

	if (nla_put(skb, CRYPTOCFGA_REPORT_BLKCIPHER,
		    sizeof(struct crypto_report_blkcipher), &rblkcipher))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}
#else
static int crypto_skcipher_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif

static const struct crypto_type crypto_skcipher_type2 = {
	.extsize = crypto_skcipher_extsize,
	.init_tfm = crypto_skcipher_init_tfm,
	.free = crypto_skcipher_free_instance,
#ifdef CONFIG_PROC_FS
	.show = crypto_skcipher_show,
#endif
	.report = crypto_skcipher_report,
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_BLKCIPHER_MASK,
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.tfmsize = offsetof(struct crypto_skcipher, base),
};

int crypto_grab_skcipher(struct crypto_skcipher_spawn *spawn,
			  const char *name, u32 type, u32 mask)
{
	spawn->base.frontend = &crypto_skcipher_type2;
	return crypto_grab_spawn(&spawn->base, name, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_grab_skcipher);

struct crypto_skcipher *crypto_alloc_skcipher(const char *alg_name,
					      u32 type, u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_skcipher_type2, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_skcipher);

int crypto_has_skcipher2(const char *alg_name, u32 type, u32 mask)
{
	return crypto_type_has_alg(alg_name, &crypto_skcipher_type2,
				   type, mask);
}
EXPORT_SYMBOL_GPL(crypto_has_skcipher2);

static int skcipher_prepare_alg(struct skcipher_alg *alg)
{
	struct crypto_alg *base = &alg->base;

	if (alg->ivsize > PAGE_SIZE / 8 || alg->chunksize > PAGE_SIZE / 8)
		return -EINVAL;

	if (!alg->chunksize)
		alg->chunksize = base->cra_blocksize;

	base->cra_type = &crypto_skcipher_type2;
	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
	base->cra_flags |= CRYPTO_ALG_TYPE_SKCIPHER;

	return 0;
}

int crypto_register_skcipher(struct skcipher_alg *alg)
{
	struct crypto_alg *base = &alg->base;
	int err;

	err = skcipher_prepare_alg(alg);
	if (err)
		return err;

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_skcipher);

void crypto_unregister_skcipher(struct skcipher_alg *alg)
{
	crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_skcipher);

int crypto_register_skciphers(struct skcipher_alg *algs, int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = crypto_register_skcipher(&algs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (--i; i >= 0; --i)
		crypto_unregister_skcipher(&algs[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_register_skciphers);

void crypto_unregister_skciphers(struct skcipher_alg *algs, int count)
{
	int i;

	for (i = count - 1; i >= 0; --i)
		crypto_unregister_skcipher(&algs[i]);
}
EXPORT_SYMBOL_GPL(crypto_unregister_skciphers);

int skcipher_register_instance(struct crypto_template *tmpl,
			   struct skcipher_instance *inst)
{
	int err;

	err = skcipher_prepare_alg(&inst->alg);
	if (err)
		return err;

	return crypto_register_instance(tmpl, skcipher_crypto_instance(inst));
}
EXPORT_SYMBOL_GPL(skcipher_register_instance);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Symmetric key cipher type");
