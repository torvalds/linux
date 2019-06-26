/*
 * HWSIM IEEE 802.15.4 interface
 *
 * (C) 2018 Mojatau, Alexander Aring <aring@mojatau.com>
 * Copyright 2007-2012 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Based on fakelb, original Written by:
 * Sergey Lapin <slapin@ossfans.org>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#include <linux/module.h>
#include <linux/timer.h>
#include <linux/platform_device.h>
#include <linux/rtnetlink.h>
#include <linux/netdevice.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <net/mac802154.h>
#include <net/cfg802154.h>
#include <net/genetlink.h>
#include "mac802154_hwsim.h"

MODULE_DESCRIPTION("Software simulator of IEEE 802.15.4 radio(s) for mac802154");
MODULE_LICENSE("GPL");

static LIST_HEAD(hwsim_phys);
static DEFINE_MUTEX(hwsim_phys_lock);

static struct platform_device *mac802154hwsim_dev;

/* MAC802154_HWSIM netlink family */
static struct genl_family hwsim_genl_family;

static int hwsim_radio_idx;

enum hwsim_multicast_groups {
	HWSIM_MCGRP_CONFIG,
};

static const struct genl_multicast_group hwsim_mcgrps[] = {
	[HWSIM_MCGRP_CONFIG] = { .name = "config", },
};

struct hwsim_pib {
	u8 page;
	u8 channel;

	struct rcu_head rcu;
};

struct hwsim_edge_info {
	u8 lqi;

	struct rcu_head rcu;
};

struct hwsim_edge {
	struct hwsim_phy *endpoint;
	struct hwsim_edge_info __rcu *info;

	struct list_head list;
	struct rcu_head rcu;
};

struct hwsim_phy {
	struct ieee802154_hw *hw;
	u32 idx;

	struct hwsim_pib __rcu *pib;

	bool suspended;
	struct list_head edges;

	struct list_head list;
};

static int hwsim_add_one(struct genl_info *info, struct device *dev,
			 bool init);
static void hwsim_del(struct hwsim_phy *phy);

static int hwsim_hw_ed(struct ieee802154_hw *hw, u8 *level)
{
	*level = 0xbe;

	return 0;
}

static int hwsim_hw_channel(struct ieee802154_hw *hw, u8 page, u8 channel)
{
	struct hwsim_phy *phy = hw->priv;
	struct hwsim_pib *pib, *pib_old;

	pib = kzalloc(sizeof(*pib), GFP_KERNEL);
	if (!pib)
		return -ENOMEM;

	pib->page = page;
	pib->channel = channel;

	pib_old = rtnl_dereference(phy->pib);
	rcu_assign_pointer(phy->pib, pib);
	kfree_rcu(pib_old, rcu);
	return 0;
}

static int hwsim_hw_xmit(struct ieee802154_hw *hw, struct sk_buff *skb)
{
	struct hwsim_phy *current_phy = hw->priv;
	struct hwsim_pib *current_pib, *endpoint_pib;
	struct hwsim_edge_info *einfo;
	struct hwsim_edge *e;

	WARN_ON(current_phy->suspended);

	rcu_read_lock();
	current_pib = rcu_dereference(current_phy->pib);
	list_for_each_entry_rcu(e, &current_phy->edges, list) {
		/* Can be changed later in rx_irqsafe, but this is only a
		 * performance tweak. Received radio should drop the frame
		 * in mac802154 stack anyway... so we don't need to be
		 * 100% of locking here to check on suspended
		 */
		if (e->endpoint->suspended)
			continue;

		endpoint_pib = rcu_dereference(e->endpoint->pib);
		if (current_pib->page == endpoint_pib->page &&
		    current_pib->channel == endpoint_pib->channel) {
			struct sk_buff *newskb = pskb_copy(skb, GFP_ATOMIC);

			einfo = rcu_dereference(e->info);
			if (newskb)
				ieee802154_rx_irqsafe(e->endpoint->hw, newskb,
						      einfo->lqi);
		}
	}
	rcu_read_unlock();

	ieee802154_xmit_complete(hw, skb, false);
	return 0;
}

static int hwsim_hw_start(struct ieee802154_hw *hw)
{
	struct hwsim_phy *phy = hw->priv;

	phy->suspended = false;
	return 0;
}

