/*
 * This file is part of the Chelsio T4/T5/T6 Ethernet driver for Linux.
 *
 * Copyright (c) 2017 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_vlan.h>

#include "cxgb4.h"
#include "cxgb4_tc_flower.h"

#define STATS_CHECK_PERIOD (HZ / 2)

static struct ch_tc_flower_entry *allocate_flower_entry(void)
{
	struct ch_tc_flower_entry *new = kzalloc(sizeof(*new), GFP_KERNEL);
	spin_lock_init(&new->lock);
	return new;
}

/* Must be called with either RTNL or rcu_read_lock */
static struct ch_tc_flower_entry *ch_flower_lookup(struct adapter *adap,
						   unsigned long flower_cookie)
{
	struct ch_tc_flower_entry *flower_entry;

	hash_for_each_possible_rcu(adap->flower_anymatch_tbl, flower_entry,
				   link, flower_cookie)
		if (flower_entry->tc_flower_cookie == flower_cookie)
			return flower_entry;
	return NULL;
}

static void cxgb4_process_flow_match(struct net_device *dev,
				     struct tc_cls_flower_offload *cls,
				     struct ch_filter_specification *fs)
{
	u16 addr_type = 0;

	if (dissector_uses_key(cls->dissector, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_dissector_key_control *key =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_CONTROL,
						  cls->key);

		addr_type = key->addr_type;
	}

	if (dissector_uses_key(cls->dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_dissector_key_basic *key =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  cls->key);
		struct flow_dissector_key_basic *mask =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  cls->mask);
		u16 ethtype_key = ntohs(key->n_proto);
		u16 ethtype_mask = ntohs(mask->n_proto);

		if (ethtype_key == ETH_P_ALL) {
			ethtype_key = 0;
			ethtype_mask = 0;
		}

		fs->val.ethtype = ethtype_key;
		fs->mask.ethtype = ethtype_mask;
		fs->val.proto = key->ip_proto;
		fs->mask.proto = mask->ip_proto;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_dissector_key_ipv4_addrs *key =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_IPV4_ADDRS,
						  cls->key);
		struct flow_dissector_key_ipv4_addrs *mask =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_IPV4_ADDRS,
						  cls->mask);
		fs->type = 0;
		memcpy(&fs->val.lip[0], &key->dst, sizeof(key->dst));
		memcpy(&fs->val.fip[0], &key->src, sizeof(key->src));
		memcpy(&fs->mask.lip[0], &mask->dst, sizeof(mask->dst));
		memcpy(&fs->mask.fip[0], &mask->src, sizeof(mask->src));
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_dissector_key_ipv6_addrs *key =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_IPV6_ADDRS,
						  cls->key);
		struct flow_dissector_key_ipv6_addrs *mask =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_IPV6_ADDRS,
						  cls->mask);

		fs->type = 1;
		memcpy(&fs->val.lip[0], key->dst.s6_addr, sizeof(key->dst));
		memcpy(&fs->val.fip[0], key->src.s6_addr, sizeof(key->src));
		memcpy(&fs->mask.lip[0], mask->dst.s6_addr, sizeof(mask->dst));
		memcpy(&fs->mask.fip[0], mask->src.s6_addr, sizeof(mask->src));
	}

	if (dissector_uses_key(cls->dissector, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_dissector_key_ports *key, *mask;

		key = skb_flow_dissector_target(cls->dissector,
						FLOW_DISSECTOR_KEY_PORTS,
						cls->key);
		mask = skb_flow_dissector_target(cls->dissector,
						 FLOW_DISSECTOR_KEY_PORTS,
						 cls->mask);
		fs->val.lport = cpu_to_be16(key->dst);
		fs->mask.lport = cpu_to_be16(mask->dst);
		fs->val.fport = cpu_to_be16(key->src);
		fs->mask.fport = cpu_to_be16(mask->src);
	}

	/* Match only packets coming from the ingress port where this
	 * filter will be created.
	 */
	fs->val.iport = netdev2pinfo(dev)->port_id;
	fs->mask.iport = ~0;
}

static int cxgb4_validate_flow_match(struct net_device *dev,
				     struct tc_cls_flower_offload *cls)
{
	if (cls->dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_PORTS))) {
		netdev_warn(dev, "Unsupported key used: 0x%x\n",
			    cls->dissector->used_keys);
		return -EOPNOTSUPP;
	}
	return 0;
}

static void cxgb4_process_flow_actions(struct net_device *in,
				       struct tc_cls_flower_offload *cls,
				       struct ch_filter_specification *fs)
{
	const struct tc_action *a;
	LIST_HEAD(actions);

