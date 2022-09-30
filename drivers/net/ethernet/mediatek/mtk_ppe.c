// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Felix Fietkau <nbd@nbd.name> */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <net/dsa.h>
#include "mtk_eth_soc.h"
#include "mtk_ppe.h"
#include "mtk_ppe_regs.h"

static DEFINE_SPINLOCK(ppe_lock);

static const struct rhashtable_params mtk_flow_l2_ht_params = {
	.head_offset = offsetof(struct mtk_flow_entry, l2_node),
	.key_offset = offsetof(struct mtk_flow_entry, data.bridge),
	.key_len = offsetof(struct mtk_foe_bridge, key_end),
	.automatic_shrinking = true,
};

static void ppe_w32(struct mtk_ppe *ppe, u32 reg, u32 val)
{
	writel(val, ppe->base + reg);
}

static u32 ppe_r32(struct mtk_ppe *ppe, u32 reg)
{
	return readl(ppe->base + reg);
}

static u32 ppe_m32(struct mtk_ppe *ppe, u32 reg, u32 mask, u32 set)
{
	u32 val;

	val = ppe_r32(ppe, reg);
	val &= ~mask;
	val |= set;
	ppe_w32(ppe, reg, val);

	return val;
}

static u32 ppe_set(struct mtk_ppe *ppe, u32 reg, u32 val)
{
	return ppe_m32(ppe, reg, 0, val);
}

static u32 ppe_clear(struct mtk_ppe *ppe, u32 reg, u32 val)
{
	return ppe_m32(ppe, reg, val, 0);
}

static u32 mtk_eth_timestamp(struct mtk_eth *eth)
{
	return mtk_r32(eth, 0x0010) & MTK_FOE_IB1_BIND_TIMESTAMP;
}

static int mtk_ppe_wait_busy(struct mtk_ppe *ppe)
{
	int ret;
	u32 val;

	ret = readl_poll_timeout(ppe->base + MTK_PPE_GLO_CFG, val,
				 !(val & MTK_PPE_GLO_CFG_BUSY),
				 20, MTK_PPE_WAIT_TIMEOUT_US);

	if (ret)
		dev_err(ppe->dev, "PPE table busy");

	return ret;
}

static void mtk_ppe_cache_clear(struct mtk_ppe *ppe)
{
	ppe_set(ppe, MTK_PPE_CACHE_CTL, MTK_PPE_CACHE_CTL_CLEAR);
	ppe_clear(ppe, MTK_PPE_CACHE_CTL, MTK_PPE_CACHE_CTL_CLEAR);
}

static void mtk_ppe_cache_enable(struct mtk_ppe *ppe, bool enable)
{
	mtk_ppe_cache_clear(ppe);

	ppe_m32(ppe, MTK_PPE_CACHE_CTL, MTK_PPE_CACHE_CTL_EN,
		enable * MTK_PPE_CACHE_CTL_EN);
}

static u32 mtk_ppe_hash_entry(struct mtk_foe_entry *e)
{
	u32 hv1, hv2, hv3;
	u32 hash;

	switch (FIELD_GET(MTK_FOE_IB1_PACKET_TYPE, e->ib1)) {
		case MTK_PPE_PKT_TYPE_IPV4_ROUTE:
		case MTK_PPE_PKT_TYPE_IPV4_HNAPT:
			hv1 = e->ipv4.orig.ports;
			hv2 = e->ipv4.orig.dest_ip;
			hv3 = e->ipv4.orig.src_ip;
			break;
		case MTK_PPE_PKT_TYPE_IPV6_ROUTE_3T:
		case MTK_PPE_PKT_TYPE_IPV6_ROUTE_5T:
			hv1 = e->ipv6.src_ip[3] ^ e->ipv6.dest_ip[3];
			hv1 ^= e->ipv6.ports;

			hv2 = e->ipv6.src_ip[2] ^ e->ipv6.dest_ip[2];
			hv2 ^= e->ipv6.dest_ip[0];

			hv3 = e->ipv6.src_ip[1] ^ e->ipv6.dest_ip[1];
			hv3 ^= e->ipv6.src_ip[0];
			break;
		case MTK_PPE_PKT_TYPE_IPV4_DSLITE:
		case MTK_PPE_PKT_TYPE_IPV6_6RD:
		default:
			WARN_ON_ONCE(1);
			return MTK_PPE_HASH_MASK;
	}

	hash = (hv1 & hv2) | ((~hv1) & hv3);
	hash = (hash >> 24) | ((hash & 0xffffff) << 8);
	hash ^= hv1 ^ hv2 ^ hv3;
	hash ^= hash >> 16;
	hash <<= 1;
	hash &= MTK_PPE_ENTRIES - 1;

	return hash;
}

