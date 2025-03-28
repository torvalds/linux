// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 AIROHA Inc
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/rhashtable.h>
#include <net/ipv6.h>
#include <net/pkt_cls.h>

#include "airoha_npu.h"
#include "airoha_regs.h"
#include "airoha_eth.h"

static DEFINE_MUTEX(flow_offload_mutex);
static DEFINE_SPINLOCK(ppe_lock);

static const struct rhashtable_params airoha_flow_table_params = {
	.head_offset = offsetof(struct airoha_flow_table_entry, node),
	.key_offset = offsetof(struct airoha_flow_table_entry, cookie),
	.key_len = sizeof(unsigned long),
	.automatic_shrinking = true,
};

static bool airoha_ppe2_is_enabled(struct airoha_eth *eth)
{
	return airoha_fe_rr(eth, REG_PPE_GLO_CFG(1)) & PPE_GLO_CFG_EN_MASK;
}

static u32 airoha_ppe_get_timestamp(struct airoha_ppe *ppe)
{
	u16 timestamp = airoha_fe_rr(ppe->eth, REG_FE_FOE_TS);

	return FIELD_GET(AIROHA_FOE_IB1_BIND_TIMESTAMP, timestamp);
}

static void airoha_ppe_hw_init(struct airoha_ppe *ppe)
{
	u32 sram_tb_size, sram_num_entries, dram_num_entries;
	struct airoha_eth *eth = ppe->eth;
	int i;

	sram_tb_size = PPE_SRAM_NUM_ENTRIES * sizeof(struct airoha_foe_entry);
	dram_num_entries = PPE_RAM_NUM_ENTRIES_SHIFT(PPE_DRAM_NUM_ENTRIES);

	for (i = 0; i < PPE_NUM; i++) {
		int p;

		airoha_fe_wr(eth, REG_PPE_TB_BASE(i),
			     ppe->foe_dma + sram_tb_size);

		airoha_fe_rmw(eth, REG_PPE_BND_AGE0(i),
			      PPE_BIND_AGE0_DELTA_NON_L4 |
			      PPE_BIND_AGE0_DELTA_UDP,
			      FIELD_PREP(PPE_BIND_AGE0_DELTA_NON_L4, 1) |
			      FIELD_PREP(PPE_BIND_AGE0_DELTA_UDP, 12));
		airoha_fe_rmw(eth, REG_PPE_BND_AGE1(i),
			      PPE_BIND_AGE1_DELTA_TCP_FIN |
			      PPE_BIND_AGE1_DELTA_TCP,
			      FIELD_PREP(PPE_BIND_AGE1_DELTA_TCP_FIN, 1) |
			      FIELD_PREP(PPE_BIND_AGE1_DELTA_TCP, 7));

		airoha_fe_rmw(eth, REG_PPE_TB_HASH_CFG(i),
			      PPE_SRAM_TABLE_EN_MASK |
			      PPE_SRAM_HASH1_EN_MASK |
			      PPE_DRAM_TABLE_EN_MASK |
			      PPE_SRAM_HASH0_MODE_MASK |
			      PPE_SRAM_HASH1_MODE_MASK |
			      PPE_DRAM_HASH0_MODE_MASK |
			      PPE_DRAM_HASH1_MODE_MASK,
			      FIELD_PREP(PPE_SRAM_TABLE_EN_MASK, 1) |
			      FIELD_PREP(PPE_SRAM_HASH1_EN_MASK, 1) |
			      FIELD_PREP(PPE_SRAM_HASH1_MODE_MASK, 1) |
			      FIELD_PREP(PPE_DRAM_HASH1_MODE_MASK, 3));

		airoha_fe_rmw(eth, REG_PPE_TB_CFG(i),
			      PPE_TB_CFG_SEARCH_MISS_MASK |
			      PPE_TB_ENTRY_SIZE_MASK,
			      FIELD_PREP(PPE_TB_CFG_SEARCH_MISS_MASK, 3) |
			      FIELD_PREP(PPE_TB_ENTRY_SIZE_MASK, 0));

		airoha_fe_wr(eth, REG_PPE_HASH_SEED(i), PPE_HASH_SEED);

		for (p = 0; p < ARRAY_SIZE(eth->ports); p++)
			airoha_fe_rmw(eth, REG_PPE_MTU(i, p),
				      FP0_EGRESS_MTU_MASK |
				      FP1_EGRESS_MTU_MASK,
				      FIELD_PREP(FP0_EGRESS_MTU_MASK,
						 AIROHA_MAX_MTU) |
				      FIELD_PREP(FP1_EGRESS_MTU_MASK,
						 AIROHA_MAX_MTU));
	}

	if (airoha_ppe2_is_enabled(eth)) {
		sram_num_entries =
			PPE_RAM_NUM_ENTRIES_SHIFT(PPE1_SRAM_NUM_ENTRIES);
		airoha_fe_rmw(eth, REG_PPE_TB_CFG(0),
			      PPE_SRAM_TB_NUM_ENTRY_MASK |
			      PPE_DRAM_TB_NUM_ENTRY_MASK,
			      FIELD_PREP(PPE_SRAM_TB_NUM_ENTRY_MASK,
					 sram_num_entries) |
			      FIELD_PREP(PPE_DRAM_TB_NUM_ENTRY_MASK,
					 dram_num_entries));
		airoha_fe_rmw(eth, REG_PPE_TB_CFG(1),
			      PPE_SRAM_TB_NUM_ENTRY_MASK |
			      PPE_DRAM_TB_NUM_ENTRY_MASK,
			      FIELD_PREP(PPE_SRAM_TB_NUM_ENTRY_MASK,
					 sram_num_entries) |
			      FIELD_PREP(PPE_DRAM_TB_NUM_ENTRY_MASK,
					 dram_num_entries));
	} else {
		sram_num_entries =
			PPE_RAM_NUM_ENTRIES_SHIFT(PPE_SRAM_NUM_ENTRIES);
		airoha_fe_rmw(eth, REG_PPE_TB_CFG(0),
			      PPE_SRAM_TB_NUM_ENTRY_MASK |
			      PPE_DRAM_TB_NUM_ENTRY_MASK,
			      FIELD_PREP(PPE_SRAM_TB_NUM_ENTRY_MASK,
					 sram_num_entries) |
			      FIELD_PREP(PPE_DRAM_TB_NUM_ENTRY_MASK,
					 dram_num_entries));
	}
}

