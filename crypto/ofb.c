// SPDX-License-Identifier: GPL-2.0

/*
 * OFB: Output FeedBack mode
 *
 * Copyright (C) 2018 ARM Limited or its affiliates.
 * All rights reserved.
 *
 * Based loosely on public domain code gleaned from libtomcrypt
 * (https://github.com/libtom/libtomcrypt).
 */

#include <crypto/algapi.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

struct crypto_ofb_ctx {
	struct crypto_cipher *child;
	int cnt;
};


static int crypto_ofb_setkey(struct crypto_skcipher *parent, const u8 *key,
			     unsigned int keylen)
{
	struct crypto_ofb_ctx *ctx = crypto_skcipher_ctx(parent);
	struct crypto_cipher *child = ctx->child;
	int err;

	crypto_cipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(child, crypto_skcipher_get_flags(parent) &
				       CRYPTO_TFM_REQ_MASK);
	err = crypto_cipher_setkey(child, key, keylen);
	crypto_skcipher_set_flags(parent, crypto_cipher_get_flags(child) &
				  CRYPTO_TFM_RES_MASK);
	return err;
}

static int crypto_ofb_encrypt_segment(struct crypto_ofb_ctx *ctx,
				      struct skcipher_walk *walk,
				      struct crypto_cipher *tfm)
{
	int bsize = crypto_cipher_blocksize(tfm);
	int nbytes = walk->nbytes;

	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	u8 *iv = walk->iv;

	do {
		if (ctx->cnt == bsize) {
			if (nbytes < bsize)
				break;
			crypto_cipher_encrypt_one(tfm, iv, iv);
			ctx->cnt = 0;
		}
		*dst = *src ^ iv[ctx->cnt];
		src++;
		dst++;
		ctx->cnt++;
	} while (--nbytes);
	return nbytes;
}

static int crypto_ofb_encrypt(struct skcipher_request *req)
{
	struct skcipher_walk walk;
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	unsigned int bsize;
	struct crypto_ofb_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_cipher *child = ctx->child;
	int ret = 0;

	bsize =  crypto_cipher_blocksize(child);
	ctx->cnt = bsize;

	ret = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes) {
		ret = crypto_ofb_encrypt_segment(ctx, &walk, child);
		ret = skcipher_walk_done(&walk, ret);
	}

	return ret;
}

/* OFB encrypt and decrypt are identical */
static int crypto_ofb_decrypt(struct skcipher_request *req)
{
	return crypto_ofb_encrypt(req);
}

static int crypto_ofb_init_tfm(struct crypto_skcipher *tfm)
{
	struct skcipher_instance *inst = skcipher_alg_instance(tfm);
	struct crypto_spawn *spawn = skcipher_instance_ctx(inst);
	struct crypto_ofb_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_cipher *cipher;

	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;
	return 0;
}

static void crypto_ofb_exit_tfm(struct crypto_skcipher *tfm)
{
	struct crypto_ofb_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_cipher(ctx->child);
}

static void crypto_ofb_free(struct skcipher_instance *inst)
{
	crypto_drop_skcipher(skcipher_instance_ctx(inst));
	kfree(inst);
}

static int crypto_ofb_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct skcipher_instance *inst;
	struct crypto_attr_type *algt;
	struct crypto_spawn *spawn;
	struct crypto_alg *alg;
	u32 mask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SKCIPHER);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	algt = crypto_get_attr_type(tb);
	err = PTR_ERR(algt);
	if (IS_ERR(algt))
		goto err_free_inst;

	mask = CRYPTO_ALG_TYPE_MASK |
		crypto_requires_off(algt->type, algt->mask,
				    CRYPTO_ALG_NEED_FALLBACK);

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_CIPHER, mask);
	err = PTR_ERR(alg);
	if (IS_ERR(alg))
		goto err_free_inst;

	spawn = skcipher_instance_ctx(inst);
	err = crypto_init_spawn(spawn, alg, skcipher_crypto_instance(inst),
				CRYPTO_ALG_TYPE_MASK);
	crypto_mod_put(alg);
	if (err)
		goto err_free_inst;

	err = crypto_inst_setname(skcipher_crypto_instance(inst), "ofb", alg);
	if (err)
		goto err_drop_spawn;

	inst->alg.base.cra_priority = alg->cra_priority;
	inst->alg.base.cra_blocksize = alg->cra_blocksize;
	inst->alg.base.cra_alignmask = alg->cra_alignmask;

	/* We access the data as u32s when xoring. */
	inst->alg.base.cra_alignmask |= __alignof__(u32) - 1;

	inst->alg.ivsize = alg->cra_blocksize;
	inst->alg.min_keysize = alg->cra_cipher.cia_min_keysize;
	inst->alg.max_keysize = alg->cra_cipher.cia_max_keysize;

	inst->alg.base.cra_ctxsize = sizeof(struct crypto_ofb_ctx);

	inst->alg.init = crypto_ofb_init_tfm;
	inst->alg.exit = crypto_ofb_exit_tfm;

	inst->alg.setkey = crypto_ofb_setkey;
	inst->alg.encrypt = crypto_ofb_encrypt;
	inst->alg.decrypt = crypto_ofb_decrypt;

	inst->free = crypto_ofb_free;

	err = skcipher_register_instance(tmpl, inst);
	if (err)
		goto err_drop_spawn;

out:
	return err;

err_drop_spawn:
	crypto_drop_spawn(spawn);
err_free_inst:
	kfree(inst);
	goto out;
}

static struct crypto_template crypto_ofb_tmpl = {
	.name = "ofb",
	.create = crypto_ofb_create,
	.module = THIS_MODULE,
};

static int __init crypto_ofb_module_init(void)
{
	return crypto_register_template(&crypto_ofb_tmpl);
}

static void __exit crypto_ofb_module_exit(void)
{
	crypto_unregister_template(&crypto_ofb_tmpl);
}

module_init(crypto_ofb_module_init);
module_exit(crypto_ofb_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("OFB block cipher algorithm");
MODULE_ALIAS_CRYPTO("ofb");
