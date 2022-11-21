// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PCBC: Propagating Cipher Block Chaining mode
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * Derived from cbc.c
 * - Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/algapi.h>
#include <crypto/internal/cipher.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int crypto_pcbc_encrypt_segment(struct skcipher_request *req,
				       struct skcipher_walk *walk,
				       struct crypto_cipher *tfm)
{
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	u8 * const iv = walk->iv;

	do {
		crypto_xor(iv, src, bsize);
		crypto_cipher_encrypt_one(tfm, dst, iv);
		crypto_xor_cpy(iv, dst, src, bsize);

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	return nbytes;
}

static int crypto_pcbc_encrypt_inplace(struct skcipher_request *req,
				       struct skcipher_walk *walk,
				       struct crypto_cipher *tfm)
{
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 * const iv = walk->iv;
	u8 tmpbuf[MAX_CIPHER_BLOCKSIZE];

	do {
		memcpy(tmpbuf, src, bsize);
		crypto_xor(iv, src, bsize);
		crypto_cipher_encrypt_one(tfm, src, iv);
		crypto_xor_cpy(iv, tmpbuf, src, bsize);

		src += bsize;
	} while ((nbytes -= bsize) >= bsize);

	return nbytes;
}

static int crypto_pcbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_cipher *cipher = skcipher_cipher_simple(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			nbytes = crypto_pcbc_encrypt_inplace(req, &walk,
							     cipher);
		else
			nbytes = crypto_pcbc_encrypt_segment(req, &walk,
							     cipher);
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int crypto_pcbc_decrypt_segment(struct skcipher_request *req,
				       struct skcipher_walk *walk,
				       struct crypto_cipher *tfm)
{
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	u8 * const iv = walk->iv;

	do {
		crypto_cipher_decrypt_one(tfm, dst, src);
		crypto_xor(dst, iv, bsize);
		crypto_xor_cpy(iv, dst, src, bsize);

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	return nbytes;
}

static int crypto_pcbc_decrypt_inplace(struct skcipher_request *req,
				       struct skcipher_walk *walk,
				       struct crypto_cipher *tfm)
{
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 * const iv = walk->iv;
	u8 tmpbuf[MAX_CIPHER_BLOCKSIZE] __aligned(__alignof__(u32));

	do {
		memcpy(tmpbuf, src, bsize);
		crypto_cipher_decrypt_one(tfm, src, src);
		crypto_xor(src, iv, bsize);
		crypto_xor_cpy(iv, src, tmpbuf, bsize);

		src += bsize;
	} while ((nbytes -= bsize) >= bsize);

	return nbytes;
}

static int crypto_pcbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_cipher *cipher = skcipher_cipher_simple(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			nbytes = crypto_pcbc_decrypt_inplace(req, &walk,
							     cipher);
		else
			nbytes = crypto_pcbc_decrypt_segment(req, &walk,
							     cipher);
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int crypto_pcbc_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct skcipher_instance *inst;
	int err;

	inst = skcipher_alloc_instance_simple(tmpl, tb);
	if (IS_ERR(inst))
		return PTR_ERR(inst);

	inst->alg.encrypt = crypto_pcbc_encrypt;
	inst->alg.decrypt = crypto_pcbc_decrypt;

	err = skcipher_register_instance(tmpl, inst);
	if (err)
		inst->free(inst);

	return err;
}

static struct crypto_template crypto_pcbc_tmpl = {
	.name = "pcbc",
	.create = crypto_pcbc_create,
	.module = THIS_MODULE,
};

static int __init crypto_pcbc_module_init(void)
{
	return crypto_register_template(&crypto_pcbc_tmpl);
}

static void __exit crypto_pcbc_module_exit(void)
{
	crypto_unregister_template(&crypto_pcbc_tmpl);
}

subsys_initcall(crypto_pcbc_module_init);
module_exit(crypto_pcbc_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCBC block cipher mode of operation");
MODULE_ALIAS_CRYPTO("pcbc");
MODULE_IMPORT_NS(CRYPTO_INTERNAL);
