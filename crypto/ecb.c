/*
 * ECB: Electronic CodeBook mode
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/algapi.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int crypto_ecb_crypt(struct skcipher_request *req,
			    struct crypto_cipher *cipher,
			    void (*fn)(struct crypto_tfm *, u8 *, const u8 *))
{
	const unsigned int bsize = crypto_cipher_blocksize(cipher);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) != 0) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;

		do {
			fn(crypto_cipher_tfm(cipher), dst, src);

			src += bsize;
			dst += bsize;
		} while ((nbytes -= bsize) >= bsize);

		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int crypto_ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_cipher *cipher = skcipher_cipher_simple(tfm);

	return crypto_ecb_crypt(req, cipher,
				crypto_cipher_alg(cipher)->cia_encrypt);
}

static int crypto_ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_cipher *cipher = skcipher_cipher_simple(tfm);

	return crypto_ecb_crypt(req, cipher,
				crypto_cipher_alg(cipher)->cia_decrypt);
}

static int crypto_ecb_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct skcipher_instance *inst;
	struct crypto_alg *alg;
	int err;

	inst = skcipher_alloc_instance_simple(tmpl, tb, &alg);
	if (IS_ERR(inst))
		return PTR_ERR(inst);

	inst->alg.ivsize = 0; /* ECB mode doesn't take an IV */

	inst->alg.encrypt = crypto_ecb_encrypt;
	inst->alg.decrypt = crypto_ecb_decrypt;

	err = skcipher_register_instance(tmpl, inst);
	if (err)
		inst->free(inst);
	crypto_mod_put(alg);
	return err;
}

static struct crypto_template crypto_ecb_tmpl = {
	.name = "ecb",
	.create = crypto_ecb_create,
	.module = THIS_MODULE,
};

static int __init crypto_ecb_module_init(void)
{
	return crypto_register_template(&crypto_ecb_tmpl);
}

static void __exit crypto_ecb_module_exit(void)
{
	crypto_unregister_template(&crypto_ecb_tmpl);
}

module_init(crypto_ecb_module_init);
module_exit(crypto_ecb_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ECB block cipher mode of operation");
MODULE_ALIAS_CRYPTO("ecb");