static void airoha_ppe_flow_mangle_eth(const struct flow_action_entry *act, void *eth)
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

static int airoha_ppe_flow_mangle_ports(const struct flow_action_entry *act,
					struct airoha_flow_data *data)
{
	u32 val = be32_to_cpu((__force __be32)act->mangle.val);

	switch (act->mangle.offset) {
	case 0:
		if ((__force __be32)act->mangle.mask == ~cpu_to_be32(0xffff))
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

static int airoha_ppe_flow_mangle_ipv4(const struct flow_action_entry *act,
				       struct airoha_flow_data *data)
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

static int airoha_get_dsa_port(struct net_device **dev)
{
#if IS_ENABLED(CONFIG_NET_DSA)
	struct dsa_port *dp = dsa_port_from_netdev(*dev);

	if (IS_ERR(dp))
		return -ENODEV;

	*dev = dsa_port_to_conduit(dp);
	return dp->index;
#else
	return -ENODEV;
#endif
}

static int airoha_ppe_foe_entry_prepare(struct airoha_foe_entry *hwe,
					struct net_device *dev, int type,
					struct airoha_flow_data *data,
					int l4proto)
{
	int dsa_port = airoha_get_dsa_port(&dev);
	struct airoha_foe_mac_info_common *l2;
	u32 qdata, ports_pad, val;

	memset(hwe, 0, sizeof(*hwe));

	val = FIELD_PREP(AIROHA_FOE_IB1_BIND_STATE, AIROHA_FOE_STATE_BIND) |
	      FIELD_PREP(AIROHA_FOE_IB1_BIND_PACKET_TYPE, type) |
	      FIELD_PREP(AIROHA_FOE_IB1_BIND_UDP, l4proto == IPPROTO_UDP) |
	      FIELD_PREP(AIROHA_FOE_IB1_BIND_VLAN_LAYER, data->vlan.num) |
	      FIELD_PREP(AIROHA_FOE_IB1_BIND_VPM, data->vlan.num) |
	      AIROHA_FOE_IB1_BIND_TTL;
	hwe->ib1 = val;

	val = FIELD_PREP(AIROHA_FOE_IB2_PORT_AG, 0x1f) |
	      AIROHA_FOE_IB2_PSE_QOS;
	if (dsa_port >= 0)
		val |= FIELD_PREP(AIROHA_FOE_IB2_NBQ, dsa_port);

	if (dev) {
		struct airoha_gdm_port *port = netdev_priv(dev);
		u8 pse_port;

		if (dsa_port >= 0)
			pse_port = port->id == 4 ? FE_PSE_PORT_GDM4 : port->id;
		else
			pse_port = 2; /* uplink relies on GDM2 loopback */
		val |= FIELD_PREP(AIROHA_FOE_IB2_PSE_PORT, pse_port);
	}

	if (is_multicast_ether_addr(data->eth.h_dest))
		val |= AIROHA_FOE_IB2_MULTICAST;

	ports_pad = 0xa5a5a500 | (l4proto & 0xff);
	if (type == PPE_PKT_TYPE_IPV4_ROUTE)
		hwe->ipv4.orig_tuple.ports = ports_pad;
	if (type == PPE_PKT_TYPE_IPV6_ROUTE_3T)
		hwe->ipv6.ports = ports_pad;

	qdata = FIELD_PREP(AIROHA_FOE_SHAPER_ID, 0x7f);
	if (type == PPE_PKT_TYPE_BRIDGE) {
		hwe->bridge.dest_mac_hi = get_unaligned_be32(data->eth.h_dest);
		hwe->bridge.dest_mac_lo =
			get_unaligned_be16(data->eth.h_dest + 4);
		hwe->bridge.src_mac_hi =
			get_unaligned_be16(data->eth.h_source);
		hwe->bridge.src_mac_lo =
			get_unaligned_be32(data->eth.h_source + 2);
		hwe->bridge.data = qdata;
		hwe->bridge.ib2 = val;
		l2 = &hwe->bridge.l2.common;
	} else if (type >= PPE_PKT_TYPE_IPV6_ROUTE_3T) {
		hwe->ipv6.data = qdata;
		hwe->ipv6.ib2 = val;
		l2 = &hwe->ipv6.l2;
	} else {
		hwe->ipv4.data = qdata;
		hwe->ipv4.ib2 = val;
		l2 = &hwe->ipv4.l2.common;
	}

	l2->dest_mac_hi = get_unaligned_be32(data->eth.h_dest);
	l2->dest_mac_lo = get_unaligned_be16(data->eth.h_dest + 4);
	if (type <= PPE_PKT_TYPE_IPV4_DSLITE) {
		l2->src_mac_hi = get_unaligned_be32(data->eth.h_source);
		hwe->ipv4.l2.src_mac_lo =
			get_unaligned_be16(data->eth.h_source + 4);
	} else {
		l2->src_mac_hi = FIELD_PREP(AIROHA_FOE_MAC_SMAC_ID, 0xf);
	}

	if (data->vlan.num) {
		l2->etype = dsa_port >= 0 ? BIT(dsa_port) : 0;
		l2->vlan1 = data->vlan.hdr[0].id;
		if (data->vlan.num == 2)
			l2->vlan2 = data->vlan.hdr[1].id;
	} else if (dsa_port >= 0) {
		l2->etype = BIT(15) | BIT(dsa_port);
	} else if (type >= PPE_PKT_TYPE_IPV6_ROUTE_3T) {
		l2->etype = ETH_P_IPV6;
	} else {
		l2->etype = ETH_P_IP;
	}

	return 0;
}

static int airoha_ppe_foe_entry_set_ipv4_tuple(struct airoha_foe_entry *hwe,
					       struct airoha_flow_data *data,
					       bool egress)
{
	int type = FIELD_GET(AIROHA_FOE_IB1_BIND_PACKET_TYPE, hwe->ib1);
	struct airoha_foe_ipv4_tuple *t;

	switch (type) {
	case PPE_PKT_TYPE_IPV4_HNAPT:
		if (egress) {
			t = &hwe->ipv4.new_tuple;
			break;
		}
		fallthrough;
	case PPE_PKT_TYPE_IPV4_DSLITE:
	case PPE_PKT_TYPE_IPV4_ROUTE:
		t = &hwe->ipv4.orig_tuple;
		break;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	t->src_ip = be32_to_cpu(data->v4.src_addr);
	t->dest_ip = be32_to_cpu(data->v4.dst_addr);

	if (type != PPE_PKT_TYPE_IPV4_ROUTE) {
		t->src_port = be16_to_cpu(data->src_port);
		t->dest_port = be16_to_cpu(data->dst_port);
	}

	return 0;
}

static int airoha_ppe_foe_entry_set_ipv6_tuple(struct airoha_foe_entry *hwe,
					       struct airoha_flow_data *data)

{
	int type = FIELD_GET(AIROHA_FOE_IB1_BIND_PACKET_TYPE, hwe->ib1);
	u32 *src, *dest;

	switch (type) {
	case PPE_PKT_TYPE_IPV6_ROUTE_5T:
	case PPE_PKT_TYPE_IPV6_6RD:
		hwe->ipv6.src_port = be16_to_cpu(data->src_port);
		hwe->ipv6.dest_port = be16_to_cpu(data->dst_port);
		fallthrough;
	case PPE_PKT_TYPE_IPV6_ROUTE_3T:
		src = hwe->ipv6.src_ip;
		dest = hwe->ipv6.dest_ip;
		break;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	ipv6_addr_be32_to_cpu(src, data->v6.src_addr.s6_addr32);
	ipv6_addr_be32_to_cpu(dest, data->v6.dst_addr.s6_addr32);

	return 0;
}

static u32 airoha_ppe_foe_get_entry_hash(struct airoha_foe_entry *hwe)
{
	int type = FIELD_GET(AIROHA_FOE_IB1_BIND_PACKET_TYPE, hwe->ib1);
	u32 hash, hv1, hv2, hv3;

	switch (type) {
	case PPE_PKT_TYPE_IPV4_ROUTE:
	case PPE_PKT_TYPE_IPV4_HNAPT:
		hv1 = hwe->ipv4.orig_tuple.ports;
		hv2 = hwe->ipv4.orig_tuple.dest_ip;
		hv3 = hwe->ipv4.orig_tuple.src_ip;
		break;
	case PPE_PKT_TYPE_IPV6_ROUTE_3T:
	case PPE_PKT_TYPE_IPV6_ROUTE_5T:
		hv1 = hwe->ipv6.src_ip[3] ^ hwe->ipv6.dest_ip[3];
		hv1 ^= hwe->ipv6.ports;

		hv2 = hwe->ipv6.src_ip[2] ^ hwe->ipv6.dest_ip[2];
		hv2 ^= hwe->ipv6.dest_ip[0];

		hv3 = hwe->ipv6.src_ip[1] ^ hwe->ipv6.dest_ip[1];
		hv3 ^= hwe->ipv6.src_ip[0];
		break;
	case PPE_PKT_TYPE_IPV4_DSLITE:
	case PPE_PKT_TYPE_IPV6_6RD:
	default:
		WARN_ON_ONCE(1);
		return PPE_HASH_MASK;
	}

	hash = (hv1 & hv2) | ((~hv1) & hv3);
	hash = (hash >> 24) | ((hash & 0xffffff) << 8);
	hash ^= hv1 ^ hv2 ^ hv3;
	hash ^= hash >> 16;
	hash &= PPE_NUM_ENTRIES - 1;

	return hash;
}

struct airoha_foe_entry *airoha_ppe_foe_get_entry(struct airoha_ppe *ppe,
						  u32 hash)
{
	if (hash < PPE_SRAM_NUM_ENTRIES) {
		u32 *hwe = ppe->foe + hash * sizeof(struct airoha_foe_entry);
		struct airoha_eth *eth = ppe->eth;
		bool ppe2;
		u32 val;
		int i;

		ppe2 = airoha_ppe2_is_enabled(ppe->eth) &&
		       hash >= PPE1_SRAM_NUM_ENTRIES;
		airoha_fe_wr(ppe->eth, REG_PPE_RAM_CTRL(ppe2),
			     FIELD_PREP(PPE_SRAM_CTRL_ENTRY_MASK, hash) |
			     PPE_SRAM_CTRL_REQ_MASK);
		if (read_poll_timeout_atomic(airoha_fe_rr, val,
					     val & PPE_SRAM_CTRL_ACK_MASK,
					     10, 100, false, eth,
					     REG_PPE_RAM_CTRL(ppe2)))
			return NULL;

		for (i = 0; i < sizeof(struct airoha_foe_entry) / 4; i++)
			hwe[i] = airoha_fe_rr(eth,
					      REG_PPE_RAM_ENTRY(ppe2, i));
	}

	return ppe->foe + hash * sizeof(struct airoha_foe_entry);
}

static bool airoha_ppe_foe_compare_entry(struct airoha_flow_table_entry *e,
					 struct airoha_foe_entry *hwe)
{
	int type = FIELD_GET(AIROHA_FOE_IB1_BIND_PACKET_TYPE, e->data.ib1);
	int len;

	if ((hwe->ib1 ^ e->data.ib1) & AIROHA_FOE_IB1_BIND_UDP)
		return false;

	if (type > PPE_PKT_TYPE_IPV4_DSLITE)
		len = offsetof(struct airoha_foe_entry, ipv6.data);
	else
		len = offsetof(struct airoha_foe_entry, ipv4.ib2);

	return !memcmp(&e->data.d, &hwe->d, len - sizeof(hwe->ib1));
}

static int airoha_ppe_foe_commit_entry(struct airoha_ppe *ppe,
				       struct airoha_foe_entry *e,
				       u32 hash)
{
	struct airoha_foe_entry *hwe = ppe->foe + hash * sizeof(*hwe);
	u32 ts = airoha_ppe_get_timestamp(ppe);
	struct airoha_eth *eth = ppe->eth;

	memcpy(&hwe->d, &e->d, sizeof(*hwe) - sizeof(hwe->ib1));
	wmb();

	e->ib1 &= ~AIROHA_FOE_IB1_BIND_TIMESTAMP;
	e->ib1 |= FIELD_PREP(AIROHA_FOE_IB1_BIND_TIMESTAMP, ts);
	hwe->ib1 = e->ib1;

	if (hash < PPE_SRAM_NUM_ENTRIES) {
		dma_addr_t addr = ppe->foe_dma + hash * sizeof(*hwe);
		bool ppe2 = airoha_ppe2_is_enabled(eth) &&
			    hash >= PPE1_SRAM_NUM_ENTRIES;
		struct airoha_npu *npu;
		int err = -ENODEV;

		rcu_read_lock();
		npu = rcu_dereference(eth->npu);
		if (npu)
			err = npu->ops.ppe_foe_commit_entry(npu, addr,
							    sizeof(*hwe), hash,
							    ppe2);
		rcu_read_unlock();

		return err;
	}

	return 0;
}

static void airoha_ppe_foe_insert_entry(struct airoha_ppe *ppe, u32 hash)
{
	struct airoha_flow_table_entry *e;
	struct airoha_foe_entry *hwe;
	struct hlist_node *n;
	u32 index, state;

	spin_lock_bh(&ppe_lock);

	hwe = airoha_ppe_foe_get_entry(ppe, hash);
	if (!hwe)
		goto unlock;

	state = FIELD_GET(AIROHA_FOE_IB1_BIND_STATE, hwe->ib1);
	if (state == AIROHA_FOE_STATE_BIND)
		goto unlock;

	index = airoha_ppe_foe_get_entry_hash(hwe);
	hlist_for_each_entry_safe(e, n, &ppe->foe_flow[index], list) {
		if (airoha_ppe_foe_compare_entry(e, hwe)) {
			airoha_ppe_foe_commit_entry(ppe, &e->data, hash);
			e->hash = hash;
			break;
		}
	}
unlock:
	spin_unlock_bh(&ppe_lock);
}

static int airoha_ppe_foe_flow_commit_entry(struct airoha_ppe *ppe,
					    struct airoha_flow_table_entry *e)
{
	u32 hash = airoha_ppe_foe_get_entry_hash(&e->data);

	e->hash = 0xffff;

	spin_lock_bh(&ppe_lock);
	hlist_add_head(&e->list, &ppe->foe_flow[hash]);
	spin_unlock_bh(&ppe_lock);

	return 0;
}

static void airoha_ppe_foe_flow_remove_entry(struct airoha_ppe *ppe,
					     struct airoha_flow_table_entry *e)
{
	spin_lock_bh(&ppe_lock);

	hlist_del_init(&e->list);
	if (e->hash != 0xffff) {
		e->data.ib1 &= ~AIROHA_FOE_IB1_BIND_STATE;
		e->data.ib1 |= FIELD_PREP(AIROHA_FOE_IB1_BIND_STATE,
					  AIROHA_FOE_STATE_INVALID);
		airoha_ppe_foe_commit_entry(ppe, &e->data, e->hash);
		e->hash = 0xffff;
	}

	spin_unlock_bh(&ppe_lock);
}

static int airoha_ppe_flow_offload_replace(struct airoha_gdm_port *port,
					   struct flow_cls_offload *f)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct airoha_eth *eth = port->qdma->eth;
	struct airoha_flow_table_entry *e;
	struct airoha_flow_data data = {};
	struct net_device *odev = NULL;
	struct flow_action_entry *act;
	struct airoha_foe_entry hwe;
	int err, i, offload_type;
	u16 addr_type = 0;
	u8 l4proto = 0;

	if (rhashtable_lookup(&eth->flow_table, &f->cookie,
			      airoha_flow_table_params))
		return -EEXIST;

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_META))
		return -EOPNOTSUPP;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);
		addr_type = match.key->addr_type;
		if (flow_rule_has_control_flags(match.mask->flags,
						f->common.extack))
			return -EOPNOTSUPP;
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

