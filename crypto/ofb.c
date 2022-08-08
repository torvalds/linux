// SPDX-License-Identifier: GPL-2.0

/*
 * OFB: Output FeedBack mode
 *
 * Copyright (C) 2018 ARM Limited or its affiliates.
 * All rights reserved.
 */

#include <crypto/algapi.h>
#include <crypto/internal/cipher.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int crypto_ofb_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_cipher *cipher = skcipher_cipher_simple(tfm);
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

static int crypto_ofb_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct skcipher_instance *inst;
	struct crypto_alg *alg;
	int err;

	inst = skcipher_alloc_instance_simple(tmpl, tb);
	if (IS_ERR(inst))
		return PTR_ERR(inst);

	alg = skcipher_ialg_simple(inst);

	/* OFB mode is a stream cipher. */
	inst->alg.base.cra_blocksize = 1;

	/*
	 * To simplify the implementation, configure the skcipher walk to only
	 * give a partial block at the very end, never earlier.
	 */
	inst->alg.chunksize = alg->cra_blocksize;

	inst->alg.encrypt = crypto_ofb_crypt;
	inst->alg.decrypt = crypto_ofb_crypt;

	err = skcipher_register_instance(tmpl, inst);
	if (err)
		inst->free(inst);

	return err;
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

subsys_initcall(crypto_ofb_module_init);
module_exit(crypto_ofb_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("OFB block cipher mode of operation");
MODULE_ALIAS_CRYPTO("ofb");
MODULE_IMPORT_NS(CRYPTO_INTERNAL);
