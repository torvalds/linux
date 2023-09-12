/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Public Key Signature Algorithm
 *
 * Copyright (c) 2023 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/akcipher.h>
#include <crypto/internal/sig.h>
#include <linux/cryptouser.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <net/netlink.h>

#include "internal.h"

#define CRYPTO_ALG_TYPE_SIG_MASK	0x0000000e

static const struct crypto_type crypto_sig_type;

static int crypto_sig_init_tfm(struct crypto_tfm *tfm)
{
	if (tfm->__crt_alg->cra_type != &crypto_sig_type)
		return crypto_init_akcipher_ops_sig(tfm);

	return 0;
}

static void __maybe_unused crypto_sig_show(struct seq_file *m,
					   struct crypto_alg *alg)
{
	seq_puts(m, "type         : sig\n");
}

static int __maybe_unused crypto_sig_report(struct sk_buff *skb,
					    struct crypto_alg *alg)
{
	struct crypto_report_akcipher rsig = {};

	strscpy(rsig.type, "sig", sizeof(rsig.type));

	return nla_put(skb, CRYPTOCFGA_REPORT_AKCIPHER, sizeof(rsig), &rsig);
}

static int __maybe_unused crypto_sig_report_stat(struct sk_buff *skb,
						 struct crypto_alg *alg)
{
	struct crypto_stat_akcipher rsig = {};

	strscpy(rsig.type, "sig", sizeof(rsig.type));

	return nla_put(skb, CRYPTOCFGA_STAT_AKCIPHER, sizeof(rsig), &rsig);
}

static const struct crypto_type crypto_sig_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_sig_init_tfm,
#ifdef CONFIG_PROC_FS
	.show = crypto_sig_show,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_USER)
	.report = crypto_sig_report,
#endif
#ifdef CONFIG_CRYPTO_STATS
	.report_stat = crypto_sig_report_stat,
#endif
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_SIG_MASK,
	.type = CRYPTO_ALG_TYPE_SIG,
	.tfmsize = offsetof(struct crypto_sig, base),
};

struct crypto_sig *crypto_alloc_sig(const char *alg_name, u32 type, u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_sig_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_sig);

int crypto_sig_maxsize(struct crypto_sig *tfm)
{
	struct crypto_akcipher **ctx = crypto_sig_ctx(tfm);

	return crypto_akcipher_maxsize(*ctx);
}
EXPORT_SYMBOL_GPL(crypto_sig_maxsize);

int crypto_sig_sign(struct crypto_sig *tfm,
		    const void *src, unsigned int slen,
		    void *dst, unsigned int dlen)
{
	struct crypto_akcipher **ctx = crypto_sig_ctx(tfm);
	struct crypto_akcipher_sync_data data = {
		.tfm = *ctx,
		.src = src,
		.dst = dst,
		.slen = slen,
		.dlen = dlen,
	};

	return crypto_akcipher_sync_prep(&data) ?:
	       crypto_akcipher_sync_post(&data,
					 crypto_akcipher_sign(data.req));
}
EXPORT_SYMBOL_GPL(crypto_sig_sign);

int crypto_sig_verify(struct crypto_sig *tfm,
		      const void *src, unsigned int slen,
		      const void *digest, unsigned int dlen)
{
	struct crypto_akcipher **ctx = crypto_sig_ctx(tfm);
	struct crypto_akcipher_sync_data data = {
		.tfm = *ctx,
		.src = src,
		.slen = slen,
		.dlen = dlen,
	};
	int err;

	err = crypto_akcipher_sync_prep(&data);
	if (err)
		return err;

	memcpy(data.buf + slen, digest, dlen);

	return crypto_akcipher_sync_post(&data,
					 crypto_akcipher_verify(data.req));
}
EXPORT_SYMBOL_GPL(crypto_sig_verify);

int crypto_sig_set_pubkey(struct crypto_sig *tfm,
			  const void *key, unsigned int keylen)
{
	struct crypto_akcipher **ctx = crypto_sig_ctx(tfm);

	return crypto_akcipher_set_pub_key(*ctx, key, keylen);
}
EXPORT_SYMBOL_GPL(crypto_sig_set_pubkey);

int crypto_sig_set_privkey(struct crypto_sig *tfm,
			  const void *key, unsigned int keylen)
{
	struct crypto_akcipher **ctx = crypto_sig_ctx(tfm);

	return crypto_akcipher_set_priv_key(*ctx, key, keylen);
}
EXPORT_SYMBOL_GPL(crypto_sig_set_privkey);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Public Key Signature Algorithms");
