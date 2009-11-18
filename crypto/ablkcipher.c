/*
 * Asynchronous block chaining cipher operations.
 * 
 * This is the asynchronous version of blkcipher.c indicating completion
 * via a callback.
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#include <crypto/internal/skcipher.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/seq_file.h>

#include "internal.h"

static const char *skcipher_default_geniv __read_mostly;

static int setkey_unaligned(struct crypto_ablkcipher *tfm, const u8 *key,
			    unsigned int keylen)
{
	struct ablkcipher_alg *cipher = crypto_ablkcipher_alg(tfm);
	unsigned long alignmask = crypto_ablkcipher_alignmask(tfm);
	int ret;
	u8 *buffer, *alignbuffer;
	unsigned long absize;

	absize = keylen + alignmask;
	buffer = kmalloc(absize, GFP_ATOMIC);
	if (!buffer)
		return -ENOMEM;

	alignbuffer = (u8 *)ALIGN((unsigned long)buffer, alignmask + 1);
	memcpy(alignbuffer, key, keylen);
	ret = cipher->setkey(tfm, alignbuffer, keylen);
	memset(alignbuffer, 0, keylen);
	kfree(buffer);
	return ret;
}

static int setkey(struct crypto_ablkcipher *tfm, const u8 *key,
		  unsigned int keylen)
{
	struct ablkcipher_alg *cipher = crypto_ablkcipher_alg(tfm);
	unsigned long alignmask = crypto_ablkcipher_alignmask(tfm);

	if (keylen < cipher->min_keysize || keylen > cipher->max_keysize) {
		crypto_ablkcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	if ((unsigned long)key & alignmask)
		return setkey_unaligned(tfm, key, keylen);

	return cipher->setkey(tfm, key, keylen);
}

static unsigned int crypto_ablkcipher_ctxsize(struct crypto_alg *alg, u32 type,
					      u32 mask)
{
	return alg->cra_ctxsize;
}

int skcipher_null_givencrypt(struct skcipher_givcrypt_request *req)
{
	return crypto_ablkcipher_encrypt(&req->creq);
}

int skcipher_null_givdecrypt(struct skcipher_givcrypt_request *req)
{
	return crypto_ablkcipher_decrypt(&req->creq);
}

static int crypto_init_ablkcipher_ops(struct crypto_tfm *tfm, u32 type,
				      u32 mask)
{
	struct ablkcipher_alg *alg = &tfm->__crt_alg->cra_ablkcipher;
	struct ablkcipher_tfm *crt = &tfm->crt_ablkcipher;

	if (alg->ivsize > PAGE_SIZE / 8)
		return -EINVAL;

	crt->setkey = setkey;
	crt->encrypt = alg->encrypt;
	crt->decrypt = alg->decrypt;
	if (!alg->ivsize) {
		crt->givencrypt = skcipher_null_givencrypt;
		crt->givdecrypt = skcipher_null_givdecrypt;
	}
	crt->base = __crypto_ablkcipher_cast(tfm);
	crt->ivsize = alg->ivsize;

	return 0;
}

static void crypto_ablkcipher_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_ablkcipher_show(struct seq_file *m, struct crypto_alg *alg)
{
	struct ablkcipher_alg *ablkcipher = &alg->cra_ablkcipher;

	seq_printf(m, "type         : ablkcipher\n");
	seq_printf(m, "async        : %s\n", alg->cra_flags & CRYPTO_ALG_ASYNC ?
					     "yes" : "no");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "min keysize  : %u\n", ablkcipher->min_keysize);
	seq_printf(m, "max keysize  : %u\n", ablkcipher->max_keysize);
	seq_printf(m, "ivsize       : %u\n", ablkcipher->ivsize);
	seq_printf(m, "geniv        : %s\n", ablkcipher->geniv ?: "<default>");
}

const struct crypto_type crypto_ablkcipher_type = {
	.ctxsize = crypto_ablkcipher_ctxsize,
	.init = crypto_init_ablkcipher_ops,
#ifdef CONFIG_PROC_FS
	.show = crypto_ablkcipher_show,
#endif
};
EXPORT_SYMBOL_GPL(crypto_ablkcipher_type);

static int no_givdecrypt(struct skcipher_givcrypt_request *req)
{
	return -ENOSYS;
}

static int crypto_init_givcipher_ops(struct crypto_tfm *tfm, u32 type,
				      u32 mask)
{
	struct ablkcipher_alg *alg = &tfm->__crt_alg->cra_ablkcipher;
	struct ablkcipher_tfm *crt = &tfm->crt_ablkcipher;

	if (alg->ivsize > PAGE_SIZE / 8)
		return -EINVAL;

	crt->setkey = tfm->__crt_alg->cra_flags & CRYPTO_ALG_GENIV ?
		      alg->setkey : setkey;
	crt->encrypt = alg->encrypt;
	crt->decrypt = alg->decrypt;
	crt->givencrypt = alg->givencrypt;
	crt->givdecrypt = alg->givdecrypt ?: no_givdecrypt;
	crt->base = __crypto_ablkcipher_cast(tfm);
	crt->ivsize = alg->ivsize;

	return 0;
}

static void crypto_givcipher_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_givcipher_show(struct seq_file *m, struct crypto_alg *alg)
{
	struct ablkcipher_alg *ablkcipher = &alg->cra_ablkcipher;

	seq_printf(m, "type         : givcipher\n");
	seq_printf(m, "async        : %s\n", alg->cra_flags & CRYPTO_ALG_ASYNC ?
					     "yes" : "no");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "min keysize  : %u\n", ablkcipher->min_keysize);
	seq_printf(m, "max keysize  : %u\n", ablkcipher->max_keysize);
	seq_printf(m, "ivsize       : %u\n", ablkcipher->ivsize);
	seq_printf(m, "geniv        : %s\n", ablkcipher->geniv ?: "<built-in>");
}

const struct crypto_type crypto_givcipher_type = {
	.ctxsize = crypto_ablkcipher_ctxsize,
	.init = crypto_init_givcipher_ops,
#ifdef CONFIG_PROC_FS
	.show = crypto_givcipher_show,
#endif
};
EXPORT_SYMBOL_GPL(crypto_givcipher_type);

const char *crypto_default_geniv(const struct crypto_alg *alg)
{
	if (((alg->cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	     CRYPTO_ALG_TYPE_BLKCIPHER ? alg->cra_blkcipher.ivsize :
					 alg->cra_ablkcipher.ivsize) !=
	    alg->cra_blocksize)
		return "chainiv";

	return alg->cra_flags & CRYPTO_ALG_ASYNC ?
	       "eseqiv" : skcipher_default_geniv;
}

static int crypto_givcipher_default(struct crypto_alg *alg, u32 type, u32 mask)
{
	struct rtattr *tb[3];
	struct {
		struct rtattr attr;
		struct crypto_attr_type data;
	} ptype;
	struct {
		struct rtattr attr;
		struct crypto_attr_alg data;
	} palg;
	struct crypto_template *tmpl;
	struct crypto_instance *inst;
	struct crypto_alg *larval;
	const char *geniv;
	int err;

	larval = crypto_larval_lookup(alg->cra_driver_name,
				      (type & ~CRYPTO_ALG_TYPE_MASK) |
				      CRYPTO_ALG_TYPE_GIVCIPHER,
				      mask | CRYPTO_ALG_TYPE_MASK);
	err = PTR_ERR(larval);
	if (IS_ERR(larval))
		goto out;

	err = -EAGAIN;
	if (!crypto_is_larval(larval))
		goto drop_larval;

	ptype.attr.rta_len = sizeof(ptype);
	ptype.attr.rta_type = CRYPTOA_TYPE;
	ptype.data.type = type | CRYPTO_ALG_GENIV;
	/* GENIV tells the template that we're making a default geniv. */
	ptype.data.mask = mask | CRYPTO_ALG_GENIV;
	tb[0] = &ptype.attr;

	palg.attr.rta_len = sizeof(palg);
	palg.attr.rta_type = CRYPTOA_ALG;
	/* Must use the exact name to locate ourselves. */
	memcpy(palg.data.name, alg->cra_driver_name, CRYPTO_MAX_ALG_NAME);
	tb[1] = &palg.attr;

	tb[2] = NULL;

	if ((alg->cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	    CRYPTO_ALG_TYPE_BLKCIPHER)
		geniv = alg->cra_blkcipher.geniv;
	else
		geniv = alg->cra_ablkcipher.geniv;

	if (!geniv)
		geniv = crypto_default_geniv(alg);

	tmpl = crypto_lookup_template(geniv);
	err = -ENOENT;
	if (!tmpl)
		goto kill_larval;

	inst = tmpl->alloc(tb);
	err = PTR_ERR(inst);
	if (IS_ERR(inst))
		goto put_tmpl;

	if ((err = crypto_register_instance(tmpl, inst))) {
		tmpl->free(inst);
		goto put_tmpl;
	}

	/* Redo the lookup to use the instance we just registered. */
	err = -EAGAIN;

put_tmpl:
	crypto_tmpl_put(tmpl);
kill_larval:
	crypto_larval_kill(larval);
drop_larval:
	crypto_mod_put(larval);
out:
	crypto_mod_put(alg);
	return err;
}

