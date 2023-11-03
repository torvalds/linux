// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linear symmetric key cipher operations.
 *
 * Generic encrypt/decrypt wrapper for ciphers.
 *
 * Copyright (c) 2023 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <linux/cryptouser.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <net/netlink.h>
#include "skcipher.h"

static inline struct crypto_lskcipher *__crypto_lskcipher_cast(
	struct crypto_tfm *tfm)
{
	return container_of(tfm, struct crypto_lskcipher, base);
}

static inline struct lskcipher_alg *__crypto_lskcipher_alg(
	struct crypto_alg *alg)
{
	return container_of(alg, struct lskcipher_alg, co.base);
}

static inline struct crypto_istat_cipher *lskcipher_get_stat(
	struct lskcipher_alg *alg)
{
	return skcipher_get_stat_common(&alg->co);
}

static inline int crypto_lskcipher_errstat(struct lskcipher_alg *alg, int err)
{
	struct crypto_istat_cipher *istat = lskcipher_get_stat(alg);

	if (!IS_ENABLED(CONFIG_CRYPTO_STATS))
		return err;

	if (err)
		atomic64_inc(&istat->err_cnt);

	return err;
}

static int lskcipher_setkey_unaligned(struct crypto_lskcipher *tfm,
				      const u8 *key, unsigned int keylen)
{
	unsigned long alignmask = crypto_lskcipher_alignmask(tfm);
	struct lskcipher_alg *cipher = crypto_lskcipher_alg(tfm);
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
	kfree_sensitive(buffer);
	return ret;
}

int crypto_lskcipher_setkey(struct crypto_lskcipher *tfm, const u8 *key,
			    unsigned int keylen)
{
	unsigned long alignmask = crypto_lskcipher_alignmask(tfm);
	struct lskcipher_alg *cipher = crypto_lskcipher_alg(tfm);

	if (keylen < cipher->co.min_keysize || keylen > cipher->co.max_keysize)
		return -EINVAL;

	if ((unsigned long)key & alignmask)
		return lskcipher_setkey_unaligned(tfm, key, keylen);
	else
		return cipher->setkey(tfm, key, keylen);
}
EXPORT_SYMBOL_GPL(crypto_lskcipher_setkey);

static int crypto_lskcipher_crypt_unaligned(
	struct crypto_lskcipher *tfm, const u8 *src, u8 *dst, unsigned len,
	u8 *iv, int (*crypt)(struct crypto_lskcipher *tfm, const u8 *src,
			     u8 *dst, unsigned len, u8 *iv, bool final))
{
	unsigned ivsize = crypto_lskcipher_ivsize(tfm);
	unsigned bs = crypto_lskcipher_blocksize(tfm);
	unsigned cs = crypto_lskcipher_chunksize(tfm);
	int err;
	u8 *tiv;
	u8 *p;

	BUILD_BUG_ON(MAX_CIPHER_BLOCKSIZE > PAGE_SIZE ||
		     MAX_CIPHER_ALIGNMASK >= PAGE_SIZE);

	tiv = kmalloc(PAGE_SIZE, GFP_ATOMIC);
	if (!tiv)
		return -ENOMEM;

	memcpy(tiv, iv, ivsize);

	p = kmalloc(PAGE_SIZE, GFP_ATOMIC);
	err = -ENOMEM;
	if (!p)
		goto out;

	while (len >= bs) {
		unsigned chunk = min((unsigned)PAGE_SIZE, len);
		int err;

		if (chunk > cs)
			chunk &= ~(cs - 1);

		memcpy(p, src, chunk);
		err = crypt(tfm, p, p, chunk, tiv, true);
		if (err)
			goto out;

		memcpy(dst, p, chunk);
		src += chunk;
		dst += chunk;
		len -= chunk;
	}

	err = len ? -EINVAL : 0;

out:
	memcpy(iv, tiv, ivsize);
	kfree_sensitive(p);
	kfree_sensitive(tiv);
	return err;
}