static void hwsim_hw_stop(struct ieee802154_hw *hw)
{
	struct hwsim_phy *phy = hw->priv;

	phy->suspended = true;
}

static int
hwsim_set_promiscuous_mode(struct ieee802154_hw *hw, const bool on)
{
	return 0;
}

static const struct ieee802154_ops hwsim_ops = {
	.owner = THIS_MODULE,
	.xmit_async = hwsim_hw_xmit,
	.ed = hwsim_hw_ed,
	.set_channel = hwsim_hw_channel,
	.start = hwsim_hw_start,
	.stop = hwsim_hw_stop,
	.set_promiscuous_mode = hwsim_set_promiscuous_mode,
};

static int hwsim_new_radio_nl(struct sk_buff *msg, struct genl_info *info)
{
	return hwsim_add_one(info, &mac802154hwsim_dev->dev, false);
}

static int hwsim_del_radio_nl(struct sk_buff *msg, struct genl_info *info)
{
	struct hwsim_phy *phy, *tmp;
	s64 idx = -1;

	if (!info->attrs[MAC802154_HWSIM_ATTR_RADIO_ID])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[MAC802154_HWSIM_ATTR_RADIO_ID]);

	mutex_lock(&hwsim_phys_lock);
	list_for_each_entry_safe(phy, tmp, &hwsim_phys, list) {
		if (idx == phy->idx) {
			hwsim_del(phy);
			mutex_unlock(&hwsim_phys_lock);
			return 0;
		}
	}
	mutex_unlock(&hwsim_phys_lock);

	return -ENODEV;
}

static int append_radio_msg(struct sk_buff *skb, struct hwsim_phy *phy)
{
	struct nlattr *nl_edges, *nl_edge;
	struct hwsim_edge_info *einfo;
	struct hwsim_edge *e;
	int ret;

	ret = nla_put_u32(skb, MAC802154_HWSIM_ATTR_RADIO_ID, phy->idx);
	if (ret < 0)
		return ret;

	rcu_read_lock();
	if (list_empty(&phy->edges)) {
		rcu_read_unlock();
		return 0;
	}

	nl_edges = nla_nest_start(skb, MAC802154_HWSIM_ATTR_RADIO_EDGES);
	if (!nl_edges) {
		rcu_read_unlock();
		return -ENOBUFS;
	}

	list_for_each_entry_rcu(e, &phy->edges, list) {
		nl_edge = nla_nest_start(skb, MAC802154_HWSIM_ATTR_RADIO_EDGE);
		if (!nl_edge) {
			rcu_read_unlock();
			nla_nest_cancel(skb, nl_edges);
			return -ENOBUFS;
		}

		ret = nla_put_u32(skb, MAC802154_HWSIM_EDGE_ATTR_ENDPOINT_ID,
				  e->endpoint->idx);
		if (ret < 0) {
			rcu_read_unlock();
			nla_nest_cancel(skb, nl_edge);
			nla_nest_cancel(skb, nl_edges);
			return ret;
		}

		einfo = rcu_dereference(e->info);
		ret = nla_put_u8(skb, MAC802154_HWSIM_EDGE_ATTR_LQI,
				 einfo->lqi);
		if (ret < 0) {
			rcu_read_unlock();
			nla_nest_cancel(skb, nl_edge);
			nla_nest_cancel(skb, nl_edges);
			return ret;
		}

		nla_nest_end(skb, nl_edge);
	}
	rcu_read_unlock();

	nla_nest_end(skb, nl_edges);

	return 0;
}

static int hwsim_get_radio(struct sk_buff *skb, struct hwsim_phy *phy,
			   u32 portid, u32 seq,
			   struct netlink_callback *cb, int flags)
{
	void *hdr;
	int res = -EMSGSIZE;

	hdr = genlmsg_put(skb, portid, seq, &hwsim_genl_family, flags,
			  MAC802154_HWSIM_CMD_GET_RADIO);
	if (!hdr)
		return -EMSGSIZE;

	if (cb)
		genl_dump_check_consistent(cb, hdr);

	res = append_radio_msg(skb, phy);
	if (res < 0)
		goto out_err;

	genlmsg_end(skb, hdr);
	return 0;

out_err:
	genlmsg_cancel(skb, hdr);
	return res;
}

