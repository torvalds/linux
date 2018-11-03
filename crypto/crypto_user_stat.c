// SPDX-License-Identifier: GPL-2.0
/*
 * Crypto user configuration API.
 *
 * Copyright (C) 2017-2018 Corentin Labbe <clabbe@baylibre.com>
 *
 */

#include <linux/crypto.h>
#include <linux/cryptouser.h>
#include <linux/sched.h>
#include <net/netlink.h>
#include <crypto/internal/skcipher.h>
#include <crypto/internal/rng.h>
#include <crypto/akcipher.h>
#include <crypto/kpp.h>
#include <crypto/internal/cryptouser.h>

#include "internal.h"

#define null_terminated(x)	(strnlen(x, sizeof(x)) < sizeof(x))

static DEFINE_MUTEX(crypto_cfg_mutex);

extern struct sock *crypto_nlsk;

struct crypto_dump_info {
	struct sk_buff *in_skb;
	struct sk_buff *out_skb;
	u32 nlmsg_seq;
	u16 nlmsg_flags;
};

static int crypto_report_aead(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_stat raead;
	u64 v64;
	u32 v32;

	memset(&raead, 0, sizeof(raead));

	strscpy(raead.type, "aead", sizeof(raead.type));

	v32 = atomic_read(&alg->encrypt_cnt);
	raead.stat_encrypt_cnt = v32;
	v64 = atomic64_read(&alg->encrypt_tlen);
	raead.stat_encrypt_tlen = v64;
	v32 = atomic_read(&alg->decrypt_cnt);
	raead.stat_decrypt_cnt = v32;
	v64 = atomic64_read(&alg->decrypt_tlen);
	raead.stat_decrypt_tlen = v64;
	v32 = atomic_read(&alg->aead_err_cnt);
	raead.stat_aead_err_cnt = v32;

	return nla_put(skb, CRYPTOCFGA_STAT_AEAD, sizeof(raead), &raead);
}

static int crypto_report_cipher(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_stat rcipher;
	u64 v64;
	u32 v32;

	memset(&rcipher, 0, sizeof(rcipher));

	strscpy(rcipher.type, "cipher", sizeof(rcipher.type));

	v32 = atomic_read(&alg->encrypt_cnt);
	rcipher.stat_encrypt_cnt = v32;
	v64 = atomic64_read(&alg->encrypt_tlen);
	rcipher.stat_encrypt_tlen = v64;
	v32 = atomic_read(&alg->decrypt_cnt);
	rcipher.stat_decrypt_cnt = v32;
	v64 = atomic64_read(&alg->decrypt_tlen);
	rcipher.stat_decrypt_tlen = v64;
	v32 = atomic_read(&alg->cipher_err_cnt);
	rcipher.stat_cipher_err_cnt = v32;

	return nla_put(skb, CRYPTOCFGA_STAT_CIPHER, sizeof(rcipher), &rcipher);
}

static int crypto_report_comp(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_stat rcomp;
	u64 v64;
	u32 v32;

	memset(&rcomp, 0, sizeof(rcomp));

	strscpy(rcomp.type, "compression", sizeof(rcomp.type));
	v32 = atomic_read(&alg->compress_cnt);
	rcomp.stat_compress_cnt = v32;
	v64 = atomic64_read(&alg->compress_tlen);
	rcomp.stat_compress_tlen = v64;
	v32 = atomic_read(&alg->decompress_cnt);
	rcomp.stat_decompress_cnt = v32;
	v64 = atomic64_read(&alg->decompress_tlen);
	rcomp.stat_decompress_tlen = v64;
	v32 = atomic_read(&alg->cipher_err_cnt);
	rcomp.stat_compress_err_cnt = v32;

	return nla_put(skb, CRYPTOCFGA_STAT_COMPRESS, sizeof(rcomp), &rcomp);
}

static int crypto_report_acomp(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_stat racomp;
	u64 v64;
	u32 v32;

	memset(&racomp, 0, sizeof(racomp));

	strscpy(racomp.type, "acomp", sizeof(racomp.type));
	v32 = atomic_read(&alg->compress_cnt);
	racomp.stat_compress_cnt = v32;
	v64 = atomic64_read(&alg->compress_tlen);
	racomp.stat_compress_tlen = v64;
	v32 = atomic_read(&alg->decompress_cnt);
	racomp.stat_decompress_cnt = v32;
	v64 = atomic64_read(&alg->decompress_tlen);
	racomp.stat_decompress_tlen = v64;
	v32 = atomic_read(&alg->cipher_err_cnt);
	racomp.stat_compress_err_cnt = v32;

	return nla_put(skb, CRYPTOCFGA_STAT_ACOMP, sizeof(racomp), &racomp);
}

