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

#include <linux/atomic.h>
#include <crypto/internal/rng.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/cryptouser.h>
#include <net/netlink.h>

#include "internal.h"

static DEFINE_MUTEX(crypto_default_rng_lock);
struct crypto_rng *crypto_default_rng;
EXPORT_SYMBOL_GPL(crypto_default_rng);
static int crypto_default_rng_refcnt;

static inline struct crypto_rng *__crypto_rng_cast(struct crypto_tfm *tfm)
{
	return container_of(tfm, struct crypto_rng, base);
}

static inline struct old_rng_alg *crypto_old_rng_alg(struct crypto_rng *tfm)
{
	return &crypto_rng_tfm(tfm)->__crt_alg->cra_rng;
}

static int generate(struct crypto_rng *tfm, const u8 *src, unsigned int slen,
		    u8 *dst, unsigned int dlen)
{
	return crypto_old_rng_alg(tfm)->rng_make_random(tfm, dst, dlen);
}

static int rngapi_reset(struct crypto_rng *tfm, const u8 *seed,
			unsigned int slen)
{
	u8 *buf = NULL;
	u8 *src = (u8 *)seed;
	int err;

	if (slen) {
		buf = kmalloc(slen, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		memcpy(buf, seed, slen);
		src = buf;
	}

	err = crypto_old_rng_alg(tfm)->rng_reset(tfm, src, slen);

	kzfree(buf);
	return err;
}

int crypto_rng_reset(struct crypto_rng *tfm, const u8 *seed, unsigned int slen)
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

	err = tfm->seed(tfm, seed, slen);

	kfree(buf);
	return err;
}
EXPORT_SYMBOL_GPL(crypto_rng_reset);

static int crypto_rng_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_rng *rng = __crypto_rng_cast(tfm);
	struct rng_alg *alg = crypto_rng_alg(rng);
	struct old_rng_alg *oalg = crypto_old_rng_alg(rng);

	if (oalg->rng_make_random) {
		rng->generate = generate;
		rng->seed = rngapi_reset;
		rng->seedsize = oalg->seedsize;
		return 0;
	}

	rng->generate = alg->generate;
	rng->seed = alg->seed;
	rng->seedsize = alg->seedsize;

	return 0;
}

static unsigned int seedsize(struct crypto_alg *alg)
{
	struct rng_alg *ralg = container_of(alg, struct rng_alg, base);

	return alg->cra_rng.rng_make_random ?
	       alg->cra_rng.seedsize : ralg->seedsize;
}

#ifdef CONFIG_NET
static int crypto_rng_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_rng rrng;

	strncpy(rrng.type, "rng", sizeof(rrng.type));

	rrng.seedsize = seedsize(alg);

	if (nla_put(skb, CRYPTOCFGA_REPORT_RNG,
		    sizeof(struct crypto_report_rng), &rrng))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}
#else
static int crypto_rng_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif

static void crypto_rng_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_rng_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_printf(m, "type         : rng\n");
	seq_printf(m, "seedsize     : %u\n", seedsize(alg));
}

const struct crypto_type crypto_rng_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_rng_init_tfm,
#ifdef CONFIG_PROC_FS
	.show = crypto_rng_show,
#endif
	.report = crypto_rng_report,
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_MASK,
	.type = CRYPTO_ALG_TYPE_RNG,
	.tfmsize = offsetof(struct crypto_rng, base),
};
EXPORT_SYMBOL_GPL(crypto_rng_type);

struct crypto_rng *crypto_alloc_rng(const char *alg_name, u32 type, u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_rng_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_rng);

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

int crypto_register_rng(struct rng_alg *alg)
{
	struct crypto_alg *base = &alg->base;

	if (alg->seedsize > PAGE_SIZE / 8)
		return -EINVAL;

	base->cra_type = &crypto_rng_type;
	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
	base->cra_flags |= CRYPTO_ALG_TYPE_RNG;

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_rng);

void crypto_unregister_rng(struct rng_alg *alg)
{
	crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_rng);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Random Number Generator");