static int hwsim_get_radio_nl(struct sk_buff *msg, struct genl_info *info)
{
	struct hwsim_phy *phy;
	struct sk_buff *skb;
	int idx, res = -ENODEV;

	if (!info->attrs[MAC802154_HWSIM_ATTR_RADIO_ID])
		return -EINVAL;
	idx = nla_get_u32(info->attrs[MAC802154_HWSIM_ATTR_RADIO_ID]);

	mutex_lock(&hwsim_phys_lock);
	list_for_each_entry(phy, &hwsim_phys, list) {
		if (phy->idx != idx)
			continue;

		skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
		if (!skb) {
			res = -ENOMEM;
			goto out_err;
		}

		res = hwsim_get_radio(skb, phy, info->snd_portid,
				      info->snd_seq, NULL, 0);
		if (res < 0) {
			nlmsg_free(skb);
			goto out_err;
		}

		res = genlmsg_reply(skb, info);
		break;
	}

out_err:
	mutex_unlock(&hwsim_phys_lock);

	return res;
}

static int hwsim_dump_radio_nl(struct sk_buff *skb,
			       struct netlink_callback *cb)
{
	int idx = cb->args[0];
	struct hwsim_phy *phy;
	int res;

	mutex_lock(&hwsim_phys_lock);

	if (idx == hwsim_radio_idx)
		goto done;

	list_for_each_entry(phy, &hwsim_phys, list) {
		if (phy->idx < idx)
			continue;

		res = hwsim_get_radio(skb, phy, NETLINK_CB(cb->skb).portid,
				      cb->nlh->nlmsg_seq, cb, NLM_F_MULTI);
		if (res < 0)
			break;

		idx = phy->idx + 1;
	}

	cb->args[0] = idx;

done:
	mutex_unlock(&hwsim_phys_lock);
	return skb->len;
}

/* caller need to held hwsim_phys_lock */
static struct hwsim_phy *hwsim_get_radio_by_id(uint32_t idx)
{
	struct hwsim_phy *phy;

	list_for_each_entry(phy, &hwsim_phys, list) {
		if (phy->idx == idx)
			return phy;
	}

	return NULL;
}

static const struct nla_policy hwsim_edge_policy[MAC802154_HWSIM_EDGE_ATTR_MAX + 1] = {
	[MAC802154_HWSIM_EDGE_ATTR_ENDPOINT_ID] = { .type = NLA_U32 },
	[MAC802154_HWSIM_EDGE_ATTR_LQI] = { .type = NLA_U8 },
};

static struct hwsim_edge *hwsim_alloc_edge(struct hwsim_phy *endpoint, u8 lqi)
{
	struct hwsim_edge_info *einfo;
	struct hwsim_edge *e;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return NULL;

	einfo = kzalloc(sizeof(*einfo), GFP_KERNEL);
	if (!einfo) {
		kfree(e);
		return NULL;
	}

	einfo->lqi = 0xff;
	rcu_assign_pointer(e->info, einfo);
	e->endpoint = endpoint;

	return e;
}

static void hwsim_free_edge(struct hwsim_edge *e)
{
	struct hwsim_edge_info *einfo;

	rcu_read_lock();
	einfo = rcu_dereference(e->info);
	rcu_read_unlock();

	kfree_rcu(einfo, rcu);
	kfree_rcu(e, rcu);
}