static inline struct mtk_foe_mac_info *
mtk_foe_entry_l2(struct mtk_foe_entry *entry)
{
	int type = FIELD_GET(MTK_FOE_IB1_PACKET_TYPE, entry->ib1);

	if (type == MTK_PPE_PKT_TYPE_BRIDGE)
		return &entry->bridge.l2;

	if (type >= MTK_PPE_PKT_TYPE_IPV4_DSLITE)
		return &entry->ipv6.l2;

	return &entry->ipv4.l2;
}

static inline u32 *
mtk_foe_entry_ib2(struct mtk_foe_entry *entry)
{
	int type = FIELD_GET(MTK_FOE_IB1_PACKET_TYPE, entry->ib1);

	if (type == MTK_PPE_PKT_TYPE_BRIDGE)
		return &entry->bridge.ib2;

	if (type >= MTK_PPE_PKT_TYPE_IPV4_DSLITE)
		return &entry->ipv6.ib2;

	return &entry->ipv4.ib2;
}

int mtk_foe_entry_prepare(struct mtk_foe_entry *entry, int type, int l4proto,
			  u8 pse_port, u8 *src_mac, u8 *dest_mac)
{
	struct mtk_foe_mac_info *l2;
	u32 ports_pad, val;

	memset(entry, 0, sizeof(*entry));

	val = FIELD_PREP(MTK_FOE_IB1_STATE, MTK_FOE_STATE_BIND) |
	      FIELD_PREP(MTK_FOE_IB1_PACKET_TYPE, type) |
	      FIELD_PREP(MTK_FOE_IB1_UDP, l4proto == IPPROTO_UDP) |
	      MTK_FOE_IB1_BIND_TTL |
	      MTK_FOE_IB1_BIND_CACHE;
	entry->ib1 = val;

	val = FIELD_PREP(MTK_FOE_IB2_PORT_MG, 0x3f) |
	      FIELD_PREP(MTK_FOE_IB2_PORT_AG, 0x1f) |
	      FIELD_PREP(MTK_FOE_IB2_DEST_PORT, pse_port);

	if (is_multicast_ether_addr(dest_mac))
		val |= MTK_FOE_IB2_MULTICAST;

	ports_pad = 0xa5a5a500 | (l4proto & 0xff);
	if (type == MTK_PPE_PKT_TYPE_IPV4_ROUTE)
		entry->ipv4.orig.ports = ports_pad;
	if (type == MTK_PPE_PKT_TYPE_IPV6_ROUTE_3T)
		entry->ipv6.ports = ports_pad;

	if (type == MTK_PPE_PKT_TYPE_BRIDGE) {
		ether_addr_copy(entry->bridge.src_mac, src_mac);
		ether_addr_copy(entry->bridge.dest_mac, dest_mac);
		entry->bridge.ib2 = val;
		l2 = &entry->bridge.l2;
	} else if (type >= MTK_PPE_PKT_TYPE_IPV4_DSLITE) {
		entry->ipv6.ib2 = val;
		l2 = &entry->ipv6.l2;
	} else {
		entry->ipv4.ib2 = val;
		l2 = &entry->ipv4.l2;
	}

	l2->dest_mac_hi = get_unaligned_be32(dest_mac);
	l2->dest_mac_lo = get_unaligned_be16(dest_mac + 4);
	l2->src_mac_hi = get_unaligned_be32(src_mac);
	l2->src_mac_lo = get_unaligned_be16(src_mac + 4);

	if (type >= MTK_PPE_PKT_TYPE_IPV6_ROUTE_3T)
		l2->etype = ETH_P_IPV6;
	else
		l2->etype = ETH_P_IP;

	return 0;
}