static struct crypto_alg *crypto_lookup_skcipher(const char *name, u32 type,
						 u32 mask)
{
	struct crypto_alg *alg;

	alg = crypto_alg_mod_lookup(name, type, mask);
	if (IS_ERR(alg))
		return alg;

	if ((alg->cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	    CRYPTO_ALG_TYPE_GIVCIPHER)
		return alg;

	if (!((alg->cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	      CRYPTO_ALG_TYPE_BLKCIPHER ? alg->cra_blkcipher.ivsize :
					  alg->cra_ablkcipher.ivsize))
		return alg;

	crypto_mod_put(alg);
	alg = crypto_alg_mod_lookup(name, type | CRYPTO_ALG_TESTED,
				    mask & ~CRYPTO_ALG_TESTED);
	if (IS_ERR(alg))
		return alg;

	if ((alg->cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	    CRYPTO_ALG_TYPE_GIVCIPHER) {
		if ((alg->cra_flags ^ type ^ ~mask) & CRYPTO_ALG_TESTED) {
			crypto_mod_put(alg);
			alg = ERR_PTR(-ENOENT);
		}
		return alg;
	}

	BUG_ON(!((alg->cra_flags & CRYPTO_ALG_TYPE_MASK) ==
		 CRYPTO_ALG_TYPE_BLKCIPHER ? alg->cra_blkcipher.ivsize :
					     alg->cra_ablkcipher.ivsize));

	return ERR_PTR(crypto_givcipher_default(alg, type, mask));
}

int crypto_grab_skcipher(struct crypto_skcipher_spawn *spawn, const char *name,
			 u32 type, u32 mask)
{
	struct crypto_alg *alg;
	int err;

	type = crypto_skcipher_type(type);
	mask = crypto_skcipher_mask(mask);

	alg = crypto_lookup_skcipher(name, type, mask);
	if (IS_ERR(alg))
		return PTR_ERR(alg);

	err = crypto_init_spawn(&spawn->base, alg, spawn->base.inst, mask);
	crypto_mod_put(alg);
	return err;
}
EXPORT_SYMBOL_GPL(crypto_grab_skcipher);

struct crypto_ablkcipher *crypto_alloc_ablkcipher(const char *alg_name,
						  u32 type, u32 mask)
{
	struct crypto_tfm *tfm;
	int err;

	type = crypto_skcipher_type(type);
	mask = crypto_skcipher_mask(mask);

	for (;;) {
		struct crypto_alg *alg;

		alg = crypto_lookup_skcipher(alg_name, type, mask);
		if (IS_ERR(alg)) {
			err = PTR_ERR(alg);
			goto err;
		}

		tfm = __crypto_alloc_tfm(alg, type, mask);
		if (!IS_ERR(tfm))
			return __crypto_ablkcipher_cast(tfm);

		crypto_mod_put(alg);
		err = PTR_ERR(tfm);

err:
		if (err != -EAGAIN)
			break;
		if (signal_pending(current)) {
			err = -EINTR;
			break;
		}
	}

	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(crypto_alloc_ablkcipher);

static int __init skcipher_module_init(void)
{
	skcipher_default_geniv = num_possible_cpus() > 1 ?
				 "eseqiv" : "chainiv";
	return 0;
}

static void skcipher_module_exit(void)
{
}

module_init(skcipher_module_init);
module_exit(skcipher_module_exit);