static int crypto_report_akcipher(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_stat rakcipher;
	u64 v64;
	u32 v32;

	memset(&rakcipher, 0, sizeof(rakcipher));

	strscpy(rakcipher.type, "akcipher", sizeof(rakcipher.type));
	v32 = atomic_read(&alg->encrypt_cnt);
	rakcipher.stat_encrypt_cnt = v32;
	v64 = atomic64_read(&alg->encrypt_tlen);
	rakcipher.stat_encrypt_tlen = v64;
	v32 = atomic_read(&alg->decrypt_cnt);
	rakcipher.stat_decrypt_cnt = v32;
	v64 = atomic64_read(&alg->decrypt_tlen);
	rakcipher.stat_decrypt_tlen = v64;
	v32 = atomic_read(&alg->sign_cnt);
	rakcipher.stat_sign_cnt = v32;
	v32 = atomic_read(&alg->verify_cnt);
	rakcipher.stat_verify_cnt = v32;
	v32 = atomic_read(&alg->akcipher_err_cnt);
	rakcipher.stat_akcipher_err_cnt = v32;

	return nla_put(skb, CRYPTOCFGA_STAT_AKCIPHER,
		       sizeof(rakcipher), &rakcipher);
}

static int crypto_report_kpp(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_stat rkpp;
	u32 v;

	memset(&rkpp, 0, sizeof(rkpp));

	strscpy(rkpp.type, "kpp", sizeof(rkpp.type));

	v = atomic_read(&alg->setsecret_cnt);
	rkpp.stat_setsecret_cnt = v;
	v = atomic_read(&alg->generate_public_key_cnt);
	rkpp.stat_generate_public_key_cnt = v;
	v = atomic_read(&alg->compute_shared_secret_cnt);
	rkpp.stat_compute_shared_secret_cnt = v;
	v = atomic_read(&alg->kpp_err_cnt);
	rkpp.stat_kpp_err_cnt = v;

	return nla_put(skb, CRYPTOCFGA_STAT_KPP, sizeof(rkpp), &rkpp);
}

static int crypto_report_ahash(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_stat rhash;
	u64 v64;
	u32 v32;

	memset(&rhash, 0, sizeof(rhash));

	strscpy(rhash.type, "ahash", sizeof(rhash.type));

	v32 = atomic_read(&alg->hash_cnt);
	rhash.stat_hash_cnt = v32;
	v64 = atomic64_read(&alg->hash_tlen);
	rhash.stat_hash_tlen = v64;
	v32 = atomic_read(&alg->hash_err_cnt);
	rhash.stat_hash_err_cnt = v32;

	return nla_put(skb, CRYPTOCFGA_STAT_HASH, sizeof(rhash), &rhash);
}

static int crypto_report_shash(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_stat rhash;
	u64 v64;
	u32 v32;

	memset(&rhash, 0, sizeof(rhash));

	strscpy(rhash.type, "shash", sizeof(rhash.type));

	v32 = atomic_read(&alg->hash_cnt);
	rhash.stat_hash_cnt = v32;
	v64 = atomic64_read(&alg->hash_tlen);
	rhash.stat_hash_tlen = v64;
	v32 = atomic_read(&alg->hash_err_cnt);
	rhash.stat_hash_err_cnt = v32;

	return nla_put(skb, CRYPTOCFGA_STAT_HASH, sizeof(rhash), &rhash);
}

static int crypto_report_rng(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_stat rrng;
	u64 v64;
	u32 v32;

	memset(&rrng, 0, sizeof(rrng));

	strscpy(rrng.type, "rng", sizeof(rrng.type));

	v32 = atomic_read(&alg->generate_cnt);
	rrng.stat_generate_cnt = v32;
	v64 = atomic64_read(&alg->generate_tlen);
	rrng.stat_generate_tlen = v64;
	v32 = atomic_read(&alg->seed_cnt);
	rrng.stat_seed_cnt = v32;
	v32 = atomic_read(&alg->hash_err_cnt);
	rrng.stat_rng_err_cnt = v32;

	return nla_put(skb, CRYPTOCFGA_STAT_RNG, sizeof(rrng), &rrng);
}

static int crypto_reportstat_one(struct crypto_alg *alg,
				 struct crypto_user_alg *ualg,
				 struct sk_buff *skb)
{
	memset(ualg, 0, sizeof(*ualg));

	strscpy(ualg->cru_name, alg->cra_name, sizeof(ualg->cru_name));
	strscpy(ualg->cru_driver_name, alg->cra_driver_name,
		sizeof(ualg->cru_driver_name));
	strscpy(ualg->cru_module_name, module_name(alg->cra_module),
		sizeof(ualg->cru_module_name));

	ualg->cru_type = 0;
	ualg->cru_mask = 0;
	ualg->cru_flags = alg->cra_flags;
	ualg->cru_refcnt = refcount_read(&alg->cra_refcnt);

	if (nla_put_u32(skb, CRYPTOCFGA_PRIORITY_VAL, alg->cra_priority))
		goto nla_put_failure;
	if (alg->cra_flags & CRYPTO_ALG_LARVAL) {
		struct crypto_stat rl;

		memset(&rl, 0, sizeof(rl));
		strscpy(rl.type, "larval", sizeof(rl.type));
		if (nla_put(skb, CRYPTOCFGA_STAT_LARVAL, sizeof(rl), &rl))
			goto nla_put_failure;
		goto out;
	}