static int crypto_lskcipher_crypt(struct crypto_lskcipher *tfm, const u8 *src,
				  u8 *dst, unsigned len, u8 *iv,
				  int (*crypt)(struct crypto_lskcipher *tfm,
					       const u8 *src, u8 *dst,
					       unsigned len, u8 *iv,
					       bool final))
{
	unsigned long alignmask = crypto_lskcipher_alignmask(tfm);
	struct lskcipher_alg *alg = crypto_lskcipher_alg(tfm);
	int ret;

	if (((unsigned long)src | (unsigned long)dst | (unsigned long)iv) &
	    alignmask) {
		ret = crypto_lskcipher_crypt_unaligned(tfm, src, dst, len, iv,
						       crypt);
		goto out;
	}

	ret = crypt(tfm, src, dst, len, iv, true);

out:
	return crypto_lskcipher_errstat(alg, ret);
}

int crypto_lskcipher_encrypt(struct crypto_lskcipher *tfm, const u8 *src,
			     u8 *dst, unsigned len, u8 *iv)
{
	struct lskcipher_alg *alg = crypto_lskcipher_alg(tfm);

	if (IS_ENABLED(CONFIG_CRYPTO_STATS)) {
		struct crypto_istat_cipher *istat = lskcipher_get_stat(alg);

		atomic64_inc(&istat->encrypt_cnt);
		atomic64_add(len, &istat->encrypt_tlen);
	}

	return crypto_lskcipher_crypt(tfm, src, dst, len, iv, alg->encrypt);
}
EXPORT_SYMBOL_GPL(crypto_lskcipher_encrypt);

int crypto_lskcipher_decrypt(struct crypto_lskcipher *tfm, const u8 *src,
			     u8 *dst, unsigned len, u8 *iv)
{
	struct lskcipher_alg *alg = crypto_lskcipher_alg(tfm);

	if (IS_ENABLED(CONFIG_CRYPTO_STATS)) {
		struct crypto_istat_cipher *istat = lskcipher_get_stat(alg);

		atomic64_inc(&istat->decrypt_cnt);
		atomic64_add(len, &istat->decrypt_tlen);
	}

	return crypto_lskcipher_crypt(tfm, src, dst, len, iv, alg->decrypt);
}
EXPORT_SYMBOL_GPL(crypto_lskcipher_decrypt);

static int crypto_lskcipher_crypt_sg(struct skcipher_request *req,
				     int (*crypt)(struct crypto_lskcipher *tfm,
						  const u8 *src, u8 *dst,
						  unsigned len, u8 *iv,
						  bool final))
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct crypto_lskcipher **ctx = crypto_skcipher_ctx(skcipher);
	struct crypto_lskcipher *tfm = *ctx;
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes) {
		err = crypt(tfm, walk.src.virt.addr, walk.dst.virt.addr,
			    walk.nbytes, walk.iv, walk.nbytes == walk.total);
		err = skcipher_walk_done(&walk, err);
	}

	return err;
}

int crypto_lskcipher_encrypt_sg(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct crypto_lskcipher **ctx = crypto_skcipher_ctx(skcipher);
	struct lskcipher_alg *alg = crypto_lskcipher_alg(*ctx);

	return crypto_lskcipher_crypt_sg(req, alg->encrypt);
}

int crypto_lskcipher_decrypt_sg(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct crypto_lskcipher **ctx = crypto_skcipher_ctx(skcipher);
	struct lskcipher_alg *alg = crypto_lskcipher_alg(*ctx);

	return crypto_lskcipher_crypt_sg(req, alg->decrypt);
}

static void crypto_lskcipher_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_lskcipher *skcipher = __crypto_lskcipher_cast(tfm);
	struct lskcipher_alg *alg = crypto_lskcipher_alg(skcipher);

	alg->exit(skcipher);
}

static int crypto_lskcipher_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_lskcipher *skcipher = __crypto_lskcipher_cast(tfm);
	struct lskcipher_alg *alg = crypto_lskcipher_alg(skcipher);

	if (alg->exit)
		skcipher->base.exit = crypto_lskcipher_exit_tfm;

	if (alg->init)
		return alg->init(skcipher);

	return 0;
}

static void crypto_lskcipher_free_instance(struct crypto_instance *inst)
{
	struct lskcipher_instance *skcipher =
		container_of(inst, struct lskcipher_instance, s.base);

	skcipher->free(skcipher);
}

