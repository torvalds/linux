/*
 * Header file to maintain compatibility among different kernel versions.
 *
 * Copyright (c) 2004-2006  <lawrence_wang@realsil.com.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */

#include <linux/crypto.h>

static inline int crypto_cipher_encrypt(struct crypto_tfm *tfm,
                                        struct scatterlist *dst,
                                        struct scatterlist *src,
                                        unsigned int nbytes)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->crt_cipher.cit_encrypt(tfm, dst, src, nbytes);
}


static inline int crypto_cipher_decrypt(struct crypto_tfm *tfm,
                                        struct scatterlist *dst,
                                        struct scatterlist *src,
                                        unsigned int nbytes)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->crt_cipher.cit_decrypt(tfm, dst, src, nbytes);
}

#if 0
/*
 *	crypto_free_tfm - Free crypto transform
 *	@tfm: Transform to free
 *
 *	crypto_free_tfm() frees up the transform and any associated resources,
 *	then drops the refcount on the associated algorithm.
 */
void crypto_free_tfm(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg;
	int size;

	if (unlikely(!tfm))
		return;

	alg = tfm->__crt_alg;
	size = sizeof(*tfm) + alg->cra_ctxsize;

	if (alg->cra_exit)
		alg->cra_exit(tfm);
	crypto_exit_ops(tfm);
	crypto_mod_put(alg);
	memset(tfm, 0, size);
	kfree(tfm);
}

#endif
#if 1
 struct crypto_tfm *crypto_alloc_tfm(const char *name, u32 flags)
{
	struct crypto_tfm *tfm = NULL;
	int err;
	printk("call crypto_alloc_tfm!!!\n");
	do {
		struct crypto_alg *alg;

		alg = crypto_alg_mod_lookup(name, 0, CRYPTO_ALG_ASYNC);
		err = PTR_ERR(alg);
		if (IS_ERR(alg))
			continue;

		tfm = __crypto_alloc_tfm(alg, flags);
		err = 0;
		if (IS_ERR(tfm)) {
			crypto_mod_put(alg);
			err = PTR_ERR(tfm);
			tfm = NULL;
		}
	} while (err == -EAGAIN && !signal_pending(current));

	return tfm;
}
#endif
//EXPORT_SYMBOL_GPL(crypto_alloc_tfm);
//EXPORT_SYMBOL_GPL(crypto_free_tfm);


