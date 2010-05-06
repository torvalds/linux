/*
 * Cryptographic API.
 *
 * RNG operations.
 *
 * Copyright (c) 2008 Neil Horman <nhorman@tuxdriver.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <asm/atomic.h>
#include <crypto/internal/rng.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>

static DEFINE_MUTEX(crypto_default_rng_lock);
struct crypto_rng *crypto_default_rng;
EXPORT_SYMBOL_GPL(crypto_default_rng);
static int crypto_default_rng_refcnt;

static int rngapi_reset(struct crypto_rng *tfm, u8 *seed, unsigned int slen)
{
	u8 *buf = NULL;
	int err;

	if (!seed && slen) {
		buf = kmalloc(slen, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		get_random_bytes(buf, slen);
		seed = buf;
	}

	err = crypto_rng_alg(tfm)->rng_reset(tfm, seed, slen);

	kfree(buf);
	return err;
}

static int crypto_init_rng_ops(struct crypto_tfm *tfm, u32 type, u32 mask)
{
	struct rng_alg *alg = &tfm->__crt_alg->cra_rng;
	struct rng_tfm *ops = &tfm->crt_rng;

	ops->rng_gen_random = alg->rng_make_random;
	ops->rng_reset = rngapi_reset;

	return 0;
}

static void crypto_rng_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_rng_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_printf(m, "type         : rng\n");
	seq_printf(m, "seedsize     : %u\n", alg->cra_rng.seedsize);
}

static unsigned int crypto_rng_ctxsize(struct crypto_alg *alg, u32 type,
				       u32 mask)
{
	return alg->cra_ctxsize;
}

const struct crypto_type crypto_rng_type = {
	.ctxsize = crypto_rng_ctxsize,
	.init = crypto_init_rng_ops,
#ifdef CONFIG_PROC_FS
	.show = crypto_rng_show,
#endif
};
EXPORT_SYMBOL_GPL(crypto_rng_type);

int crypto_get_default_rng(void)
{
	struct crypto_rng *rng;
	int err;

	mutex_lock(&crypto_default_rng_lock);
	if (!crypto_default_rng) {
		rng = crypto_alloc_rng("stdrng", 0, 0);
		err = PTR_ERR(rng);
		if (IS_ERR(rng))
			goto unlock;

		err = crypto_rng_reset(rng, NULL, crypto_rng_seedsize(rng));
		if (err) {
			crypto_free_rng(rng);
			goto unlock;
		}

		crypto_default_rng = rng;
	}

	crypto_default_rng_refcnt++;
	err = 0;

unlock:
	mutex_unlock(&crypto_default_rng_lock);

	return err;
}
EXPORT_SYMBOL_GPL(crypto_get_default_rng);

void crypto_put_default_rng(void)
{
	mutex_lock(&crypto_default_rng_lock);
	if (!--crypto_default_rng_refcnt) {
		crypto_free_rng(crypto_default_rng);
		crypto_default_rng = NULL;
	}
	mutex_unlock(&crypto_default_rng_lock);
}
EXPORT_SYMBOL_GPL(crypto_put_default_rng);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Random Number Generator");