static void __maybe_unused crypto_lskcipher_show(
	struct seq_file *m, struct crypto_alg *alg)
{
	struct lskcipher_alg *skcipher = __crypto_lskcipher_alg(alg);

	seq_printf(m, "type         : lskcipher\n");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "min keysize  : %u\n", skcipher->co.min_keysize);
	seq_printf(m, "max keysize  : %u\n", skcipher->co.max_keysize);
	seq_printf(m, "ivsize       : %u\n", skcipher->co.ivsize);
	seq_printf(m, "chunksize    : %u\n", skcipher->co.chunksize);
}

static int __maybe_unused crypto_lskcipher_report(
	struct sk_buff *skb, struct crypto_alg *alg)
{
	struct lskcipher_alg *skcipher = __crypto_lskcipher_alg(alg);
	struct crypto_report_blkcipher rblkcipher;

	memset(&rblkcipher, 0, sizeof(rblkcipher));

	strscpy(rblkcipher.type, "lskcipher", sizeof(rblkcipher.type));
	strscpy(rblkcipher.geniv, "<none>", sizeof(rblkcipher.geniv));

	rblkcipher.blocksize = alg->cra_blocksize;
	rblkcipher.min_keysize = skcipher->co.min_keysize;
	rblkcipher.max_keysize = skcipher->co.max_keysize;
	rblkcipher.ivsize = skcipher->co.ivsize;

	return nla_put(skb, CRYPTOCFGA_REPORT_BLKCIPHER,
		       sizeof(rblkcipher), &rblkcipher);
}

static int __maybe_unused crypto_lskcipher_report_stat(
	struct sk_buff *skb, struct crypto_alg *alg)
{
	struct lskcipher_alg *skcipher = __crypto_lskcipher_alg(alg);
	struct crypto_istat_cipher *istat;
	struct crypto_stat_cipher rcipher;

	istat = lskcipher_get_stat(skcipher);

	memset(&rcipher, 0, sizeof(rcipher));

	strscpy(rcipher.type, "cipher", sizeof(rcipher.type));

	rcipher.stat_encrypt_cnt = atomic64_read(&istat->encrypt_cnt);
	rcipher.stat_encrypt_tlen = atomic64_read(&istat->encrypt_tlen);
	rcipher.stat_decrypt_cnt =  atomic64_read(&istat->decrypt_cnt);
	rcipher.stat_decrypt_tlen = atomic64_read(&istat->decrypt_tlen);
	rcipher.stat_err_cnt =  atomic64_read(&istat->err_cnt);

	return nla_put(skb, CRYPTOCFGA_STAT_CIPHER, sizeof(rcipher), &rcipher);
}

static const struct crypto_type crypto_lskcipher_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_lskcipher_init_tfm,
	.free = crypto_lskcipher_free_instance,
#ifdef CONFIG_PROC_FS
	.show = crypto_lskcipher_show,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_USER)
	.report = crypto_lskcipher_report,
#endif
#ifdef CONFIG_CRYPTO_STATS
	.report_stat = crypto_lskcipher_report_stat,
#endif
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_MASK,
	.type = CRYPTO_ALG_TYPE_LSKCIPHER,
	.tfmsize = offsetof(struct crypto_lskcipher, base),
};

static void crypto_lskcipher_exit_tfm_sg(struct crypto_tfm *tfm)
{
	struct crypto_lskcipher **ctx = crypto_tfm_ctx(tfm);

	crypto_free_lskcipher(*ctx);
}

int crypto_init_lskcipher_ops_sg(struct crypto_tfm *tfm)
{
	struct crypto_lskcipher **ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *calg = tfm->__crt_alg;
	struct crypto_lskcipher *skcipher;

	if (!crypto_mod_get(calg))
		return -EAGAIN;

	skcipher = crypto_create_tfm(calg, &crypto_lskcipher_type);
	if (IS_ERR(skcipher)) {
		crypto_mod_put(calg);
		return PTR_ERR(skcipher);
	}

	*ctx = skcipher;
	tfm->exit = crypto_lskcipher_exit_tfm_sg;

	return 0;
}