int mtk_foe_entry_set_pse_port(struct mtk_foe_entry *entry, u8 port)
{
	u32 *ib2 = mtk_foe_entry_ib2(entry);
	u32 val;

	val = *ib2;
	val &= ~MTK_FOE_IB2_DEST_PORT;
	val |= FIELD_PREP(MTK_FOE_IB2_DEST_PORT, port);
	*ib2 = val;

	return 0;
}

int mtk_foe_entry_set_ipv4_tuple(struct mtk_foe_entry *entry, bool egress,
				 __be32 src_addr, __be16 src_port,
				 __be32 dest_addr, __be16 dest_port)
{
	int type = FIELD_GET(MTK_FOE_IB1_PACKET_TYPE, entry->ib1);
	struct mtk_ipv4_tuple *t;

	switch (type) {
	case MTK_PPE_PKT_TYPE_IPV4_HNAPT:
		if (egress) {
			t = &entry->ipv4.new;
			break;
		}
		fallthrough;
	case MTK_PPE_PKT_TYPE_IPV4_DSLITE:
	case MTK_PPE_PKT_TYPE_IPV4_ROUTE:
		t = &entry->ipv4.orig;
		break;
	case MTK_PPE_PKT_TYPE_IPV6_6RD:
		entry->ipv6_6rd.tunnel_src_ip = be32_to_cpu(src_addr);
		entry->ipv6_6rd.tunnel_dest_ip = be32_to_cpu(dest_addr);
		return 0;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	t->src_ip = be32_to_cpu(src_addr);
	t->dest_ip = be32_to_cpu(dest_addr);

	if (type == MTK_PPE_PKT_TYPE_IPV4_ROUTE)
		return 0;

	t->src_port = be16_to_cpu(src_port);
	t->dest_port = be16_to_cpu(dest_port);

	return 0;
}

int mtk_foe_entry_set_ipv6_tuple(struct mtk_foe_entry *entry,
				 __be32 *src_addr, __be16 src_port,
				 __be32 *dest_addr, __be16 dest_port)
{
	int type = FIELD_GET(MTK_FOE_IB1_PACKET_TYPE, entry->ib1);
	u32 *src, *dest;
	int i;

	switch (type) {
	case MTK_PPE_PKT_TYPE_IPV4_DSLITE:
		src = entry->dslite.tunnel_src_ip;
		dest = entry->dslite.tunnel_dest_ip;
		break;
	case MTK_PPE_PKT_TYPE_IPV6_ROUTE_5T:
	case MTK_PPE_PKT_TYPE_IPV6_6RD:
		entry->ipv6.src_port = be16_to_cpu(src_port);
		entry->ipv6.dest_port = be16_to_cpu(dest_port);
		fallthrough;
	case MTK_PPE_PKT_TYPE_IPV6_ROUTE_3T:
		src = entry->ipv6.src_ip;
		dest = entry->ipv6.dest_ip;
		break;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	for (i = 0; i < 4; i++)
		src[i] = be32_to_cpu(src_addr[i]);
	for (i = 0; i < 4; i++)
		dest[i] = be32_to_cpu(dest_addr[i]);

	return 0;
}

int mtk_foe_entry_set_dsa(struct mtk_foe_entry *entry, int port)
{
	struct mtk_foe_mac_info *l2 = mtk_foe_entry_l2(entry);

	l2->etype = BIT(port);

	if (!(entry->ib1 & MTK_FOE_IB1_BIND_VLAN_LAYER))
		entry->ib1 |= FIELD_PREP(MTK_FOE_IB1_BIND_VLAN_LAYER, 1);
	else
		l2->etype |= BIT(8);

	entry->ib1 &= ~MTK_FOE_IB1_BIND_VLAN_TAG;

	return 0;
}

int mtk_foe_entry_set_vlan(struct mtk_foe_entry *entry, int vid)
{
	struct mtk_foe_mac_info *l2 = mtk_foe_entry_l2(entry);

	switch (FIELD_GET(MTK_FOE_IB1_BIND_VLAN_LAYER, entry->ib1)) {
	case 0:
		entry->ib1 |= MTK_FOE_IB1_BIND_VLAN_TAG |
			      FIELD_PREP(MTK_FOE_IB1_BIND_VLAN_LAYER, 1);
		l2->vlan1 = vid;
		return 0;
	case 1:
		if (!(entry->ib1 & MTK_FOE_IB1_BIND_VLAN_TAG)) {
			l2->vlan1 = vid;
			l2->etype |= BIT(8);
		} else {
			l2->vlan2 = vid;
			entry->ib1 += FIELD_PREP(MTK_FOE_IB1_BIND_VLAN_LAYER, 1);
		}
		return 0;
	default:
		return -ENOSPC;
	}
}

int mtk_foe_entry_set_pppoe(struct mtk_foe_entry *entry, int sid)
{
	struct mtk_foe_mac_info *l2 = mtk_foe_entry_l2(entry);

	if (!(entry->ib1 & MTK_FOE_IB1_BIND_VLAN_LAYER) ||
	    (entry->ib1 & MTK_FOE_IB1_BIND_VLAN_TAG))
		l2->etype = ETH_P_PPP_SES;

	entry->ib1 |= MTK_FOE_IB1_BIND_PPPOE;
	l2->pppoe_id = sid;

	return 0;
}

int mtk_foe_entry_set_wdma(struct mtk_foe_entry *entry, int wdma_idx, int txq,
			   int bss, int wcid)
{
	struct mtk_foe_mac_info *l2 = mtk_foe_entry_l2(entry);
	u32 *ib2 = mtk_foe_entry_ib2(entry);

	*ib2 &= ~MTK_FOE_IB2_PORT_MG;
	*ib2 |= MTK_FOE_IB2_WDMA_WINFO;
	if (wdma_idx)
		*ib2 |= MTK_FOE_IB2_WDMA_DEVIDX;

	l2->vlan2 = FIELD_PREP(MTK_FOE_VLAN2_WINFO_BSS, bss) |
		    FIELD_PREP(MTK_FOE_VLAN2_WINFO_WCID, wcid) |
		    FIELD_PREP(MTK_FOE_VLAN2_WINFO_RING, txq);

	return 0;
}

static inline bool mtk_foe_entry_usable(struct mtk_foe_entry *entry)
{
	return !(entry->ib1 & MTK_FOE_IB1_STATIC) &&
	       FIELD_GET(MTK_FOE_IB1_STATE, entry->ib1) != MTK_FOE_STATE_BIND;
}

static bool
mtk_flow_entry_match(struct mtk_flow_entry *entry, struct mtk_foe_entry *data)
{
	int type, len;

	if ((data->ib1 ^ entry->data.ib1) & MTK_FOE_IB1_UDP)
		return false;

	type = FIELD_GET(MTK_FOE_IB1_PACKET_TYPE, entry->data.ib1);
	if (type > MTK_PPE_PKT_TYPE_IPV4_DSLITE)
		len = offsetof(struct mtk_foe_entry, ipv6._rsv);
	else
		len = offsetof(struct mtk_foe_entry, ipv4.ib2);

	return !memcmp(&entry->data.data, &data->data, len - 4);
}

static void
__mtk_foe_entry_clear(struct mtk_ppe *ppe, struct mtk_flow_entry *entry)
{
	struct hlist_head *head;
	struct hlist_node *tmp;

	if (entry->type == MTK_FLOW_TYPE_L2) {
		rhashtable_remove_fast(&ppe->l2_flows, &entry->l2_node,
				       mtk_flow_l2_ht_params);

		head = &entry->l2_flows;
		hlist_for_each_entry_safe(entry, tmp, head, l2_data.list)
			__mtk_foe_entry_clear(ppe, entry);
		return;
	}

	hlist_del_init(&entry->list);
	if (entry->hash != 0xffff) {
		ppe->foe_table[entry->hash].ib1 &= ~MTK_FOE_IB1_STATE;
		ppe->foe_table[entry->hash].ib1 |= FIELD_PREP(MTK_FOE_IB1_STATE,
							      MTK_FOE_STATE_INVALID);
		dma_wmb();
	}
	entry->hash = 0xffff;

	if (entry->type != MTK_FLOW_TYPE_L2_SUBFLOW)
		return;

	hlist_del_init(&entry->l2_data.list);
	kfree(entry);
}

static int __mtk_foe_entry_idle_time(struct mtk_ppe *ppe, u32 ib1)
{
	u16 timestamp;
	u16 now;

	now = mtk_eth_timestamp(ppe->eth) & MTK_FOE_IB1_BIND_TIMESTAMP;
	timestamp = ib1 & MTK_FOE_IB1_BIND_TIMESTAMP;

	if (timestamp > now)
		return MTK_FOE_IB1_BIND_TIMESTAMP + 1 - timestamp + now;
	else
		return now - timestamp;
}

static void
mtk_flow_entry_update_l2(struct mtk_ppe *ppe, struct mtk_flow_entry *entry)
{
	struct mtk_flow_entry *cur;
	struct mtk_foe_entry *hwe;
	struct hlist_node *tmp;
	int idle;

	idle = __mtk_foe_entry_idle_time(ppe, entry->data.ib1);
	hlist_for_each_entry_safe(cur, tmp, &entry->l2_flows, l2_data.list) {
		int cur_idle;
		u32 ib1;

		hwe = &ppe->foe_table[cur->hash];
		ib1 = READ_ONCE(hwe->ib1);

		if (FIELD_GET(MTK_FOE_IB1_STATE, ib1) != MTK_FOE_STATE_BIND) {
			cur->hash = 0xffff;
			__mtk_foe_entry_clear(ppe, cur);
			continue;
		}

		cur_idle = __mtk_foe_entry_idle_time(ppe, ib1);
		if (cur_idle >= idle)
			continue;

		idle = cur_idle;
		entry->data.ib1 &= ~MTK_FOE_IB1_BIND_TIMESTAMP;
		entry->data.ib1 |= hwe->ib1 & MTK_FOE_IB1_BIND_TIMESTAMP;
	}
}

static void
mtk_flow_entry_update(struct mtk_ppe *ppe, struct mtk_flow_entry *entry)
{
	struct mtk_foe_entry *hwe;
	struct mtk_foe_entry foe;

	spin_lock_bh(&ppe_lock);

	if (entry->type == MTK_FLOW_TYPE_L2) {
		mtk_flow_entry_update_l2(ppe, entry);
		goto out;
	}

	if (entry->hash == 0xffff)
		goto out;

	hwe = &ppe->foe_table[entry->hash];
	memcpy(&foe, hwe, sizeof(foe));
	if (!mtk_flow_entry_match(entry, &foe)) {
		entry->hash = 0xffff;
		goto out;
	}

	entry->data.ib1 = foe.ib1;

out:
	spin_unlock_bh(&ppe_lock);
}

static void
__mtk_foe_entry_commit(struct mtk_ppe *ppe, struct mtk_foe_entry *entry,
		       u16 hash)
{
	struct mtk_foe_entry *hwe;
	u16 timestamp;

	timestamp = mtk_eth_timestamp(ppe->eth);
	timestamp &= MTK_FOE_IB1_BIND_TIMESTAMP;
	entry->ib1 &= ~MTK_FOE_IB1_BIND_TIMESTAMP;
	entry->ib1 |= FIELD_PREP(MTK_FOE_IB1_BIND_TIMESTAMP, timestamp);

	hwe = &ppe->foe_table[hash];
	memcpy(&hwe->data, &entry->data, sizeof(hwe->data));
	wmb();
	hwe->ib1 = entry->ib1;

	dma_wmb();

	mtk_ppe_cache_clear(ppe);
}

void mtk_foe_entry_clear(struct mtk_ppe *ppe, struct mtk_flow_entry *entry)
{
	spin_lock_bh(&ppe_lock);
	__mtk_foe_entry_clear(ppe, entry);
	spin_unlock_bh(&ppe_lock);
}

static int
mtk_foe_entry_commit_l2(struct mtk_ppe *ppe, struct mtk_flow_entry *entry)
{
	entry->type = MTK_FLOW_TYPE_L2;

	return rhashtable_insert_fast(&ppe->l2_flows, &entry->l2_node,
				      mtk_flow_l2_ht_params);
}

int mtk_foe_entry_commit(struct mtk_ppe *ppe, struct mtk_flow_entry *entry)
{
	int type = FIELD_GET(MTK_FOE_IB1_PACKET_TYPE, entry->data.ib1);
	u32 hash;

	if (type == MTK_PPE_PKT_TYPE_BRIDGE)
		return mtk_foe_entry_commit_l2(ppe, entry);

	hash = mtk_ppe_hash_entry(&entry->data);
	entry->hash = 0xffff;
	spin_lock_bh(&ppe_lock);
	hlist_add_head(&entry->list, &ppe->foe_flow[hash / 2]);
	spin_unlock_bh(&ppe_lock);

	return 0;
}

static void
mtk_foe_entry_commit_subflow(struct mtk_ppe *ppe, struct mtk_flow_entry *entry,
			     u16 hash)
{
	struct mtk_flow_entry *flow_info;
	struct mtk_foe_entry foe, *hwe;
	struct mtk_foe_mac_info *l2;
	u32 ib1_mask = MTK_FOE_IB1_PACKET_TYPE | MTK_FOE_IB1_UDP;
	int type;

	flow_info = kzalloc(offsetof(struct mtk_flow_entry, l2_data.end),
			    GFP_ATOMIC);
	if (!flow_info)
		return;

	flow_info->l2_data.base_flow = entry;
	flow_info->type = MTK_FLOW_TYPE_L2_SUBFLOW;
	flow_info->hash = hash;
	hlist_add_head(&flow_info->list, &ppe->foe_flow[hash / 2]);
	hlist_add_head(&flow_info->l2_data.list, &entry->l2_flows);

	hwe = &ppe->foe_table[hash];
	memcpy(&foe, hwe, sizeof(foe));
	foe.ib1 &= ib1_mask;
	foe.ib1 |= entry->data.ib1 & ~ib1_mask;

	l2 = mtk_foe_entry_l2(&foe);
	memcpy(l2, &entry->data.bridge.l2, sizeof(*l2));

	type = FIELD_GET(MTK_FOE_IB1_PACKET_TYPE, foe.ib1);
	if (type == MTK_PPE_PKT_TYPE_IPV4_HNAPT)
		memcpy(&foe.ipv4.new, &foe.ipv4.orig, sizeof(foe.ipv4.new));
	else if (type >= MTK_PPE_PKT_TYPE_IPV6_ROUTE_3T && l2->etype == ETH_P_IP)
		l2->etype = ETH_P_IPV6;

	*mtk_foe_entry_ib2(&foe) = entry->data.bridge.ib2;

	__mtk_foe_entry_commit(ppe, &foe, hash);
}

void __mtk_ppe_check_skb(struct mtk_ppe *ppe, struct sk_buff *skb, u16 hash)
{
	struct hlist_head *head = &ppe->foe_flow[hash / 2];
	struct mtk_foe_entry *hwe = &ppe->foe_table[hash];
	struct mtk_flow_entry *entry;
	struct mtk_foe_bridge key = {};
	struct hlist_node *n;
	struct ethhdr *eh;
	bool found = false;
	u8 *tag;

	spin_lock_bh(&ppe_lock);

	if (FIELD_GET(MTK_FOE_IB1_STATE, hwe->ib1) == MTK_FOE_STATE_BIND)
		goto out;

	hlist_for_each_entry_safe(entry, n, head, list) {
		if (entry->type == MTK_FLOW_TYPE_L2_SUBFLOW) {
			if (unlikely(FIELD_GET(MTK_FOE_IB1_STATE, hwe->ib1) ==
				     MTK_FOE_STATE_BIND))
				continue;

			entry->hash = 0xffff;
			__mtk_foe_entry_clear(ppe, entry);
			continue;
		}

		if (found || !mtk_flow_entry_match(entry, hwe)) {
			if (entry->hash != 0xffff)
				entry->hash = 0xffff;
			continue;
		}

		entry->hash = hash;
		__mtk_foe_entry_commit(ppe, &entry->data, hash);
		found = true;
	}

	if (found)
		goto out;

	eh = eth_hdr(skb);
	ether_addr_copy(key.dest_mac, eh->h_dest);
	ether_addr_copy(key.src_mac, eh->h_source);
	tag = skb->data - 2;
	key.vlan = 0;
	switch (skb->protocol) {
#if IS_ENABLED(CONFIG_NET_DSA)
	case htons(ETH_P_XDSA):
		if (!netdev_uses_dsa(skb->dev) ||
		    skb->dev->dsa_ptr->tag_ops->proto != DSA_TAG_PROTO_MTK)
			goto out;

		tag += 4;
		if (get_unaligned_be16(tag) != ETH_P_8021Q)
			break;

		fallthrough;
#endif
	case htons(ETH_P_8021Q):
		key.vlan = get_unaligned_be16(tag + 2) & VLAN_VID_MASK;
		break;
	default:
		break;
	}

	entry = rhashtable_lookup_fast(&ppe->l2_flows, &key, mtk_flow_l2_ht_params);
	if (!entry)
		goto out;

	mtk_foe_entry_commit_subflow(ppe, entry, hash);

out:
	spin_unlock_bh(&ppe_lock);
}

int mtk_foe_entry_idle_time(struct mtk_ppe *ppe, struct mtk_flow_entry *entry)
{
	mtk_flow_entry_update(ppe, entry);

	return __mtk_foe_entry_idle_time(ppe, entry->data.ib1);
}

struct mtk_ppe *mtk_ppe_init(struct mtk_eth *eth, void __iomem *base,
		 int version)
{
	struct device *dev = eth->dev;
	struct mtk_foe_entry *foe;
	struct mtk_ppe *ppe;

	ppe = devm_kzalloc(dev, sizeof(*ppe), GFP_KERNEL);
	if (!ppe)
		return NULL;

	rhashtable_init(&ppe->l2_flows, &mtk_flow_l2_ht_params);

	/* need to allocate a separate device, since it PPE DMA access is
	 * not coherent.
	 */
	ppe->base = base;
	ppe->eth = eth;
	ppe->dev = dev;
	ppe->version = version;

	foe = dmam_alloc_coherent(ppe->dev, MTK_PPE_ENTRIES * sizeof(*foe),
				  &ppe->foe_phys, GFP_KERNEL);
	if (!foe)
		return NULL;

	ppe->foe_table = foe;

	mtk_ppe_debugfs_init(ppe);

	return ppe;
}

static void mtk_ppe_init_foe_table(struct mtk_ppe *ppe)
{
	static const u8 skip[] = { 12, 25, 38, 51, 76, 89, 102 };
	int i, k;

	memset(ppe->foe_table, 0, MTK_PPE_ENTRIES * sizeof(*ppe->foe_table));

	if (!IS_ENABLED(CONFIG_SOC_MT7621))
		return;

	/* skip all entries that cross the 1024 byte boundary */
	for (i = 0; i < MTK_PPE_ENTRIES; i += 128)
		for (k = 0; k < ARRAY_SIZE(skip); k++)
			ppe->foe_table[i + skip[k]].ib1 |= MTK_FOE_IB1_STATIC;
}

int mtk_ppe_start(struct mtk_ppe *ppe)
{
	u32 val;

	mtk_ppe_init_foe_table(ppe);
	ppe_w32(ppe, MTK_PPE_TB_BASE, ppe->foe_phys);

	val = MTK_PPE_TB_CFG_ENTRY_80B |
	      MTK_PPE_TB_CFG_AGE_NON_L4 |
	      MTK_PPE_TB_CFG_AGE_UNBIND |
	      MTK_PPE_TB_CFG_AGE_TCP |
	      MTK_PPE_TB_CFG_AGE_UDP |
	      MTK_PPE_TB_CFG_AGE_TCP_FIN |
	      FIELD_PREP(MTK_PPE_TB_CFG_SEARCH_MISS,
			 MTK_PPE_SEARCH_MISS_ACTION_FORWARD_BUILD) |
	      FIELD_PREP(MTK_PPE_TB_CFG_KEEPALIVE,
			 MTK_PPE_KEEPALIVE_DISABLE) |
	      FIELD_PREP(MTK_PPE_TB_CFG_HASH_MODE, 1) |
	      FIELD_PREP(MTK_PPE_TB_CFG_SCAN_MODE,
			 MTK_PPE_SCAN_MODE_KEEPALIVE_AGE) |
	      FIELD_PREP(MTK_PPE_TB_CFG_ENTRY_NUM,
			 MTK_PPE_ENTRIES_SHIFT);
	ppe_w32(ppe, MTK_PPE_TB_CFG, val);

	ppe_w32(ppe, MTK_PPE_IP_PROTO_CHK,
		MTK_PPE_IP_PROTO_CHK_IPV4 | MTK_PPE_IP_PROTO_CHK_IPV6);

	mtk_ppe_cache_enable(ppe, true);

	val = MTK_PPE_FLOW_CFG_IP4_TCP_FRAG |
	      MTK_PPE_FLOW_CFG_IP4_UDP_FRAG |
	      MTK_PPE_FLOW_CFG_IP6_3T_ROUTE |
	      MTK_PPE_FLOW_CFG_IP6_5T_ROUTE |
	      MTK_PPE_FLOW_CFG_IP6_6RD |
	      MTK_PPE_FLOW_CFG_IP4_NAT |
	      MTK_PPE_FLOW_CFG_IP4_NAPT |
	      MTK_PPE_FLOW_CFG_IP4_DSLITE |
	      MTK_PPE_FLOW_CFG_IP4_NAT_FRAG;
	ppe_w32(ppe, MTK_PPE_FLOW_CFG, val);

	val = FIELD_PREP(MTK_PPE_UNBIND_AGE_MIN_PACKETS, 1000) |
	      FIELD_PREP(MTK_PPE_UNBIND_AGE_DELTA, 3);
	ppe_w32(ppe, MTK_PPE_UNBIND_AGE, val);

	val = FIELD_PREP(MTK_PPE_BIND_AGE0_DELTA_UDP, 12) |
	      FIELD_PREP(MTK_PPE_BIND_AGE0_DELTA_NON_L4, 1);
	ppe_w32(ppe, MTK_PPE_BIND_AGE0, val);

	val = FIELD_PREP(MTK_PPE_BIND_AGE1_DELTA_TCP_FIN, 1) |
	      FIELD_PREP(MTK_PPE_BIND_AGE1_DELTA_TCP, 7);
	ppe_w32(ppe, MTK_PPE_BIND_AGE1, val);

	val = MTK_PPE_BIND_LIMIT0_QUARTER | MTK_PPE_BIND_LIMIT0_HALF;
	ppe_w32(ppe, MTK_PPE_BIND_LIMIT0, val);

	val = MTK_PPE_BIND_LIMIT1_FULL |
	      FIELD_PREP(MTK_PPE_BIND_LIMIT1_NON_L4, 1);
	ppe_w32(ppe, MTK_PPE_BIND_LIMIT1, val);

	val = FIELD_PREP(MTK_PPE_BIND_RATE_BIND, 30) |
	      FIELD_PREP(MTK_PPE_BIND_RATE_PREBIND, 1);
	ppe_w32(ppe, MTK_PPE_BIND_RATE, val);

	/* enable PPE */
	val = MTK_PPE_GLO_CFG_EN |
	      MTK_PPE_GLO_CFG_IP4_L4_CS_DROP |
	      MTK_PPE_GLO_CFG_IP4_CS_DROP |
	      MTK_PPE_GLO_CFG_FLOW_DROP_UPDATE;
	ppe_w32(ppe, MTK_PPE_GLO_CFG, val);

	ppe_w32(ppe, MTK_PPE_DEFAULT_CPU_PORT, 0);

	return 0;
}

int mtk_ppe_stop(struct mtk_ppe *ppe)
{
	u32 val;
	int i;

	for (i = 0; i < MTK_PPE_ENTRIES; i++)
		ppe->foe_table[i].ib1 = FIELD_PREP(MTK_FOE_IB1_STATE,
						   MTK_FOE_STATE_INVALID);

	mtk_ppe_cache_enable(ppe, false);

	/* disable offload engine */
	ppe_clear(ppe, MTK_PPE_GLO_CFG, MTK_PPE_GLO_CFG_EN);
	ppe_w32(ppe, MTK_PPE_FLOW_CFG, 0);

	/* disable aging */
	val = MTK_PPE_TB_CFG_AGE_NON_L4 |
	      MTK_PPE_TB_CFG_AGE_UNBIND |
	      MTK_PPE_TB_CFG_AGE_TCP |
	      MTK_PPE_TB_CFG_AGE_UDP |
	      MTK_PPE_TB_CFG_AGE_TCP_FIN;
	ppe_clear(ppe, MTK_PPE_TB_CFG, val);

	return mtk_ppe_wait_busy(ppe);
}