static int hwsim_new_edge_nl(struct sk_buff *msg, struct genl_info *info)
{
	struct nlattr *edge_attrs[MAC802154_HWSIM_EDGE_ATTR_MAX + 1];
	struct hwsim_phy *phy_v0, *phy_v1;
	struct hwsim_edge *e;
	u32 v0, v1;

	if (!info->attrs[MAC802154_HWSIM_ATTR_RADIO_ID] &&
	    !info->attrs[MAC802154_HWSIM_ATTR_RADIO_EDGE])
		return -EINVAL;

	if (nla_parse_nested(edge_attrs, MAC802154_HWSIM_EDGE_ATTR_MAX,
			     info->attrs[MAC802154_HWSIM_ATTR_RADIO_EDGE],
			     hwsim_edge_policy, NULL))
		return -EINVAL;

	if (!edge_attrs[MAC802154_HWSIM_EDGE_ATTR_ENDPOINT_ID])
		return -EINVAL;

	v0 = nla_get_u32(info->attrs[MAC802154_HWSIM_ATTR_RADIO_ID]);
	v1 = nla_get_u32(edge_attrs[MAC802154_HWSIM_EDGE_ATTR_ENDPOINT_ID]);

	if (v0 == v1)
		return -EINVAL;

	mutex_lock(&hwsim_phys_lock);
	phy_v0 = hwsim_get_radio_by_id(v0);
	if (!phy_v0) {
		mutex_unlock(&hwsim_phys_lock);
		return -ENOENT;
	}

	phy_v1 = hwsim_get_radio_by_id(v1);
	if (!phy_v1) {
		mutex_unlock(&hwsim_phys_lock);
		return -ENOENT;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(e, &phy_v0->edges, list) {
		if (e->endpoint->idx == v1) {
			mutex_unlock(&hwsim_phys_lock);
			rcu_read_unlock();
			return -EEXIST;
		}
	}
	rcu_read_unlock();

	e = hwsim_alloc_edge(phy_v1, 0xff);
	if (!e) {
		mutex_unlock(&hwsim_phys_lock);
		return -ENOMEM;
	}
	list_add_rcu(&e->list, &phy_v0->edges);
	/* wait until changes are done under hwsim_phys_lock lock
	 * should prevent of calling this function twice while
	 * edges list has not the changes yet.
	 */
	synchronize_rcu();
	mutex_unlock(&hwsim_phys_lock);

	return 0;
}

static int hwsim_del_edge_nl(struct sk_buff *msg, struct genl_info *info)
{
	struct nlattr *edge_attrs[MAC802154_HWSIM_EDGE_ATTR_MAX + 1];
	struct hwsim_phy *phy_v0;
	struct hwsim_edge *e;
	u32 v0, v1;

	if (!info->attrs[MAC802154_HWSIM_ATTR_RADIO_ID] &&
	    !info->attrs[MAC802154_HWSIM_ATTR_RADIO_EDGE])
		return -EINVAL;

	if (nla_parse_nested(edge_attrs, MAC802154_HWSIM_EDGE_ATTR_MAX,
			     info->attrs[MAC802154_HWSIM_ATTR_RADIO_EDGE],
			     hwsim_edge_policy, NULL))
		return -EINVAL;

	if (!edge_attrs[MAC802154_HWSIM_EDGE_ATTR_ENDPOINT_ID])
		return -EINVAL;

	v0 = nla_get_u32(info->attrs[MAC802154_HWSIM_ATTR_RADIO_ID]);
	v1 = nla_get_u32(edge_attrs[MAC802154_HWSIM_EDGE_ATTR_ENDPOINT_ID]);

	mutex_lock(&hwsim_phys_lock);
	phy_v0 = hwsim_get_radio_by_id(v0);
	if (!phy_v0) {
		mutex_unlock(&hwsim_phys_lock);
		return -ENOENT;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(e, &phy_v0->edges, list) {
		if (e->endpoint->idx == v1) {
			rcu_read_unlock();
			list_del_rcu(&e->list);
			hwsim_free_edge(e);
			/* same again - wait until list changes are done */
			synchronize_rcu();
			mutex_unlock(&hwsim_phys_lock);
			return 0;
		}
	}
	rcu_read_unlock();

	mutex_unlock(&hwsim_phys_lock);

	return -ENOENT;
}

static int hwsim_set_edge_lqi(struct sk_buff *msg, struct genl_info *info)
{
	struct nlattr *edge_attrs[MAC802154_HWSIM_EDGE_ATTR_MAX + 1];
	struct hwsim_edge_info *einfo;
	struct hwsim_phy *phy_v0;
	struct hwsim_edge *e;
	u32 v0, v1;
	u8 lqi;

	if (!info->attrs[MAC802154_HWSIM_ATTR_RADIO_ID] &&
	    !info->attrs[MAC802154_HWSIM_ATTR_RADIO_EDGE])
		return -EINVAL;

	if (nla_parse_nested(edge_attrs, MAC802154_HWSIM_EDGE_ATTR_MAX,
			     info->attrs[MAC802154_HWSIM_ATTR_RADIO_EDGE],
			     hwsim_edge_policy, NULL))
		return -EINVAL;

	if (!edge_attrs[MAC802154_HWSIM_EDGE_ATTR_ENDPOINT_ID] &&
	    !edge_attrs[MAC802154_HWSIM_EDGE_ATTR_LQI])
		return -EINVAL;

	v0 = nla_get_u32(info->attrs[MAC802154_HWSIM_ATTR_RADIO_ID]);
	v1 = nla_get_u32(edge_attrs[MAC802154_HWSIM_EDGE_ATTR_ENDPOINT_ID]);
	lqi = nla_get_u8(edge_attrs[MAC802154_HWSIM_EDGE_ATTR_LQI]);

	mutex_lock(&hwsim_phys_lock);
	phy_v0 = hwsim_get_radio_by_id(v0);
	if (!phy_v0) {
		mutex_unlock(&hwsim_phys_lock);
		return -ENOENT;
	}

	einfo = kzalloc(sizeof(*einfo), GFP_KERNEL);
	if (!einfo) {
		mutex_unlock(&hwsim_phys_lock);
		return -ENOMEM;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(e, &phy_v0->edges, list) {
		if (e->endpoint->idx == v1) {
			einfo->lqi = lqi;
			rcu_assign_pointer(e->info, einfo);
			rcu_read_unlock();
			mutex_unlock(&hwsim_phys_lock);
			return 0;
		}
	}
	rcu_read_unlock();

	kfree(einfo);
	mutex_unlock(&hwsim_phys_lock);

	return -ENOENT;
}

/* MAC802154_HWSIM netlink policy */

static const struct nla_policy hwsim_genl_policy[MAC802154_HWSIM_ATTR_MAX + 1] = {
	[MAC802154_HWSIM_ATTR_RADIO_ID] = { .type = NLA_U32 },
	[MAC802154_HWSIM_ATTR_RADIO_EDGE] = { .type = NLA_NESTED },
	[MAC802154_HWSIM_ATTR_RADIO_EDGES] = { .type = NLA_NESTED },
};

/* Generic Netlink operations array */
static const struct genl_ops hwsim_nl_ops[] = {
	{
		.cmd = MAC802154_HWSIM_CMD_NEW_RADIO,
		.policy = hwsim_genl_policy,
		.doit = hwsim_new_radio_nl,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = MAC802154_HWSIM_CMD_DEL_RADIO,
		.policy = hwsim_genl_policy,
		.doit = hwsim_del_radio_nl,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = MAC802154_HWSIM_CMD_GET_RADIO,
		.policy = hwsim_genl_policy,
		.doit = hwsim_get_radio_nl,
		.dumpit = hwsim_dump_radio_nl,
	},
	{
		.cmd = MAC802154_HWSIM_CMD_NEW_EDGE,
		.policy = hwsim_genl_policy,
		.doit = hwsim_new_edge_nl,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = MAC802154_HWSIM_CMD_DEL_EDGE,
		.policy = hwsim_genl_policy,
		.doit = hwsim_del_edge_nl,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = MAC802154_HWSIM_CMD_SET_EDGE,
		.policy = hwsim_genl_policy,
		.doit = hwsim_set_edge_lqi,
		.flags = GENL_UNS_ADMIN_PERM,
	},
};

static struct genl_family hwsim_genl_family __ro_after_init = {
	.name = "MAC802154_HWSIM",
	.version = 1,
	.maxattr = MAC802154_HWSIM_ATTR_MAX,
	.module = THIS_MODULE,
	.ops = hwsim_nl_ops,
	.n_ops = ARRAY_SIZE(hwsim_nl_ops),
	.mcgrps = hwsim_mcgrps,
	.n_mcgrps = ARRAY_SIZE(hwsim_mcgrps),
};

static void hwsim_mcast_config_msg(struct sk_buff *mcast_skb,
				   struct genl_info *info)
{
	if (info)
		genl_notify(&hwsim_genl_family, mcast_skb, info,
			    HWSIM_MCGRP_CONFIG, GFP_KERNEL);
	else
		genlmsg_multicast(&hwsim_genl_family, mcast_skb, 0,
				  HWSIM_MCGRP_CONFIG, GFP_KERNEL);
}

static void hwsim_mcast_new_radio(struct genl_info *info, struct hwsim_phy *phy)
{
	struct sk_buff *mcast_skb;
	void *data;

	mcast_skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!mcast_skb)
		return;

	data = genlmsg_put(mcast_skb, 0, 0, &hwsim_genl_family, 0,
			   MAC802154_HWSIM_CMD_NEW_RADIO);
	if (!data)
		goto out_err;

	if (append_radio_msg(mcast_skb, phy) < 0)
		goto out_err;

	genlmsg_end(mcast_skb, data);

	hwsim_mcast_config_msg(mcast_skb, info);
	return;

out_err:
	genlmsg_cancel(mcast_skb, data);
	nlmsg_free(mcast_skb);
}

static void hwsim_edge_unsubscribe_me(struct hwsim_phy *phy)
{
	struct hwsim_phy *tmp;
	struct hwsim_edge *e;

	rcu_read_lock();
	/* going to all phy edges and remove phy from it */
	list_for_each_entry(tmp, &hwsim_phys, list) {
		list_for_each_entry_rcu(e, &tmp->edges, list) {
			if (e->endpoint->idx == phy->idx) {
				list_del_rcu(&e->list);
				hwsim_free_edge(e);
			}
		}
	}
	rcu_read_unlock();

	synchronize_rcu();
}

static int hwsim_subscribe_all_others(struct hwsim_phy *phy)
{
	struct hwsim_phy *sub;
	struct hwsim_edge *e;

	list_for_each_entry(sub, &hwsim_phys, list) {
		e = hwsim_alloc_edge(sub, 0xff);
		if (!e)
			goto me_fail;

		list_add_rcu(&e->list, &phy->edges);
	}

	list_for_each_entry(sub, &hwsim_phys, list) {
		e = hwsim_alloc_edge(phy, 0xff);
		if (!e)
			goto sub_fail;

		list_add_rcu(&e->list, &sub->edges);
	}

	return 0;

me_fail:
	rcu_read_lock();
	list_for_each_entry_rcu(e, &phy->edges, list) {
		list_del_rcu(&e->list);
		hwsim_free_edge(e);
	}
	rcu_read_unlock();
sub_fail:
	hwsim_edge_unsubscribe_me(phy);
	return -ENOMEM;
}

static int hwsim_add_one(struct genl_info *info, struct device *dev,
			 bool init)
{
	struct ieee802154_hw *hw;
	struct hwsim_phy *phy;
	struct hwsim_pib *pib;
	int idx;
	int err;

	idx = hwsim_radio_idx++;

	hw = ieee802154_alloc_hw(sizeof(*phy), &hwsim_ops);
	if (!hw)
		return -ENOMEM;

	phy = hw->priv;
	phy->hw = hw;

	/* 868 MHz BPSK	802.15.4-2003 */
	hw->phy->supported.channels[0] |= 1;
	/* 915 MHz BPSK	802.15.4-2003 */
	hw->phy->supported.channels[0] |= 0x7fe;
	/* 2.4 GHz O-QPSK 802.15.4-2003 */
	hw->phy->supported.channels[0] |= 0x7FFF800;
	/* 868 MHz ASK 802.15.4-2006 */
	hw->phy->supported.channels[1] |= 1;
	/* 915 MHz ASK 802.15.4-2006 */
	hw->phy->supported.channels[1] |= 0x7fe;
	/* 868 MHz O-QPSK 802.15.4-2006 */
	hw->phy->supported.channels[2] |= 1;
	/* 915 MHz O-QPSK 802.15.4-2006 */
	hw->phy->supported.channels[2] |= 0x7fe;
	/* 2.4 GHz CSS 802.15.4a-2007 */
	hw->phy->supported.channels[3] |= 0x3fff;
	/* UWB Sub-gigahertz 802.15.4a-2007 */
	hw->phy->supported.channels[4] |= 1;
	/* UWB Low band 802.15.4a-2007 */
	hw->phy->supported.channels[4] |= 0x1e;
	/* UWB High band 802.15.4a-2007 */
	hw->phy->supported.channels[4] |= 0xffe0;
	/* 750 MHz O-QPSK 802.15.4c-2009 */
	hw->phy->supported.channels[5] |= 0xf;
	/* 750 MHz MPSK 802.15.4c-2009 */
	hw->phy->supported.channels[5] |= 0xf0;
	/* 950 MHz BPSK 802.15.4d-2009 */
	hw->phy->supported.channels[6] |= 0x3ff;
	/* 950 MHz GFSK 802.15.4d-2009 */
	hw->phy->supported.channels[6] |= 0x3ffc00;

	ieee802154_random_extended_addr(&hw->phy->perm_extended_addr);

	/* hwsim phy channel 13 as default */
	hw->phy->current_channel = 13;
	pib = kzalloc(sizeof(*pib), GFP_KERNEL);
	if (!pib) {
		err = -ENOMEM;
		goto err_pib;
	}

	rcu_assign_pointer(phy->pib, pib);
	phy->idx = idx;
	INIT_LIST_HEAD(&phy->edges);

	hw->flags = IEEE802154_HW_PROMISCUOUS;
	hw->parent = dev;

	err = ieee802154_register_hw(hw);
	if (err)
		goto err_reg;

	mutex_lock(&hwsim_phys_lock);
	if (init) {
		err = hwsim_subscribe_all_others(phy);
		if (err < 0) {
			mutex_unlock(&hwsim_phys_lock);
			goto err_reg;
		}
	}
	list_add_tail(&phy->list, &hwsim_phys);
	mutex_unlock(&hwsim_phys_lock);

	hwsim_mcast_new_radio(info, phy);

	return idx;

err_reg:
	kfree(pib);
err_pib:
	ieee802154_free_hw(phy->hw);
	return err;
}

static void hwsim_del(struct hwsim_phy *phy)
{
	struct hwsim_pib *pib;

	hwsim_edge_unsubscribe_me(phy);

	list_del(&phy->list);

	rcu_read_lock();
	pib = rcu_dereference(phy->pib);
	rcu_read_unlock();

	kfree_rcu(pib, rcu);

	ieee802154_unregister_hw(phy->hw);
	ieee802154_free_hw(phy->hw);
}

static int hwsim_probe(struct platform_device *pdev)
{
	struct hwsim_phy *phy, *tmp;
	int err, i;

	for (i = 0; i < 2; i++) {
		err = hwsim_add_one(NULL, &pdev->dev, true);
		if (err < 0)
			goto err_slave;
	}

	dev_info(&pdev->dev, "Added 2 mac802154 hwsim hardware radios\n");
	return 0;

err_slave:
	mutex_lock(&hwsim_phys_lock);
	list_for_each_entry_safe(phy, tmp, &hwsim_phys, list)
		hwsim_del(phy);
	mutex_unlock(&hwsim_phys_lock);
	return err;
}

static int hwsim_remove(struct platform_device *pdev)
{
	struct hwsim_phy *phy, *tmp;

	mutex_lock(&hwsim_phys_lock);
	list_for_each_entry_safe(phy, tmp, &hwsim_phys, list)
		hwsim_del(phy);
	mutex_unlock(&hwsim_phys_lock);

	return 0;
}

static struct platform_driver mac802154hwsim_driver = {
	.probe = hwsim_probe,
	.remove = hwsim_remove,
	.driver = {
			.name = "mac802154_hwsim",
	},
};

static __init int hwsim_init_module(void)
{
	int rc;

	rc = genl_register_family(&hwsim_genl_family);
	if (rc)
		return rc;

	mac802154hwsim_dev = platform_device_register_simple("mac802154_hwsim",
							     -1, NULL, 0);
	if (IS_ERR(mac802154hwsim_dev)) {
		rc = PTR_ERR(mac802154hwsim_dev);
		goto platform_dev;
	}

	rc = platform_driver_register(&mac802154hwsim_driver);
	if (rc < 0)
		goto platform_drv;

	return 0;

platform_drv:
	genl_unregister_family(&hwsim_genl_family);
platform_dev:
	platform_device_unregister(mac802154hwsim_dev);
	return rc;
}

static __exit void hwsim_remove_module(void)
{
	genl_unregister_family(&hwsim_genl_family);
	platform_driver_unregister(&mac802154hwsim_driver);
	platform_device_unregister(mac802154hwsim_dev);
}

module_init(hwsim_init_module);
module_exit(hwsim_remove_module);
