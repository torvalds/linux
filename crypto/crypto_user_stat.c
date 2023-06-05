// SPDX-License-Identifier: GPL-2.0
/*
 * Crypto user configuration API.
 *
 * Copyright (C) 2017-2018 Corentin Labbe <clabbe@baylibre.com>
 *
 */

#include <crypto/algapi.h>
#include <crypto/internal/cryptouser.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <net/netlink.h>
#include <net/sock.h>

#define null_terminated(x)	(strnlen(x, sizeof(x)) < sizeof(x))

struct crypto_dump_info {
	struct sk_buff *in_skb;
	struct sk_buff *out_skb;
	u32 nlmsg_seq;
	u16 nlmsg_flags;
};

static int crypto_report_cipher(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_stat_cipher rcipher;

	memset(&rcipher, 0, sizeof(rcipher));

	strscpy(rcipher.type, "cipher", sizeof(rcipher.type));

	return nla_put(skb, CRYPTOCFGA_STAT_CIPHER, sizeof(rcipher), &rcipher);
}

static int crypto_report_comp(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_stat_compress rcomp;

	memset(&rcomp, 0, sizeof(rcomp));

	strscpy(rcomp.type, "compression", sizeof(rcomp.type));

	return nla_put(skb, CRYPTOCFGA_STAT_COMPRESS, sizeof(rcomp), &rcomp);
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
		struct crypto_stat_larval rl;

		memset(&rl, 0, sizeof(rl));
		strscpy(rl.type, "larval", sizeof(rl.type));
		if (nla_put(skb, CRYPTOCFGA_STAT_LARVAL, sizeof(rl), &rl))
			goto nla_put_failure;
		goto out;
	}

	if (alg->cra_type && alg->cra_type->report_stat) {
		if (alg->cra_type->report_stat(skb, alg))
			goto nla_put_failure;
		goto out;
	}

	switch (alg->cra_flags & (CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_LARVAL)) {
	case CRYPTO_ALG_TYPE_CIPHER:
		if (crypto_report_cipher(skb, alg))
			goto nla_put_failure;
		break;
	case CRYPTO_ALG_TYPE_COMPRESS:
		if (crypto_report_comp(skb, alg))
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
	struct net *net = sock_net(in_skb->sk);
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

	if (err) {
		kfree_skb(skb);
		return err;
	}

	return nlmsg_unicast(net->crypto_nlsk, skb, NETLINK_CB(in_skb).portid);
}

MODULE_LICENSE("GPL");
