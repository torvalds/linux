// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * RNG operations.
 *
 * Copyright (c) 2008 Neil Horman <nhorman@tuxdriver.com>
 * Copyright (c) 2015 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/internal/rng.h>
#include <linux/atomic.h>
#include <linux/cryptouser.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <net/netlink.h>

#include "internal.h"

static DEFINE_MUTEX(crypto_default_rng_lock);
struct crypto_rng *crypto_default_rng;
EXPORT_SYMBOL_GPL(crypto_default_rng);
static int crypto_default_rng_refcnt;

int crypto_rng_reset(struct crypto_rng *tfm, const u8 *seed, unsigned int slen)
{
	struct rng_alg *alg = crypto_rng_alg(tfm);
	u8 *buf = NULL;
	int err;

	if (IS_ENABLED(CONFIG_CRYPTO_STATS))
		atomic64_inc(&rng_get_stat(alg)->seed_cnt);

	if (!seed && slen) {
		buf = kmalloc(slen, GFP_KERNEL);
		err = -ENOMEM;
		if (!buf)
			goto out;

		err = get_random_bytes_wait(buf, slen);
		if (err)
			goto free_buf;
		seed = buf;
	}

	err = alg->seed(tfm, seed, slen);
free_buf:
	kfree_sensitive(buf);
out:
	return crypto_rng_errstat(alg, err);
}
EXPORT_SYMBOL_GPL(crypto_rng_reset);

static int crypto_rng_init_tfm(struct crypto_tfm *tfm)
{
	return 0;
}

static unsigned int seedsize(struct crypto_alg *alg)
{
	struct rng_alg *ralg = container_of(alg, struct rng_alg, base);

	return ralg->seedsize;
}

static int __maybe_unused crypto_rng_report(
	struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_rng rrng;

	memset(&rrng, 0, sizeof(rrng));

	strscpy(rrng.type, "rng", sizeof(rrng.type));

	rrng.seedsize = seedsize(alg);

	return nla_put(skb, CRYPTOCFGA_REPORT_RNG, sizeof(rrng), &rrng);
}

static void crypto_rng_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;
static void crypto_rng_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_printf(m, "type         : rng\n");
	seq_printf(m, "seedsize     : %u\n", seedsize(alg));
}

static int __maybe_unused crypto_rng_report_stat(
	struct sk_buff *skb, struct crypto_alg *alg)
{
	struct rng_alg *rng = __crypto_rng_alg(alg);
	struct crypto_istat_rng *istat;
	struct crypto_stat_rng rrng;

	istat = rng_get_stat(rng);

	memset(&rrng, 0, sizeof(rrng));

	strscpy(rrng.type, "rng", sizeof(rrng.type));

	rrng.stat_generate_cnt = atomic64_read(&istat->generate_cnt);
	rrng.stat_generate_tlen = atomic64_read(&istat->generate_tlen);
	rrng.stat_seed_cnt = atomic64_read(&istat->seed_cnt);
	rrng.stat_err_cnt = atomic64_read(&istat->err_cnt);

	return nla_put(skb, CRYPTOCFGA_STAT_RNG, sizeof(rrng), &rrng);
}

static const struct crypto_type crypto_rng_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_rng_init_tfm,
#ifdef CONFIG_PROC_FS
	.show = crypto_rng_show,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_USER)
	.report = crypto_rng_report,
#endif
#ifdef CONFIG_CRYPTO_STATS
	.report_stat = crypto_rng_report_stat,
#endif
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_MASK,
	.type = CRYPTO_ALG_TYPE_RNG,
	.tfmsize = offsetof(struct crypto_rng, base),
};

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
	crypto_default_rng_refcnt--;
	mutex_unlock(&crypto_default_rng_lock);
}
EXPORT_SYMBOL_GPL(crypto_put_default_rng);

#if defined(CONFIG_CRYPTO_RNG) || defined(CONFIG_CRYPTO_RNG_MODULE)
int crypto_del_default_rng(void)
{
	int err = -EBUSY;

	mutex_lock(&crypto_default_rng_lock);
	if (crypto_default_rng_refcnt)
		goto out;

	crypto_free_rng(crypto_default_rng);
	crypto_default_rng = NULL;

	err = 0;

out:
	mutex_unlock(&crypto_default_rng_lock);

	return err;
}
EXPORT_SYMBOL_GPL(crypto_del_default_rng);
#endif

int crypto_register_rng(struct rng_alg *alg)
{
	struct crypto_istat_rng *istat = rng_get_stat(alg);
	struct crypto_alg *base = &alg->base;

	if (alg->seedsize > PAGE_SIZE / 8)
		return -EINVAL;

	base->cra_type = &crypto_rng_type;
	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
	base->cra_flags |= CRYPTO_ALG_TYPE_RNG;

	if (IS_ENABLED(CONFIG_CRYPTO_STATS))
		memset(istat, 0, sizeof(*istat));

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_rng);

void crypto_unregister_rng(struct rng_alg *alg)
{
	crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_rng);

int crypto_register_rngs(struct rng_alg *algs, int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = crypto_register_rng(algs + i);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (--i; i >= 0; --i)
		crypto_unregister_rng(algs + i);

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_register_rngs);

void crypto_unregister_rngs(struct rng_alg *algs, int count)
{
	int i;

	for (i = count - 1; i >= 0; --i)
		crypto_unregister_rng(algs + i);
}
EXPORT_SYMBOL_GPL(crypto_unregister_rngs);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Random Number Generator");
