/*
 * Crypto user configuration API.
 *
 * Copyright (C) 2011 secunet Security Networks AG
 * Copyright (C) 2011 Steffen Klassert <steffen.klassert@secunet.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/cryptouser.h>
#include <linux/sched.h>
#include <net/netlink.h>
#include <linux/security.h>
#include <net/net_namespace.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/internal/rng.h>
#include <crypto/akcipher.h>

#include "internal.h"

#define null_terminated(x)	(strnlen(x, sizeof(x)) < sizeof(x))

static DEFINE_MUTEX(crypto_cfg_mutex);

/* The crypto netlink socket */
static struct sock *crypto_nlsk;

struct crypto_dump_info {
	struct sk_buff *in_skb;
	struct sk_buff *out_skb;
	u32 nlmsg_seq;
	u16 nlmsg_flags;
};

static struct crypto_alg *crypto_alg_match(struct crypto_user_alg *p, int exact)
{
	struct crypto_alg *q, *alg = NULL;

	down_read(&crypto_alg_sem);

	list_for_each_entry(q, &crypto_alg_list, cra_list) {
		int match = 0;

		if ((q->cra_flags ^ p->cru_type) & p->cru_mask)
			continue;

		if (strlen(p->cru_driver_name))
			match = !strcmp(q->cra_driver_name,
					p->cru_driver_name);
		else if (!exact)
			match = !strcmp(q->cra_name, p->cru_name);

		if (!match)
			continue;

		if (unlikely(!crypto_mod_get(q)))
			continue;

		alg = q;
		break;
	}

	up_read(&crypto_alg_sem);

	return alg;
}

