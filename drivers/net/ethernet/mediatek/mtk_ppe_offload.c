// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2020 Felix Fietkau <nbd@nbd.name>
 */

#include <linux/if_ether.h>
#include <linux/rhashtable.h>
#include <linux/ip.h>
#include <net/flow_offload.h>
#include <net/pkt_cls.h>
#include <net/dsa.h>
#include "mtk_eth_soc.h"

struct mtk_flow_data {
	struct ethhdr eth;

	union {
		struct {
			__be32 src_addr;
			__be32 dst_addr;
		} v4;
	};

	__be16 src_port;
	__be16 dst_port;

	struct {
		u16 id;
		__be16 proto;
		u8 num;
	} vlan;
	struct {
		u16 sid;
		u8 num;
	} pppoe;
};

struct mtk_flow_entry {
	struct rhash_head node;
	unsigned long cookie;
	u16 hash;
};

static const struct rhashtable_params mtk_flow_ht_params = {
	.head_offset = offsetof(struct mtk_flow_entry, node),
	.key_offset = offsetof(struct mtk_flow_entry, cookie),
	.key_len = sizeof(unsigned long),
	.automatic_shrinking = true,
};

static u32
mtk_eth_timestamp(struct mtk_eth *eth)
{
	return mtk_r32(eth, 0x0010) & MTK_FOE_IB1_BIND_TIMESTAMP;
}

static int
mtk_flow_set_ipv4_addr(struct mtk_foe_entry *foe, struct mtk_flow_data *data,
		       bool egress)
{
	return mtk_foe_entry_set_ipv4_tuple(foe, egress,
					    data->v4.src_addr, data->src_port,
					    data->v4.dst_addr, data->dst_port);
}

static void
mtk_flow_offload_mangle_eth(const struct flow_action_entry *act, void *eth)
{
	void *dest = eth + act->mangle.offset;
	const void *src = &act->mangle.val;

	if (act->mangle.offset > 8)
		return;

	if (act->mangle.mask == 0xffff) {
		src += 2;
		dest += 2;
	}

	memcpy(dest, src, act->mangle.mask ? 2 : 4);
}


static int
mtk_flow_mangle_ports(const struct flow_action_entry *act,
		      struct mtk_flow_data *data)
{
	u32 val = ntohl(act->mangle.val);

