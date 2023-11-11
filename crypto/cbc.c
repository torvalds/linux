// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CBC: Cipher Block Chaining mode
 *
 * Copyright (c) 2006-2016 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/algapi.h>
#include <crypto/internal/cipher.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/module.h>

static int crypto_cbc_encrypt_segment(struct skcipher_walk *walk,
				      struct crypto_skcipher *skcipher)
{
	unsigned int bsize = crypto_skcipher_blocksize(skcipher);
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	struct crypto_cipher *cipher;
	struct crypto_tfm *tfm;
	u8 *iv = walk->iv;

	cipher = skcipher_cipher_simple(skcipher);
	tfm = crypto_cipher_tfm(cipher);
	fn = crypto_cipher_alg(cipher)->cia_encrypt;

	do {
		crypto_xor(iv, src, bsize);
		fn(tfm, dst, iv);
		memcpy(iv, dst, bsize);

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	return nbytes;
}

static int crypto_cbc_encrypt_inplace(struct skcipher_walk *walk,
				      struct crypto_skcipher *skcipher)
{
	unsigned int bsize = crypto_skcipher_blocksize(skcipher);
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	struct crypto_cipher *cipher;
	struct crypto_tfm *tfm;
	u8 *iv = walk->iv;

	cipher = skcipher_cipher_simple(skcipher);
	tfm = crypto_cipher_tfm(cipher);
	fn = crypto_cipher_alg(cipher)->cia_encrypt;

	do {
		crypto_xor(src, iv, bsize);
		fn(tfm, src, src);
		iv = src;

		src += bsize;
	} while ((nbytes -= bsize) >= bsize);

	memcpy(walk->iv, iv, bsize);

	return nbytes;
}

static int crypto_cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			err = crypto_cbc_encrypt_inplace(&walk, skcipher);
		else
			err = crypto_cbc_encrypt_segment(&walk, skcipher);
		err = skcipher_walk_done(&walk, err);
	}

	return err;
}

static int crypto_cbc_decrypt_segment(struct skcipher_walk *walk,
				      struct crypto_skcipher *skcipher)
{
	unsigned int bsize = crypto_skcipher_blocksize(skcipher);
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	struct crypto_cipher *cipher;
	struct crypto_tfm *tfm;
	u8 *iv = walk->iv;

	cipher = skcipher_cipher_simple(skcipher);
	tfm = crypto_cipher_tfm(cipher);
	fn = crypto_cipher_alg(cipher)->cia_decrypt;

	do {
		fn(tfm, dst, src);
		crypto_xor(dst, iv, bsize);
		iv = src;

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	memcpy(walk->iv, iv, bsize);

	return nbytes;
}

static int crypto_cbc_decrypt_inplace(struct skcipher_walk *walk,
				      struct crypto_skcipher *skcipher)
{
	unsigned int bsize = crypto_skcipher_blocksize(skcipher);
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 last_iv[MAX_CIPHER_BLOCKSIZE];
	struct crypto_cipher *cipher;
	struct crypto_tfm *tfm;

	cipher = skcipher_cipher_simple(skcipher);
	tfm = crypto_cipher_tfm(cipher);
	fn = crypto_cipher_alg(cipher)->cia_decrypt;

	/* Start of the last block. */
	src += nbytes - (nbytes & (bsize - 1)) - bsize;
	memcpy(last_iv, src, bsize);

	for (;;) {
		fn(tfm, src, src);
		if ((nbytes -= bsize) < bsize)
			break;
		crypto_xor(src, src - bsize, bsize);
		src -= bsize;
	}

	crypto_xor(src, walk->iv, bsize);
	memcpy(walk->iv, last_iv, bsize);

	return nbytes;
}

static int crypto_cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			err = crypto_cbc_decrypt_inplace(&walk, skcipher);
		else
			err = crypto_cbc_decrypt_segment(&walk, skcipher);
		err = skcipher_walk_done(&walk, err);
	}

	return err;
}

static int crypto_cbc_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct skcipher_instance *inst;
	struct crypto_alg *alg;
	int err;

	inst = skcipher_alloc_instance_simple(tmpl, tb);
	if (IS_ERR(inst))
		return PTR_ERR(inst);

	alg = skcipher_ialg_simple(inst);

	err = -EINVAL;
	if (!is_power_of_2(alg->cra_blocksize))
		goto out_free_inst;

	inst->alg.encrypt = crypto_cbc_encrypt;
	inst->alg.decrypt = crypto_cbc_decrypt;

	err = skcipher_register_instance(tmpl, inst);
	if (err) {
out_free_inst:
		inst->free(inst);
	}

	return err;
}

static struct crypto_template crypto_cbc_tmpl = {
	.name = "cbc",
	.create = crypto_cbc_create,
	.module = THIS_MODULE,
};

static int __init crypto_cbc_module_init(void)
{
	return crypto_register_template(&crypto_cbc_tmpl);
}

static void __exit crypto_cbc_module_exit(void)
{
	crypto_unregister_template(&crypto_cbc_tmpl);
}

subsys_initcall(crypto_cbc_module_init);
module_exit(crypto_cbc_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CBC block cipher mode of operation");
MODULE_ALIAS_CRYPTO("cbc");