	switch (alg->cra_flags & (CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_LARVAL)) {
	case CRYPTO_ALG_TYPE_AEAD:
		if (crypto_report_aead(skb, alg))
			goto nla_put_failure;
		break;
	case CRYPTO_ALG_TYPE_SKCIPHER:
		if (crypto_report_cipher(skb, alg))
			goto nla_put_failure;
		break;
	case CRYPTO_ALG_TYPE_BLKCIPHER:
		if (crypto_report_cipher(skb, alg))
			goto nla_put_failure;
		break;
	case CRYPTO_ALG_TYPE_CIPHER:
		if (crypto_report_cipher(skb, alg))
			goto nla_put_failure;
		break;
	case CRYPTO_ALG_TYPE_COMPRESS:
		if (crypto_report_comp(skb, alg))
			goto nla_put_failure;
		break;
	case CRYPTO_ALG_TYPE_ACOMPRESS:
		if (crypto_report_acomp(skb, alg))
			goto nla_put_failure;
		break;
	case CRYPTO_ALG_TYPE_SCOMPRESS:
		if (crypto_report_acomp(skb, alg))
			goto nla_put_failure;
		break;
	case CRYPTO_ALG_TYPE_AKCIPHER:
		if (crypto_report_akcipher(skb, alg))
			goto nla_put_failure;
		break;
	case CRYPTO_ALG_TYPE_KPP:
		if (crypto_report_kpp(skb, alg))
			goto nla_put_failure;
		break;
	case CRYPTO_ALG_TYPE_AHASH:
		if (crypto_report_ahash(skb, alg))
			goto nla_put_failure;
		break;
	case CRYPTO_ALG_TYPE_HASH:
		if (crypto_report_shash(skb, alg))
			goto nla_put_failure;
		break;
	case CRYPTO_ALG_TYPE_RNG:
		if (crypto_report_rng(skb, alg))
			goto nla_put_failure;
		break;
	default:
		pr_err("ERROR: Unhandled alg %d in %s\n",
		       alg->cra_flags & (CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_LARVAL),
		       __func__);
	}

out:
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int crypto_reportstat_alg(struct crypto_alg *alg,
				 struct crypto_dump_info *info)
{
	struct sk_buff *in_skb = info->in_skb;
	struct sk_buff *skb = info->out_skb;
	struct nlmsghdr *nlh;
	struct crypto_user_alg *ualg;
	int err = 0;

	nlh = nlmsg_put(skb, NETLINK_CB(in_skb).portid, info->nlmsg_seq,
			CRYPTO_MSG_GETSTAT, sizeof(*ualg), info->nlmsg_flags);
	if (!nlh) {
		err = -EMSGSIZE;
		goto out;
	}

	ualg = nlmsg_data(nlh);

	err = crypto_reportstat_one(alg, ualg, skb);
	if (err) {
		nlmsg_cancel(skb, nlh);
		goto out;
	}

	nlmsg_end(skb, nlh);

out:
	return err;
}

int crypto_reportstat(struct sk_buff *in_skb, struct nlmsghdr *in_nlh,
		      struct nlattr **attrs)
{
	struct crypto_user_alg *p = nlmsg_data(in_nlh);
	struct crypto_alg *alg;
	struct sk_buff *skb;
	struct crypto_dump_info info;
	int err;

	if (!null_terminated(p->cru_name) || !null_terminated(p->cru_driver_name))
		return -EINVAL;

	alg = crypto_alg_match(p, 0);
	if (!alg)
		return -ENOENT;

	err = -ENOMEM;
	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!skb)
		goto drop_alg;

	info.in_skb = in_skb;
	info.out_skb = skb;
	info.nlmsg_seq = in_nlh->nlmsg_seq;
	info.nlmsg_flags = 0;

	err = crypto_reportstat_alg(alg, &info);

drop_alg:
	crypto_mod_put(alg);

	if (err)
		return err;

	return nlmsg_unicast(crypto_nlsk, skb, NETLINK_CB(in_skb).portid);
}

int crypto_dump_reportstat(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct crypto_alg *alg;
	struct crypto_dump_info info;
	int err;

	if (cb->args[0])
		goto out;

	cb->args[0] = 1;

	info.in_skb = cb->skb;
	info.out_skb = skb;
	info.nlmsg_seq = cb->nlh->nlmsg_seq;
	info.nlmsg_flags = NLM_F_MULTI;

	list_for_each_entry(alg, &crypto_alg_list, cra_list) {
		err = crypto_reportstat_alg(alg, &info);
		if (err)
			goto out_err;
	}

out:
	return skb->len;
out_err:
	return err;
}

int crypto_dump_reportstat_done(struct netlink_callback *cb)
{
	return 0;
}

MODULE_LICENSE("GPL");