	tcf_exts_to_list(cls->exts, &actions);
	list_for_each_entry(a, &actions, list) {
		if (is_tcf_gact_shot(a)) {
			fs->action = FILTER_DROP;
		} else if (is_tcf_mirred_egress_redirect(a)) {
			int ifindex = tcf_mirred_ifindex(a);
			struct net_device *out = __dev_get_by_index(dev_net(in),
								    ifindex);
			struct port_info *pi = netdev_priv(out);

			fs->action = FILTER_SWITCH;
			fs->eport = pi->port_id;
		} else if (is_tcf_vlan(a)) {
			u32 vlan_action = tcf_vlan_action(a);
			u8 prio = tcf_vlan_push_prio(a);
			u16 vid = tcf_vlan_push_vid(a);
			u16 vlan_tci = (prio << VLAN_PRIO_SHIFT) | vid;

			switch (vlan_action) {
			case TCA_VLAN_ACT_POP:
				fs->newvlan |= VLAN_REMOVE;
				break;
			case TCA_VLAN_ACT_PUSH:
				fs->newvlan |= VLAN_INSERT;
				fs->vlan = vlan_tci;
				break;
			case TCA_VLAN_ACT_MODIFY:
				fs->newvlan |= VLAN_REWRITE;
				fs->vlan = vlan_tci;
				break;
			default:
				break;
			}
		}
	}
}

static int cxgb4_validate_flow_actions(struct net_device *dev,
				       struct tc_cls_flower_offload *cls)
{
	const struct tc_action *a;
	LIST_HEAD(actions);

	tcf_exts_to_list(cls->exts, &actions);
	list_for_each_entry(a, &actions, list) {
		if (is_tcf_gact_shot(a)) {
			/* Do nothing */
		} else if (is_tcf_mirred_egress_redirect(a)) {
			struct adapter *adap = netdev2adap(dev);
			struct net_device *n_dev;
			unsigned int i, ifindex;
			bool found = false;

			ifindex = tcf_mirred_ifindex(a);
			for_each_port(adap, i) {
				n_dev = adap->port[i];
				if (ifindex == n_dev->ifindex) {
					found = true;
					break;
				}
			}

			/* If interface doesn't belong to our hw, then
			 * the provided output port is not valid
			 */
			if (!found) {
				netdev_err(dev, "%s: Out port invalid\n",
					   __func__);
				return -EINVAL;
			}
		} else if (is_tcf_vlan(a)) {
			u16 proto = be16_to_cpu(tcf_vlan_push_proto(a));
			u32 vlan_action = tcf_vlan_action(a);

			switch (vlan_action) {
			case TCA_VLAN_ACT_POP:
				break;
			case TCA_VLAN_ACT_PUSH:
			case TCA_VLAN_ACT_MODIFY:
				if (proto != ETH_P_8021Q) {
					netdev_err(dev, "%s: Unsupported vlan proto\n",
						   __func__);
					return -EOPNOTSUPP;
				}
				break;
			default:
				netdev_err(dev, "%s: Unsupported vlan action\n",
					   __func__);
				return -EOPNOTSUPP;
			}
		} else {
			netdev_err(dev, "%s: Unsupported action\n", __func__);
			return -EOPNOTSUPP;
		}
	}
	return 0;
}

int cxgb4_tc_flower_replace(struct net_device *dev,
			    struct tc_cls_flower_offload *cls)
{
	struct adapter *adap = netdev2adap(dev);
	struct ch_tc_flower_entry *ch_flower;
	struct ch_filter_specification *fs;
	struct filter_ctx ctx;
	int fidx;
	int ret;

	if (cxgb4_validate_flow_actions(dev, cls))
		return -EOPNOTSUPP;

	if (cxgb4_validate_flow_match(dev, cls))
		return -EOPNOTSUPP;

	ch_flower = allocate_flower_entry();
	if (!ch_flower) {
		netdev_err(dev, "%s: ch_flower alloc failed.\n", __func__);
		return -ENOMEM;
	}

	fs = &ch_flower->fs;
	fs->hitcnts = 1;
	cxgb4_process_flow_actions(dev, cls, fs);
	cxgb4_process_flow_match(dev, cls, fs);

	fidx = cxgb4_get_free_ftid(dev, fs->type ? PF_INET6 : PF_INET);
	if (fidx < 0) {
		netdev_err(dev, "%s: No fidx for offload.\n", __func__);
		ret = -ENOMEM;
		goto free_entry;
	}