	switch (act->mangle.offset) {
	case 0:
		if (act->mangle.mask == ~htonl(0xffff))
			data->dst_port = cpu_to_be16(val);
		else
			data->src_port = cpu_to_be16(val >> 16);
		break;
	case 2:
		data->dst_port = cpu_to_be16(val);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
mtk_flow_mangle_ipv4(const struct flow_action_entry *act,
		     struct mtk_flow_data *data)
{
	__be32 *dest;

	switch (act->mangle.offset) {
	case offsetof(struct iphdr, saddr):
		dest = &data->v4.src_addr;
		break;
	case offsetof(struct iphdr, daddr):
		dest = &data->v4.dst_addr;
		break;
	default:
		return -EINVAL;
	}

	memcpy(dest, &act->mangle.val, sizeof(u32));

	return 0;
}

static int
mtk_flow_get_dsa_port(struct net_device **dev)
{
#if IS_ENABLED(CONFIG_NET_DSA)
	struct dsa_port *dp;

	dp = dsa_port_from_netdev(*dev);
	if (IS_ERR(dp))
		return -ENODEV;

	if (dp->cpu_dp->tag_ops->proto != DSA_TAG_PROTO_MTK)
		return -ENODEV;

	*dev = dp->cpu_dp->master;

	return dp->index;
#else
	return -ENODEV;
#endif
}

static int
mtk_flow_set_output_device(struct mtk_eth *eth, struct mtk_foe_entry *foe,
			   struct net_device *dev)
{
	int pse_port, dsa_port;

	dsa_port = mtk_flow_get_dsa_port(&dev);
	if (dsa_port >= 0)
		mtk_foe_entry_set_dsa(foe, dsa_port);

	if (dev == eth->netdev[0])
		pse_port = 1;
	else if (dev == eth->netdev[1])
		pse_port = 2;
	else
		return -EOPNOTSUPP;

	mtk_foe_entry_set_pse_port(foe, pse_port);

	return 0;
}

static int
mtk_flow_offload_replace(struct mtk_eth *eth, struct flow_cls_offload *f)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct flow_action_entry *act;
	struct mtk_flow_data data = {};
	struct mtk_foe_entry foe;
	struct net_device *odev = NULL;
	struct mtk_flow_entry *entry;
	int offload_type = 0;
	u16 addr_type = 0;
	u32 timestamp;
	u8 l4proto = 0;
	int err = 0;
	int hash;
	int i;

	if (rhashtable_lookup(&eth->flow_table, &f->cookie, mtk_flow_ht_params))
		return -EEXIST;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_META)) {
		struct flow_match_meta match;

		flow_rule_match_meta(rule, &match);
	} else {
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);
		addr_type = match.key->addr_type;
	} else {
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		l4proto = match.key->ip_proto;
	} else {
		return -EOPNOTSUPP;
	}

	flow_action_for_each(i, act, &rule->action) {
		switch (act->id) {
		case FLOW_ACTION_MANGLE:
			if (act->mangle.htype == FLOW_ACT_MANGLE_HDR_TYPE_ETH)
				mtk_flow_offload_mangle_eth(act, &data.eth);
			break;
		case FLOW_ACTION_REDIRECT:
			odev = act->dev;
			break;
		case FLOW_ACTION_CSUM:
			break;
		case FLOW_ACTION_VLAN_PUSH:
			if (data.vlan.num == 1 ||
			    act->vlan.proto != htons(ETH_P_8021Q))
				return -EOPNOTSUPP;

			data.vlan.id = act->vlan.vid;
			data.vlan.proto = act->vlan.proto;
			data.vlan.num++;
			break;
		case FLOW_ACTION_VLAN_POP:
			break;
		case FLOW_ACTION_PPPOE_PUSH:
			if (data.pppoe.num == 1)
				return -EOPNOTSUPP;

			data.pppoe.sid = act->pppoe.sid;
			data.pppoe.num++;
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	switch (addr_type) {
	case FLOW_DISSECTOR_KEY_IPV4_ADDRS:
		offload_type = MTK_PPE_PKT_TYPE_IPV4_HNAPT;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (!is_valid_ether_addr(data.eth.h_source) ||
	    !is_valid_ether_addr(data.eth.h_dest))
		return -EINVAL;

	err = mtk_foe_entry_prepare(&foe, offload_type, l4proto, 0,
				    data.eth.h_source,
				    data.eth.h_dest);
	if (err)
		return err;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports ports;

		flow_rule_match_ports(rule, &ports);
		data.src_port = ports.key->src;
		data.dst_port = ports.key->dst;
	} else {
		return -EOPNOTSUPP;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_match_ipv4_addrs addrs;

		flow_rule_match_ipv4_addrs(rule, &addrs);

		data.v4.src_addr = addrs.key->src;
		data.v4.dst_addr = addrs.key->dst;

		mtk_flow_set_ipv4_addr(&foe, &data, false);
	}

	flow_action_for_each(i, act, &rule->action) {
		if (act->id != FLOW_ACTION_MANGLE)
			continue;

		switch (act->mangle.htype) {
		case FLOW_ACT_MANGLE_HDR_TYPE_TCP:
		case FLOW_ACT_MANGLE_HDR_TYPE_UDP:
			err = mtk_flow_mangle_ports(act, &data);
			break;
		case FLOW_ACT_MANGLE_HDR_TYPE_IP4:
			err = mtk_flow_mangle_ipv4(act, &data);
			break;
		case FLOW_ACT_MANGLE_HDR_TYPE_ETH:
			/* handled earlier */
			break;
		default:
			return -EOPNOTSUPP;
		}

		if (err)
			return err;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		err = mtk_flow_set_ipv4_addr(&foe, &data, true);
		if (err)
			return err;
	}

	if (data.vlan.num == 1) {
		if (data.vlan.proto != htons(ETH_P_8021Q))
			return -EOPNOTSUPP;

		mtk_foe_entry_set_vlan(&foe, data.vlan.id);
	}
	if (data.pppoe.num == 1)
		mtk_foe_entry_set_pppoe(&foe, data.pppoe.sid);

	err = mtk_flow_set_output_device(eth, &foe, odev);
	if (err)
		return err;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->cookie = f->cookie;
	timestamp = mtk_eth_timestamp(eth);
	hash = mtk_foe_entry_commit(&eth->ppe, &foe, timestamp);
	if (hash < 0) {
		err = hash;
		goto free;
	}

	entry->hash = hash;
	err = rhashtable_insert_fast(&eth->flow_table, &entry->node,
				     mtk_flow_ht_params);
	if (err < 0)
		goto clear_flow;

	return 0;
clear_flow:
	mtk_foe_entry_clear(&eth->ppe, hash);
free:
	kfree(entry);
	return err;
}

static int
mtk_flow_offload_destroy(struct mtk_eth *eth, struct flow_cls_offload *f)
{
	struct mtk_flow_entry *entry;

	entry = rhashtable_lookup(&eth->flow_table, &f->cookie,
				  mtk_flow_ht_params);
	if (!entry)
		return -ENOENT;

	mtk_foe_entry_clear(&eth->ppe, entry->hash);
	rhashtable_remove_fast(&eth->flow_table, &entry->node,
			       mtk_flow_ht_params);
	kfree(entry);

	return 0;
}

static int
mtk_flow_offload_stats(struct mtk_eth *eth, struct flow_cls_offload *f)
{
	struct mtk_flow_entry *entry;
	int timestamp;
	u32 idle;

	entry = rhashtable_lookup(&eth->flow_table, &f->cookie,
				  mtk_flow_ht_params);
	if (!entry)
		return -ENOENT;

	timestamp = mtk_foe_entry_timestamp(&eth->ppe, entry->hash);
	if (timestamp < 0)
		return -ETIMEDOUT;

	idle = mtk_eth_timestamp(eth) - timestamp;
	f->stats.lastused = jiffies - idle * HZ;

	return 0;
}

static DEFINE_MUTEX(mtk_flow_offload_mutex);

static int
mtk_eth_setup_tc_block_cb(enum tc_setup_type type, void *type_data, void *cb_priv)
{
	struct flow_cls_offload *cls = type_data;
	struct net_device *dev = cb_priv;
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	int err;

	if (!tc_can_offload(dev))
		return -EOPNOTSUPP;

	if (type != TC_SETUP_CLSFLOWER)
		return -EOPNOTSUPP;

	mutex_lock(&mtk_flow_offload_mutex);
	switch (cls->command) {
	case FLOW_CLS_REPLACE:
		err = mtk_flow_offload_replace(eth, cls);
		break;
	case FLOW_CLS_DESTROY:
		err = mtk_flow_offload_destroy(eth, cls);
		break;
	case FLOW_CLS_STATS:
		err = mtk_flow_offload_stats(eth, cls);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&mtk_flow_offload_mutex);

	return err;
}

static int
mtk_eth_setup_tc_block(struct net_device *dev, struct flow_block_offload *f)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	static LIST_HEAD(block_cb_list);
	struct flow_block_cb *block_cb;
	flow_setup_cb_t *cb;

	if (!eth->ppe.foe_table)
		return -EOPNOTSUPP;

	if (f->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	cb = mtk_eth_setup_tc_block_cb;
	f->driver_block_list = &block_cb_list;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		block_cb = flow_block_cb_lookup(f->block, cb, dev);
		if (block_cb) {
			flow_block_cb_incref(block_cb);
			return 0;
		}
		block_cb = flow_block_cb_alloc(cb, dev, dev, NULL);
		if (IS_ERR(block_cb))
			return PTR_ERR(block_cb);

		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list, &block_cb_list);
		return 0;
	case FLOW_BLOCK_UNBIND:
		block_cb = flow_block_cb_lookup(f->block, cb, dev);
		if (!block_cb)
			return -ENOENT;

		if (flow_block_cb_decref(block_cb)) {
			flow_block_cb_remove(block_cb, f);
			list_del(&block_cb->driver_list);
		}
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

int mtk_eth_setup_tc(struct net_device *dev, enum tc_setup_type type,
		     void *type_data)
{
	if (type == TC_SETUP_FT)
		return mtk_eth_setup_tc_block(dev, type_data);

	return -EOPNOTSUPP;
}

int mtk_eth_offload_init(struct mtk_eth *eth)
{
	if (!eth->ppe.foe_table)
		return 0;

	return rhashtable_init(&eth->flow_table, &mtk_flow_ht_params);
}
