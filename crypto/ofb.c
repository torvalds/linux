// SPDX-License-Identifier: GPL-2.0

/*
 * OFB: Output FeedBack mode
 *
 * Copyright (C) 2018 ARM Limited or its affiliates.
 * All rights reserved.
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

static int crypto_ofb_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_ofb_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_cipher *cipher = ctx->child;
	const unsigned int bsize = crypto_cipher_blocksize(cipher);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes >= bsize) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		u8 * const iv = walk.iv;
		unsigned int nbytes = walk.nbytes;

		do {
			crypto_cipher_encrypt_one(cipher, iv, iv);
			crypto_xor_cpy(dst, src, iv, bsize);
			dst += bsize;
			src += bsize;
		} while ((nbytes -= bsize) >= bsize);

		err = skcipher_walk_done(&walk, nbytes);
	}

	if (walk.nbytes) {
		crypto_cipher_encrypt_one(cipher, walk.iv, walk.iv);
		crypto_xor_cpy(walk.dst.virt.addr, walk.src.virt.addr, walk.iv,
			       walk.nbytes);
		err = skcipher_walk_done(&walk, 0);
	}
	return err;
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

	/* OFB mode is a stream cipher. */
	inst->alg.base.cra_blocksize = 1;

	/*
	 * To simplify the implementation, configure the skcipher walk to only
	 * give a partial block at the very end, never earlier.
	 */
	inst->alg.chunksize = alg->cra_blocksize;

	inst->alg.base.cra_priority = alg->cra_priority;
	inst->alg.base.cra_alignmask = alg->cra_alignmask;

	inst->alg.ivsize = alg->cra_blocksize;
	inst->alg.min_keysize = alg->cra_cipher.cia_min_keysize;
	inst->alg.max_keysize = alg->cra_cipher.cia_max_keysize;

	inst->alg.base.cra_ctxsize = sizeof(struct crypto_ofb_ctx);

	inst->alg.init = crypto_ofb_init_tfm;
	inst->alg.exit = crypto_ofb_exit_tfm;

	inst->alg.setkey = crypto_ofb_setkey;
	inst->alg.encrypt = crypto_ofb_crypt;
	inst->alg.decrypt = crypto_ofb_crypt;

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