int crypto_grab_lskcipher(struct crypto_lskcipher_spawn *spawn,
			  struct crypto_instance *inst,
			  const char *name, u32 type, u32 mask)
{
	spawn->base.frontend = &crypto_lskcipher_type;
	return crypto_grab_spawn(&spawn->base, inst, name, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_grab_lskcipher);

struct crypto_lskcipher *crypto_alloc_lskcipher(const char *alg_name,
						u32 type, u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_lskcipher_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_lskcipher);

static int lskcipher_prepare_alg(struct lskcipher_alg *alg)
{
	struct crypto_alg *base = &alg->co.base;
	int err;

	err = skcipher_prepare_alg_common(&alg->co);
	if (err)
		return err;

	if (alg->co.chunksize & (alg->co.chunksize - 1))
		return -EINVAL;

	base->cra_type = &crypto_lskcipher_type;
	base->cra_flags |= CRYPTO_ALG_TYPE_LSKCIPHER;

	return 0;
}

int crypto_register_lskcipher(struct lskcipher_alg *alg)
{
	struct crypto_alg *base = &alg->co.base;
	int err;

	err = lskcipher_prepare_alg(alg);
	if (err)
		return err;

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_lskcipher);

void crypto_unregister_lskcipher(struct lskcipher_alg *alg)
{
	crypto_unregister_alg(&alg->co.base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_lskcipher);

int crypto_register_lskciphers(struct lskcipher_alg *algs, int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = crypto_register_lskcipher(&algs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (--i; i >= 0; --i)
		crypto_unregister_lskcipher(&algs[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_register_lskciphers);

void crypto_unregister_lskciphers(struct lskcipher_alg *algs, int count)
{
	int i;

	for (i = count - 1; i >= 0; --i)
		crypto_unregister_lskcipher(&algs[i]);
}
EXPORT_SYMBOL_GPL(crypto_unregister_lskciphers);

int lskcipher_register_instance(struct crypto_template *tmpl,
				struct lskcipher_instance *inst)
{
	int err;

	if (WARN_ON(!inst->free))
		return -EINVAL;

	err = lskcipher_prepare_alg(&inst->alg);
	if (err)
		return err;

	return crypto_register_instance(tmpl, lskcipher_crypto_instance(inst));
}
EXPORT_SYMBOL_GPL(lskcipher_register_instance);

static int lskcipher_setkey_simple(struct crypto_lskcipher *tfm, const u8 *key,
				   unsigned int keylen)
{
	struct crypto_lskcipher *cipher = lskcipher_cipher_simple(tfm);

	crypto_lskcipher_clear_flags(cipher, CRYPTO_TFM_REQ_MASK);
	crypto_lskcipher_set_flags(cipher, crypto_lskcipher_get_flags(tfm) &
				   CRYPTO_TFM_REQ_MASK);
	return crypto_lskcipher_setkey(cipher, key, keylen);
}

static int lskcipher_init_tfm_simple(struct crypto_lskcipher *tfm)
{
	struct lskcipher_instance *inst = lskcipher_alg_instance(tfm);
	struct crypto_lskcipher **ctx = crypto_lskcipher_ctx(tfm);
	struct crypto_lskcipher_spawn *spawn;
	struct crypto_lskcipher *cipher;

	spawn = lskcipher_instance_ctx(inst);
	cipher = crypto_spawn_lskcipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	*ctx = cipher;
	return 0;
}

static void lskcipher_exit_tfm_simple(struct crypto_lskcipher *tfm)
{
	struct crypto_lskcipher **ctx = crypto_lskcipher_ctx(tfm);

	crypto_free_lskcipher(*ctx);
}

static void lskcipher_free_instance_simple(struct lskcipher_instance *inst)
{
	crypto_drop_lskcipher(lskcipher_instance_ctx(inst));
	kfree(inst);
}

/**
 * lskcipher_alloc_instance_simple - allocate instance of simple block cipher
 *
 * Allocate an lskcipher_instance for a simple block cipher mode of operation,
 * e.g. cbc or ecb.  The instance context will have just a single crypto_spawn,
 * that for the underlying cipher.  The {min,max}_keysize, ivsize, blocksize,
 * alignmask, and priority are set from the underlying cipher but can be
 * overridden if needed.  The tfm context defaults to
 * struct crypto_lskcipher *, and default ->setkey(), ->init(), and
 * ->exit() methods are installed.
 *
 * @tmpl: the template being instantiated
 * @tb: the template parameters
 *
 * Return: a pointer to the new instance, or an ERR_PTR().  The caller still
 *	   needs to register the instance.
 */
struct lskcipher_instance *lskcipher_alloc_instance_simple(
	struct crypto_template *tmpl, struct rtattr **tb)
{
	u32 mask;
	struct lskcipher_instance *inst;
	struct crypto_lskcipher_spawn *spawn;
	char ecb_name[CRYPTO_MAX_ALG_NAME];
	struct lskcipher_alg *cipher_alg;
	const char *cipher_name;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_LSKCIPHER, &mask);
	if (err)
		return ERR_PTR(err);

	cipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(cipher_name))
		return ERR_CAST(cipher_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return ERR_PTR(-ENOMEM);

	spawn = lskcipher_instance_ctx(inst);
	err = crypto_grab_lskcipher(spawn,
				    lskcipher_crypto_instance(inst),
				    cipher_name, 0, mask);

	ecb_name[0] = 0;
	if (err == -ENOENT && !!memcmp(tmpl->name, "ecb", 4)) {
		err = -ENAMETOOLONG;
		if (snprintf(ecb_name, CRYPTO_MAX_ALG_NAME, "ecb(%s)",
			     cipher_name) >= CRYPTO_MAX_ALG_NAME)
			goto err_free_inst;

		err = crypto_grab_lskcipher(spawn,
					    lskcipher_crypto_instance(inst),
					    ecb_name, 0, mask);
	}

	if (err)
		goto err_free_inst;

	cipher_alg = crypto_lskcipher_spawn_alg(spawn);

	err = crypto_inst_setname(lskcipher_crypto_instance(inst), tmpl->name,
				  &cipher_alg->co.base);
	if (err)
		goto err_free_inst;

	if (ecb_name[0]) {
		int len;

		err = -EINVAL;
		len = strscpy(ecb_name, &cipher_alg->co.base.cra_name[4],
			      sizeof(ecb_name));
		if (len < 2)
			goto err_free_inst;

		if (ecb_name[len - 1] != ')')
			goto err_free_inst;

		ecb_name[len - 1] = 0;

		err = -ENAMETOOLONG;
		if (snprintf(inst->alg.co.base.cra_name, CRYPTO_MAX_ALG_NAME,
			     "%s(%s)", tmpl->name, ecb_name) >=
		    CRYPTO_MAX_ALG_NAME)
			goto err_free_inst;

		if (strcmp(ecb_name, cipher_name) &&
		    snprintf(inst->alg.co.base.cra_driver_name,
			     CRYPTO_MAX_ALG_NAME,
			     "%s(%s)", tmpl->name, cipher_name) >=
		    CRYPTO_MAX_ALG_NAME)
			goto err_free_inst;
	} else {
		/* Don't allow nesting. */
		err = -ELOOP;
		if ((cipher_alg->co.base.cra_flags & CRYPTO_ALG_INSTANCE))
			goto err_free_inst;
	}

	err = -EINVAL;
	if (cipher_alg->co.ivsize)
		goto err_free_inst;

	inst->free = lskcipher_free_instance_simple;

	/* Default algorithm properties, can be overridden */
	inst->alg.co.base.cra_blocksize = cipher_alg->co.base.cra_blocksize;
	inst->alg.co.base.cra_alignmask = cipher_alg->co.base.cra_alignmask;
	inst->alg.co.base.cra_priority = cipher_alg->co.base.cra_priority;
	inst->alg.co.min_keysize = cipher_alg->co.min_keysize;
	inst->alg.co.max_keysize = cipher_alg->co.max_keysize;
	inst->alg.co.ivsize = cipher_alg->co.base.cra_blocksize;

	/* Use struct crypto_lskcipher * by default, can be overridden */
	inst->alg.co.base.cra_ctxsize = sizeof(struct crypto_lskcipher *);
	inst->alg.setkey = lskcipher_setkey_simple;
	inst->alg.init = lskcipher_init_tfm_simple;
	inst->alg.exit = lskcipher_exit_tfm_simple;

	return inst;

err_free_inst:
	lskcipher_free_instance_simple(inst);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(lskcipher_alloc_instance_simple);