	init_completion(&ctx.completion);
	ret = __cxgb4_set_filter(dev, fidx, fs, &ctx);
	if (ret) {
		netdev_err(dev, "%s: filter creation err %d\n",
			   __func__, ret);
		goto free_entry;
	}

	/* Wait for reply */
	ret = wait_for_completion_timeout(&ctx.completion, 10 * HZ);
	if (!ret) {
		ret = -ETIMEDOUT;
		goto free_entry;
	}

	ret = ctx.result;
	/* Check if hw returned error for filter creation */
	if (ret) {
		netdev_err(dev, "%s: filter creation err %d\n",
			   __func__, ret);
		goto free_entry;
	}

	INIT_HLIST_NODE(&ch_flower->link);
	ch_flower->tc_flower_cookie = cls->cookie;
	ch_flower->filter_id = ctx.tid;
	hash_add_rcu(adap->flower_anymatch_tbl, &ch_flower->link, cls->cookie);

	return ret;

free_entry:
	kfree(ch_flower);
	return ret;
}

int cxgb4_tc_flower_destroy(struct net_device *dev,
			    struct tc_cls_flower_offload *cls)
{
	struct adapter *adap = netdev2adap(dev);
	struct ch_tc_flower_entry *ch_flower;
	int ret;

	ch_flower = ch_flower_lookup(adap, cls->cookie);
	if (!ch_flower)
		return -ENOENT;

	ret = cxgb4_del_filter(dev, ch_flower->filter_id);
	if (ret)
		goto err;

	hash_del_rcu(&ch_flower->link);
	kfree_rcu(ch_flower, rcu);

err:
	return ret;
}

static void ch_flower_stats_cb(unsigned long data)
{
	struct adapter *adap = (struct adapter *)data;
	struct ch_tc_flower_entry *flower_entry;
	struct ch_tc_flower_stats *ofld_stats;
	unsigned int i;
	u64 packets;
	u64 bytes;
	int ret;

	rcu_read_lock();
	hash_for_each_rcu(adap->flower_anymatch_tbl, i, flower_entry, link) {
		ret = cxgb4_get_filter_counters(adap->port[0],
						flower_entry->filter_id,
						&packets, &bytes);
		if (!ret) {
			spin_lock(&flower_entry->lock);
			ofld_stats = &flower_entry->stats;

			if (ofld_stats->prev_packet_count != packets) {
				ofld_stats->prev_packet_count = packets;
				ofld_stats->last_used = jiffies;
			}
			spin_unlock(&flower_entry->lock);
		}
	}
	rcu_read_unlock();
	mod_timer(&adap->flower_stats_timer, jiffies + STATS_CHECK_PERIOD);
}

int cxgb4_tc_flower_stats(struct net_device *dev,
			  struct tc_cls_flower_offload *cls)
{
	struct adapter *adap = netdev2adap(dev);
	struct ch_tc_flower_stats *ofld_stats;
	struct ch_tc_flower_entry *ch_flower;
	u64 packets;
	u64 bytes;
	int ret;

	ch_flower = ch_flower_lookup(adap, cls->cookie);
	if (!ch_flower) {
		ret = -ENOENT;
		goto err;
	}

	ret = cxgb4_get_filter_counters(dev, ch_flower->filter_id,
					&packets, &bytes);
	if (ret < 0)
		goto err;

	spin_lock_bh(&ch_flower->lock);
	ofld_stats = &ch_flower->stats;
	if (ofld_stats->packet_count != packets) {
		if (ofld_stats->prev_packet_count != packets)
			ofld_stats->last_used = jiffies;
		tcf_exts_stats_update(cls->exts, bytes - ofld_stats->byte_count,
				      packets - ofld_stats->packet_count,
				      ofld_stats->last_used);

		ofld_stats->packet_count = packets;
		ofld_stats->byte_count = bytes;
		ofld_stats->prev_packet_count = packets;
	}
	spin_unlock_bh(&ch_flower->lock);
	return 0;

err:
	return ret;
}

void cxgb4_init_tc_flower(struct adapter *adap)
{
	hash_init(adap->flower_anymatch_tbl);
	setup_timer(&adap->flower_stats_timer, ch_flower_stats_cb,
		    (unsigned long)adap);
	mod_timer(&adap->flower_stats_timer, jiffies + STATS_CHECK_PERIOD);
}

void cxgb4_cleanup_tc_flower(struct adapter *adap)
{
	if (adap->flower_stats_timer.function)
		del_timer_sync(&adap->flower_stats_timer);
}