	switch (addr_type) {
	case 0:
		offload_type = PPE_PKT_TYPE_BRIDGE;
		if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
			struct flow_match_eth_addrs match;

			flow_rule_match_eth_addrs(rule, &match);
			memcpy(data.eth.h_dest, match.key->dst, ETH_ALEN);
			memcpy(data.eth.h_source, match.key->src, ETH_ALEN);
		} else {
			return -EOPNOTSUPP;
		}
		break;
	case FLOW_DISSECTOR_KEY_IPV4_ADDRS:
		offload_type = PPE_PKT_TYPE_IPV4_HNAPT;
		break;
	case FLOW_DISSECTOR_KEY_IPV6_ADDRS:
		offload_type = PPE_PKT_TYPE_IPV6_ROUTE_5T;
		break;
	default:
		return -EOPNOTSUPP;
	}

	flow_action_for_each(i, act, &rule->action) {
		switch (act->id) {
		case FLOW_ACTION_MANGLE:
			if (offload_type == PPE_PKT_TYPE_BRIDGE)
				return -EOPNOTSUPP;

			if (act->mangle.htype == FLOW_ACT_MANGLE_HDR_TYPE_ETH)
				airoha_ppe_flow_mangle_eth(act, &data.eth);
			break;
		case FLOW_ACTION_REDIRECT:
			odev = act->dev;
			break;
		case FLOW_ACTION_CSUM:
			break;
		case FLOW_ACTION_VLAN_PUSH:
			if (data.vlan.num == 2 ||
			    act->vlan.proto != htons(ETH_P_8021Q))
				return -EOPNOTSUPP;

			data.vlan.hdr[data.vlan.num].id = act->vlan.vid;
			data.vlan.hdr[data.vlan.num].proto = act->vlan.proto;
			data.vlan.num++;
			break;
		case FLOW_ACTION_VLAN_POP:
			break;
		case FLOW_ACTION_PPPOE_PUSH:
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	if (!is_valid_ether_addr(data.eth.h_source) ||
	    !is_valid_ether_addr(data.eth.h_dest))
		return -EINVAL;

	err = airoha_ppe_foe_entry_prepare(&hwe, odev, offload_type,
					   &data, l4proto);
	if (err)
		return err;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports ports;

		if (offload_type == PPE_PKT_TYPE_BRIDGE)
			return -EOPNOTSUPP;

		flow_rule_match_ports(rule, &ports);
		data.src_port = ports.key->src;
		data.dst_port = ports.key->dst;
	} else if (offload_type != PPE_PKT_TYPE_BRIDGE) {
		return -EOPNOTSUPP;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_match_ipv4_addrs addrs;

		flow_rule_match_ipv4_addrs(rule, &addrs);
		data.v4.src_addr = addrs.key->src;
		data.v4.dst_addr = addrs.key->dst;
		airoha_ppe_foe_entry_set_ipv4_tuple(&hwe, &data, false);
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_match_ipv6_addrs addrs;

		flow_rule_match_ipv6_addrs(rule, &addrs);

		data.v6.src_addr = addrs.key->src;
		data.v6.dst_addr = addrs.key->dst;
		airoha_ppe_foe_entry_set_ipv6_tuple(&hwe, &data);
	}

	flow_action_for_each(i, act, &rule->action) {
		if (act->id != FLOW_ACTION_MANGLE)
			continue;

		if (offload_type == PPE_PKT_TYPE_BRIDGE)
			return -EOPNOTSUPP;

		switch (act->mangle.htype) {
		case FLOW_ACT_MANGLE_HDR_TYPE_TCP:
		case FLOW_ACT_MANGLE_HDR_TYPE_UDP:
			err = airoha_ppe_flow_mangle_ports(act, &data);
			break;
		case FLOW_ACT_MANGLE_HDR_TYPE_IP4:
			err = airoha_ppe_flow_mangle_ipv4(act, &data);
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
		err = airoha_ppe_foe_entry_set_ipv4_tuple(&hwe, &data, true);
		if (err)
			return err;
	}

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	e->cookie = f->cookie;
	memcpy(&e->data, &hwe, sizeof(e->data));

	err = airoha_ppe_foe_flow_commit_entry(eth->ppe, e);
	if (err)
		goto free_entry;

	err = rhashtable_insert_fast(&eth->flow_table, &e->node,
				     airoha_flow_table_params);
	if (err < 0)
		goto remove_foe_entry;

	return 0;

remove_foe_entry:
	airoha_ppe_foe_flow_remove_entry(eth->ppe, e);
free_entry:
	kfree(e);

	return err;
}

static int airoha_ppe_flow_offload_destroy(struct airoha_gdm_port *port,
					   struct flow_cls_offload *f)
{
	struct airoha_eth *eth = port->qdma->eth;
	struct airoha_flow_table_entry *e;

	e = rhashtable_lookup(&eth->flow_table, &f->cookie,
			      airoha_flow_table_params);
	if (!e)
		return -ENOENT;

	airoha_ppe_foe_flow_remove_entry(eth->ppe, e);
	rhashtable_remove_fast(&eth->flow_table, &e->node,
			       airoha_flow_table_params);
	kfree(e);

	return 0;
}

static int airoha_ppe_flow_offload_cmd(struct airoha_gdm_port *port,
				       struct flow_cls_offload *f)
{
	switch (f->command) {
	case FLOW_CLS_REPLACE:
		return airoha_ppe_flow_offload_replace(port, f);
	case FLOW_CLS_DESTROY:
		return airoha_ppe_flow_offload_destroy(port, f);
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int airoha_ppe_flush_sram_entries(struct airoha_ppe *ppe,
					 struct airoha_npu *npu)
{
	int i, sram_num_entries = PPE_SRAM_NUM_ENTRIES;
	struct airoha_foe_entry *hwe = ppe->foe;

	if (airoha_ppe2_is_enabled(ppe->eth))
		sram_num_entries = sram_num_entries / 2;

	for (i = 0; i < sram_num_entries; i++)
		memset(&hwe[i], 0, sizeof(*hwe));

	return npu->ops.ppe_flush_sram_entries(npu, ppe->foe_dma,
					       PPE_SRAM_NUM_ENTRIES);
}

static struct airoha_npu *airoha_ppe_npu_get(struct airoha_eth *eth)
{
	struct airoha_npu *npu = airoha_npu_get(eth->dev);

	if (IS_ERR(npu)) {
		request_module("airoha-npu");
		npu = airoha_npu_get(eth->dev);
	}

	return npu;
}

static int airoha_ppe_offload_setup(struct airoha_eth *eth)
{
	struct airoha_npu *npu = airoha_ppe_npu_get(eth);
	int err;

	if (IS_ERR(npu))
		return PTR_ERR(npu);

	err = npu->ops.ppe_init(npu);
	if (err)
		goto error_npu_put;

	airoha_ppe_hw_init(eth->ppe);
	err = airoha_ppe_flush_sram_entries(eth->ppe, npu);
	if (err)
		goto error_npu_put;

	rcu_assign_pointer(eth->npu, npu);
	synchronize_rcu();

	return 0;

error_npu_put:
	airoha_npu_put(npu);

	return err;
}

int airoha_ppe_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
				 void *cb_priv)
{
	struct flow_cls_offload *cls = type_data;
	struct net_device *dev = cb_priv;
	struct airoha_gdm_port *port = netdev_priv(dev);
	struct airoha_eth *eth = port->qdma->eth;
	int err = 0;

	if (!tc_can_offload(dev) || type != TC_SETUP_CLSFLOWER)
		return -EOPNOTSUPP;

	mutex_lock(&flow_offload_mutex);

	if (!eth->npu)
		err = airoha_ppe_offload_setup(eth);
	if (!err)
		err = airoha_ppe_flow_offload_cmd(port, cls);

	mutex_unlock(&flow_offload_mutex);

	return err;
}

void airoha_ppe_check_skb(struct airoha_ppe *ppe, u16 hash)
{
	u16 now, diff;

	if (hash > PPE_HASH_MASK)
		return;

	now = (u16)jiffies;
	diff = now - ppe->foe_check_time[hash];
	if (diff < HZ / 10)
		return;

	ppe->foe_check_time[hash] = now;
	airoha_ppe_foe_insert_entry(ppe, hash);
}

int airoha_ppe_init(struct airoha_eth *eth)
{
	struct airoha_ppe *ppe;
	int foe_size, err;

	ppe = devm_kzalloc(eth->dev, sizeof(*ppe), GFP_KERNEL);
	if (!ppe)
		return -ENOMEM;

	foe_size = PPE_NUM_ENTRIES * sizeof(struct airoha_foe_entry);
	ppe->foe = dmam_alloc_coherent(eth->dev, foe_size, &ppe->foe_dma,
				       GFP_KERNEL);
	if (!ppe->foe)
		return -ENOMEM;

	ppe->eth = eth;
	eth->ppe = ppe;

	ppe->foe_flow = devm_kzalloc(eth->dev,
				     PPE_NUM_ENTRIES * sizeof(*ppe->foe_flow),
				     GFP_KERNEL);
	if (!ppe->foe_flow)
		return -ENOMEM;

	err = rhashtable_init(&eth->flow_table, &airoha_flow_table_params);
	if (err)
		return err;

	err = airoha_ppe_debugfs_init(ppe);
	if (err)
		rhashtable_destroy(&eth->flow_table);

	return err;
}

void airoha_ppe_deinit(struct airoha_eth *eth)
{
	struct airoha_npu *npu;

	rcu_read_lock();
	npu = rcu_dereference(eth->npu);
	if (npu) {
		npu->ops.ppe_deinit(npu);
		airoha_npu_put(npu);
	}
	rcu_read_unlock();

	rhashtable_destroy(&eth->flow_table);
	debugfs_remove(eth->ppe->debugfs_dir);
}