static int crypto_report_cipher(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_cipher rcipher;

	strncpy(rcipher.type, "cipher", sizeof(rcipher.type));

	rcipher.blocksize = alg->cra_blocksize;
	rcipher.min_keysize = alg->cra_cipher.cia_min_keysize;
	rcipher.max_keysize = alg->cra_cipher.cia_max_keysize;

	if (nla_put(skb, CRYPTOCFGA_REPORT_CIPHER,
		    sizeof(struct crypto_report_cipher), &rcipher))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int crypto_report_comp(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_comp rcomp;

	strncpy(rcomp.type, "compression", sizeof(rcomp.type));
	if (nla_put(skb, CRYPTOCFGA_REPORT_COMPRESS,
		    sizeof(struct crypto_report_comp), &rcomp))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int crypto_report_akcipher(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_akcipher rakcipher;

	strncpy(rakcipher.type, "akcipher", sizeof(rakcipher.type));

	if (nla_put(skb, CRYPTOCFGA_REPORT_AKCIPHER,
		    sizeof(struct crypto_report_akcipher), &rakcipher))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int crypto_report_one(struct crypto_alg *alg,
			     struct crypto_user_alg *ualg, struct sk_buff *skb)
{
	strncpy(ualg->cru_name, alg->cra_name, sizeof(ualg->cru_name));
	strncpy(ualg->cru_driver_name, alg->cra_driver_name,
		sizeof(ualg->cru_driver_name));
	strncpy(ualg->cru_module_name, module_name(alg->cra_module),
		sizeof(ualg->cru_module_name));

	ualg->cru_type = 0;
	ualg->cru_mask = 0;
	ualg->cru_flags = alg->cra_flags;
	ualg->cru_refcnt = atomic_read(&alg->cra_refcnt);

	if (nla_put_u32(skb, CRYPTOCFGA_PRIORITY_VAL, alg->cra_priority))
		goto nla_put_failure;
	if (alg->cra_flags & CRYPTO_ALG_LARVAL) {
		struct crypto_report_larval rl;

		strncpy(rl.type, "larval", sizeof(rl.type));
		if (nla_put(skb, CRYPTOCFGA_REPORT_LARVAL,
			    sizeof(struct crypto_report_larval), &rl))
			goto nla_put_failure;
		goto out;
	}

	if (alg->cra_type && alg->cra_type->report) {
		if (alg->cra_type->report(skb, alg))
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

	case CRYPTO_ALG_TYPE_AKCIPHER:
		if (crypto_report_akcipher(skb, alg))
			goto nla_put_failure;

		break;
	}

out:
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int crypto_report_alg(struct crypto_alg *alg,
			     struct crypto_dump_info *info)
{
	struct sk_buff *in_skb = info->in_skb;
	struct sk_buff *skb = info->out_skb;
	struct nlmsghdr *nlh;
	struct crypto_user_alg *ualg;
	int err = 0;

	nlh = nlmsg_put(skb, NETLINK_CB(in_skb).portid, info->nlmsg_seq,
			CRYPTO_MSG_GETALG, sizeof(*ualg), info->nlmsg_flags);
	if (!nlh) {
		err = -EMSGSIZE;
		goto out;
	}

	ualg = nlmsg_data(nlh);

	err = crypto_report_one(alg, ualg, skb);
	if (err) {
		nlmsg_cancel(skb, nlh);
		goto out;
	}

	nlmsg_end(skb, nlh);

out:
	return err;
}

static int crypto_report(struct sk_buff *in_skb, struct nlmsghdr *in_nlh,
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

	err = crypto_report_alg(alg, &info);

drop_alg:
	crypto_mod_put(alg);

	if (err)
		return err;

	return nlmsg_unicast(crypto_nlsk, skb, NETLINK_CB(in_skb).portid);
}

static int crypto_dump_report(struct sk_buff *skb, struct netlink_callback *cb)
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
		err = crypto_report_alg(alg, &info);
		if (err)
			goto out_err;
	}

out:
	return skb->len;
out_err:
	return err;
}

static int crypto_dump_report_done(struct netlink_callback *cb)
{
	return 0;
}

static int crypto_update_alg(struct sk_buff *skb, struct nlmsghdr *nlh,
			     struct nlattr **attrs)
{
	struct crypto_alg *alg;
	struct crypto_user_alg *p = nlmsg_data(nlh);
	struct nlattr *priority = attrs[CRYPTOCFGA_PRIORITY_VAL];
	LIST_HEAD(list);

	if (!netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if (!null_terminated(p->cru_name) || !null_terminated(p->cru_driver_name))
		return -EINVAL;

	if (priority && !strlen(p->cru_driver_name))
		return -EINVAL;

	alg = crypto_alg_match(p, 1);
	if (!alg)
		return -ENOENT;

	down_write(&crypto_alg_sem);

	crypto_remove_spawns(alg, &list, NULL);

	if (priority)
		alg->cra_priority = nla_get_u32(priority);

	up_write(&crypto_alg_sem);

	crypto_mod_put(alg);
	crypto_remove_final(&list);

	return 0;
}

static int crypto_del_alg(struct sk_buff *skb, struct nlmsghdr *nlh,
			  struct nlattr **attrs)
{
	struct crypto_alg *alg;
	struct crypto_user_alg *p = nlmsg_data(nlh);
	int err;

	if (!netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if (!null_terminated(p->cru_name) || !null_terminated(p->cru_driver_name))
		return -EINVAL;

	alg = crypto_alg_match(p, 1);
	if (!alg)
		return -ENOENT;

	/* We can not unregister core algorithms such as aes-generic.
	 * We would loose the reference in the crypto_alg_list to this algorithm
	 * if we try to unregister. Unregistering such an algorithm without
	 * removing the module is not possible, so we restrict to crypto
	 * instances that are build from templates. */
	err = -EINVAL;
	if (!(alg->cra_flags & CRYPTO_ALG_INSTANCE))
		goto drop_alg;

	err = -EBUSY;
	if (atomic_read(&alg->cra_refcnt) > 2)
		goto drop_alg;

	err = crypto_unregister_instance((struct crypto_instance *)alg);

drop_alg:
	crypto_mod_put(alg);
	return err;
}

static struct crypto_alg *crypto_user_skcipher_alg(const char *name, u32 type,
						   u32 mask)
{
	int err;
	struct crypto_alg *alg;

	type = crypto_skcipher_type(type);
	mask = crypto_skcipher_mask(mask);

	for (;;) {
		alg = crypto_lookup_skcipher(name,  type, mask);
		if (!IS_ERR(alg))
			return alg;

		err = PTR_ERR(alg);
		if (err != -EAGAIN)
			break;
		if (signal_pending(current)) {
			err = -EINTR;
			break;
		}
	}

	return ERR_PTR(err);
}

static struct crypto_alg *crypto_user_aead_alg(const char *name, u32 type,
					       u32 mask)
{
	int err;
	struct crypto_alg *alg;

	type &= ~(CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_GENIV);
	type |= CRYPTO_ALG_TYPE_AEAD;
	mask &= ~(CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_GENIV);
	mask |= CRYPTO_ALG_TYPE_MASK;

	for (;;) {
		alg = crypto_lookup_aead(name,  type, mask);
		if (!IS_ERR(alg))
			return alg;

		err = PTR_ERR(alg);
		if (err != -EAGAIN)
			break;
		if (signal_pending(current)) {
			err = -EINTR;
			break;
		}
	}

	return ERR_PTR(err);
}

static int crypto_add_alg(struct sk_buff *skb, struct nlmsghdr *nlh,
			  struct nlattr **attrs)
{
	int exact = 0;
	const char *name;
	struct crypto_alg *alg;
	struct crypto_user_alg *p = nlmsg_data(nlh);
	struct nlattr *priority = attrs[CRYPTOCFGA_PRIORITY_VAL];

	if (!netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if (!null_terminated(p->cru_name) || !null_terminated(p->cru_driver_name))
		return -EINVAL;

	if (strlen(p->cru_driver_name))
		exact = 1;

	if (priority && !exact)
		return -EINVAL;

	alg = crypto_alg_match(p, exact);
	if (alg) {
		crypto_mod_put(alg);
		return -EEXIST;
	}

	if (strlen(p->cru_driver_name))
		name = p->cru_driver_name;
	else
		name = p->cru_name;

	switch (p->cru_type & p->cru_mask & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_AEAD:
		alg = crypto_user_aead_alg(name, p->cru_type, p->cru_mask);
		break;
	case CRYPTO_ALG_TYPE_GIVCIPHER:
	case CRYPTO_ALG_TYPE_BLKCIPHER:
	case CRYPTO_ALG_TYPE_ABLKCIPHER:
		alg = crypto_user_skcipher_alg(name, p->cru_type, p->cru_mask);
		break;
	default:
		alg = crypto_alg_mod_lookup(name, p->cru_type, p->cru_mask);
	}

	if (IS_ERR(alg))
		return PTR_ERR(alg);

	down_write(&crypto_alg_sem);

	if (priority)
		alg->cra_priority = nla_get_u32(priority);

	up_write(&crypto_alg_sem);

	crypto_mod_put(alg);

	return 0;
}

static int crypto_del_rng(struct sk_buff *skb, struct nlmsghdr *nlh,
			  struct nlattr **attrs)
{
	if (!netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;
	return crypto_del_default_rng();
}

#define MSGSIZE(type) sizeof(struct type)

static const int crypto_msg_min[CRYPTO_NR_MSGTYPES] = {
	[CRYPTO_MSG_NEWALG	- CRYPTO_MSG_BASE] = MSGSIZE(crypto_user_alg),
	[CRYPTO_MSG_DELALG	- CRYPTO_MSG_BASE] = MSGSIZE(crypto_user_alg),
	[CRYPTO_MSG_UPDATEALG	- CRYPTO_MSG_BASE] = MSGSIZE(crypto_user_alg),
	[CRYPTO_MSG_DELRNG	- CRYPTO_MSG_BASE] = 0,
};

static const struct nla_policy crypto_policy[CRYPTOCFGA_MAX+1] = {
	[CRYPTOCFGA_PRIORITY_VAL]   = { .type = NLA_U32},
};

#undef MSGSIZE

static const struct crypto_link {
	int (*doit)(struct sk_buff *, struct nlmsghdr *, struct nlattr **);
	int (*dump)(struct sk_buff *, struct netlink_callback *);
	int (*done)(struct netlink_callback *);
} crypto_dispatch[CRYPTO_NR_MSGTYPES] = {
	[CRYPTO_MSG_NEWALG	- CRYPTO_MSG_BASE] = { .doit = crypto_add_alg},
	[CRYPTO_MSG_DELALG	- CRYPTO_MSG_BASE] = { .doit = crypto_del_alg},
	[CRYPTO_MSG_UPDATEALG	- CRYPTO_MSG_BASE] = { .doit = crypto_update_alg},
	[CRYPTO_MSG_GETALG	- CRYPTO_MSG_BASE] = { .doit = crypto_report,
						       .dump = crypto_dump_report,
						       .done = crypto_dump_report_done},
	[CRYPTO_MSG_DELRNG	- CRYPTO_MSG_BASE] = { .doit = crypto_del_rng },
};

static int crypto_user_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct nlattr *attrs[CRYPTOCFGA_MAX+1];
	const struct crypto_link *link;
	int type, err;

	type = nlh->nlmsg_type;
	if (type > CRYPTO_MSG_MAX)
		return -EINVAL;

	type -= CRYPTO_MSG_BASE;
	link = &crypto_dispatch[type];

	if ((type == (CRYPTO_MSG_GETALG - CRYPTO_MSG_BASE) &&
	    (nlh->nlmsg_flags & NLM_F_DUMP))) {
		struct crypto_alg *alg;
		u16 dump_alloc = 0;

		if (link->dump == NULL)
			return -EINVAL;

		list_for_each_entry(alg, &crypto_alg_list, cra_list)
			dump_alloc += CRYPTO_REPORT_MAXSIZE;

		{
			struct netlink_dump_control c = {
				.dump = link->dump,
				.done = link->done,
				.min_dump_alloc = dump_alloc,
			};
			return netlink_dump_start(crypto_nlsk, skb, nlh, &c);
		}
	}

	err = nlmsg_parse(nlh, crypto_msg_min[type], attrs, CRYPTOCFGA_MAX,
			  crypto_policy);
	if (err < 0)
		return err;

	if (link->doit == NULL)
		return -EINVAL;

	return link->doit(skb, nlh, attrs);
}

static void crypto_netlink_rcv(struct sk_buff *skb)
{
	mutex_lock(&crypto_cfg_mutex);
	netlink_rcv_skb(skb, &crypto_user_rcv_msg);
	mutex_unlock(&crypto_cfg_mutex);
}

static int __init crypto_user_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input	= crypto_netlink_rcv,
	};

	crypto_nlsk = netlink_kernel_create(&init_net, NETLINK_CRYPTO, &cfg);
	if (!crypto_nlsk)
		return -ENOMEM;

	return 0;
}

static void __exit crypto_user_exit(void)
{
	netlink_kernel_release(crypto_nlsk);
}

module_init(crypto_user_init);
module_exit(crypto_user_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Steffen Klassert <steffen.klassert@secunet.com>");
MODULE_DESCRIPTION("Crypto userspace configuration API");
MODULE_ALIAS("net-pf-16-proto-21");
