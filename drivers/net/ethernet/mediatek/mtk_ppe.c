// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Felix Fietkau <nbd@nbd.name> */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <net/dst_metadata.h>
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
	return mtk_r32(eth, 0x0010) & mtk_get_ib1_ts_mask(eth);
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

static int mtk_ppe_mib_wait_busy(struct mtk_ppe *ppe)
{
	int ret;
	u32 val;

	ret = readl_poll_timeout(ppe->base + MTK_PPE_MIB_SER_CR, val,
				 !(val & MTK_PPE_MIB_SER_CR_ST),
				 20, MTK_PPE_WAIT_TIMEOUT_US);

	if (ret)
		dev_err(ppe->dev, "MIB table busy");

	return ret;
}

static int mtk_mib_entry_read(struct mtk_ppe *ppe, u16 index, u64 *bytes, u64 *packets)
{
	u32 val, cnt_r0, cnt_r1, cnt_r2;
	int ret;

	val = FIELD_PREP(MTK_PPE_MIB_SER_CR_ADDR, index) | MTK_PPE_MIB_SER_CR_ST;
	ppe_w32(ppe, MTK_PPE_MIB_SER_CR, val);

	ret = mtk_ppe_mib_wait_busy(ppe);
	if (ret)
		return ret;

	cnt_r0 = readl(ppe->base + MTK_PPE_MIB_SER_R0);
	cnt_r1 = readl(ppe->base + MTK_PPE_MIB_SER_R1);
	cnt_r2 = readl(ppe->base + MTK_PPE_MIB_SER_R2);

	if (mtk_is_netsys_v3_or_greater(ppe->eth)) {
		/* 64 bit for each counter */
		u32 cnt_r3 = readl(ppe->base + MTK_PPE_MIB_SER_R3);
		*bytes = ((u64)cnt_r1 << 32) | cnt_r0;
		*packets = ((u64)cnt_r3 << 32) | cnt_r2;
	} else {
		/* 48 bit byte counter, 40 bit packet counter */
		u32 byte_cnt_low = FIELD_GET(MTK_PPE_MIB_SER_R0_BYTE_CNT_LOW, cnt_r0);
		u32 byte_cnt_high = FIELD_GET(MTK_PPE_MIB_SER_R1_BYTE_CNT_HIGH, cnt_r1);
		u32 pkt_cnt_low = FIELD_GET(MTK_PPE_MIB_SER_R1_PKT_CNT_LOW, cnt_r1);
		u32 pkt_cnt_high = FIELD_GET(MTK_PPE_MIB_SER_R2_PKT_CNT_HIGH, cnt_r2);
		*bytes = ((u64)byte_cnt_high << 32) | byte_cnt_low;
		*packets = ((u64)pkt_cnt_high << 16) | pkt_cnt_low;
	}

	return 0;
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

static u32 mtk_ppe_hash_entry(struct mtk_eth *eth, struct mtk_foe_entry *e)
{
	u32 hv1, hv2, hv3;
	u32 hash;

	switch (mtk_get_ib1_pkt_type(eth, e->ib1)) {
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
	hash <<= (ffs(eth->soc->hash_offset) - 1);
	hash &= MTK_PPE_ENTRIES - 1;

	return hash;
}

static inline struct mtk_foe_mac_info *
mtk_foe_entry_l2(struct mtk_eth *eth, struct mtk_foe_entry *entry)
{
	int type = mtk_get_ib1_pkt_type(eth, entry->ib1);

	if (type == MTK_PPE_PKT_TYPE_BRIDGE)
		return &entry->bridge.l2;

	if (type >= MTK_PPE_PKT_TYPE_IPV4_DSLITE)
		return &entry->ipv6.l2;

	return &entry->ipv4.l2;
}

static inline u32 *
mtk_foe_entry_ib2(struct mtk_eth *eth, struct mtk_foe_entry *entry)
{
	int type = mtk_get_ib1_pkt_type(eth, entry->ib1);

	if (type == MTK_PPE_PKT_TYPE_BRIDGE)
		return &entry->bridge.ib2;

	if (type >= MTK_PPE_PKT_TYPE_IPV4_DSLITE)
		return &entry->ipv6.ib2;

	return &entry->ipv4.ib2;
}

int mtk_foe_entry_prepare(struct mtk_eth *eth, struct mtk_foe_entry *entry,
			  int type, int l4proto, u8 pse_port, u8 *src_mac,
			  u8 *dest_mac)
{
	struct mtk_foe_mac_info *l2;
	u32 ports_pad, val;

	memset(entry, 0, sizeof(*entry));

	if (mtk_is_netsys_v2_or_greater(eth)) {
		val = FIELD_PREP(MTK_FOE_IB1_STATE, MTK_FOE_STATE_BIND) |
		      FIELD_PREP(MTK_FOE_IB1_PACKET_TYPE_V2, type) |
		      FIELD_PREP(MTK_FOE_IB1_UDP, l4proto == IPPROTO_UDP) |
		      MTK_FOE_IB1_BIND_CACHE_V2 | MTK_FOE_IB1_BIND_TTL_V2;
		entry->ib1 = val;

		val = FIELD_PREP(MTK_FOE_IB2_DEST_PORT_V2, pse_port) |
		      FIELD_PREP(MTK_FOE_IB2_PORT_AG_V2, 0xf);
	} else {
		int port_mg = eth->soc->offload_version > 1 ? 0 : 0x3f;

		val = FIELD_PREP(MTK_FOE_IB1_STATE, MTK_FOE_STATE_BIND) |
		      FIELD_PREP(MTK_FOE_IB1_PACKET_TYPE, type) |
		      FIELD_PREP(MTK_FOE_IB1_UDP, l4proto == IPPROTO_UDP) |
		      MTK_FOE_IB1_BIND_CACHE | MTK_FOE_IB1_BIND_TTL;
		entry->ib1 = val;

		val = FIELD_PREP(MTK_FOE_IB2_DEST_PORT, pse_port) |
		      FIELD_PREP(MTK_FOE_IB2_PORT_MG, port_mg) |
		      FIELD_PREP(MTK_FOE_IB2_PORT_AG, 0x1f);
	}

	if (is_multicast_ether_addr(dest_mac))
		val |= mtk_get_ib2_multicast_mask(eth);

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

int mtk_foe_entry_set_pse_port(struct mtk_eth *eth,
			       struct mtk_foe_entry *entry, u8 port)
{
	u32 *ib2 = mtk_foe_entry_ib2(eth, entry);
	u32 val = *ib2;

	if (mtk_is_netsys_v2_or_greater(eth)) {
		val &= ~MTK_FOE_IB2_DEST_PORT_V2;
		val |= FIELD_PREP(MTK_FOE_IB2_DEST_PORT_V2, port);
	} else {
		val &= ~MTK_FOE_IB2_DEST_PORT;
		val |= FIELD_PREP(MTK_FOE_IB2_DEST_PORT, port);
	}
	*ib2 = val;

	return 0;
}

int mtk_foe_entry_set_ipv4_tuple(struct mtk_eth *eth,
				 struct mtk_foe_entry *entry, bool egress,
				 __be32 src_addr, __be16 src_port,
				 __be32 dest_addr, __be16 dest_port)
{
	int type = mtk_get_ib1_pkt_type(eth, entry->ib1);
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

int mtk_foe_entry_set_ipv6_tuple(struct mtk_eth *eth,
				 struct mtk_foe_entry *entry,
				 __be32 *src_addr, __be16 src_port,
				 __be32 *dest_addr, __be16 dest_port)
{
	int type = mtk_get_ib1_pkt_type(eth, entry->ib1);
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

int mtk_foe_entry_set_dsa(struct mtk_eth *eth, struct mtk_foe_entry *entry,
			  int port)
{
	struct mtk_foe_mac_info *l2 = mtk_foe_entry_l2(eth, entry);

	l2->etype = BIT(port);

	if (!(entry->ib1 & mtk_get_ib1_vlan_layer_mask(eth)))
		entry->ib1 |= mtk_prep_ib1_vlan_layer(eth, 1);
	else
		l2->etype |= BIT(8);

	entry->ib1 &= ~mtk_get_ib1_vlan_tag_mask(eth);

	return 0;
}

int mtk_foe_entry_set_vlan(struct mtk_eth *eth, struct mtk_foe_entry *entry,
			   int vid)
{
	struct mtk_foe_mac_info *l2 = mtk_foe_entry_l2(eth, entry);

	switch (mtk_get_ib1_vlan_layer(eth, entry->ib1)) {
	case 0:
		entry->ib1 |= mtk_get_ib1_vlan_tag_mask(eth) |
			      mtk_prep_ib1_vlan_layer(eth, 1);
		l2->vlan1 = vid;
		return 0;
	case 1:
		if (!(entry->ib1 & mtk_get_ib1_vlan_tag_mask(eth))) {
			l2->vlan1 = vid;
			l2->etype |= BIT(8);
		} else {
			l2->vlan2 = vid;
			entry->ib1 += mtk_prep_ib1_vlan_layer(eth, 1);
		}
		return 0;
	default:
		return -ENOSPC;
	}
}

int mtk_foe_entry_set_pppoe(struct mtk_eth *eth, struct mtk_foe_entry *entry,
			    int sid)
{
	struct mtk_foe_mac_info *l2 = mtk_foe_entry_l2(eth, entry);

	if (!(entry->ib1 & mtk_get_ib1_vlan_layer_mask(eth)) ||
	    (entry->ib1 & mtk_get_ib1_vlan_tag_mask(eth)))
		l2->etype = ETH_P_PPP_SES;

	entry->ib1 |= mtk_get_ib1_ppoe_mask(eth);
	l2->pppoe_id = sid;

	return 0;
}

int mtk_foe_entry_set_wdma(struct mtk_eth *eth, struct mtk_foe_entry *entry,
			   int wdma_idx, int txq, int bss, int wcid,
			   bool amsdu_en)
{
	struct mtk_foe_mac_info *l2 = mtk_foe_entry_l2(eth, entry);
	u32 *ib2 = mtk_foe_entry_ib2(eth, entry);

	switch (eth->soc->version) {
	case 3:
		*ib2 &= ~MTK_FOE_IB2_PORT_MG_V2;
		*ib2 |=  FIELD_PREP(MTK_FOE_IB2_RX_IDX, txq) |
			 MTK_FOE_IB2_WDMA_WINFO_V2;
		l2->w3info = FIELD_PREP(MTK_FOE_WINFO_WCID_V3, wcid) |
			     FIELD_PREP(MTK_FOE_WINFO_BSS_V3, bss);
		l2->amsdu = FIELD_PREP(MTK_FOE_WINFO_AMSDU_EN, amsdu_en);
		break;
	case 2:
		*ib2 &= ~MTK_FOE_IB2_PORT_MG_V2;
		*ib2 |=  FIELD_PREP(MTK_FOE_IB2_RX_IDX, txq) |
			 MTK_FOE_IB2_WDMA_WINFO_V2;
		l2->winfo = FIELD_PREP(MTK_FOE_WINFO_WCID, wcid) |
			    FIELD_PREP(MTK_FOE_WINFO_BSS, bss);
		break;
	default:
		*ib2 &= ~MTK_FOE_IB2_PORT_MG;
		*ib2 |= MTK_FOE_IB2_WDMA_WINFO;
		if (wdma_idx)
			*ib2 |= MTK_FOE_IB2_WDMA_DEVIDX;
		l2->vlan2 = FIELD_PREP(MTK_FOE_VLAN2_WINFO_BSS, bss) |
			    FIELD_PREP(MTK_FOE_VLAN2_WINFO_WCID, wcid) |
			    FIELD_PREP(MTK_FOE_VLAN2_WINFO_RING, txq);
		break;
	}

	return 0;
}

int mtk_foe_entry_set_queue(struct mtk_eth *eth, struct mtk_foe_entry *entry,
			    unsigned int queue)
{
	u32 *ib2 = mtk_foe_entry_ib2(eth, entry);

	if (mtk_is_netsys_v2_or_greater(eth)) {
		*ib2 &= ~MTK_FOE_IB2_QID_V2;
		*ib2 |= FIELD_PREP(MTK_FOE_IB2_QID_V2, queue);
		*ib2 |= MTK_FOE_IB2_PSE_QOS_V2;
	} else {
		*ib2 &= ~MTK_FOE_IB2_QID;
		*ib2 |= FIELD_PREP(MTK_FOE_IB2_QID, queue);
		*ib2 |= MTK_FOE_IB2_PSE_QOS;
	}

	return 0;
}

static bool
mtk_flow_entry_match(struct mtk_eth *eth, struct mtk_flow_entry *entry,
		     struct mtk_foe_entry *data)
{
	int type, len;

	if ((data->ib1 ^ entry->data.ib1) & MTK_FOE_IB1_UDP)
		return false;

	type = mtk_get_ib1_pkt_type(eth, entry->data.ib1);
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
		struct mtk_foe_entry *hwe = mtk_foe_get_entry(ppe, entry->hash);

		hwe->ib1 &= ~MTK_FOE_IB1_STATE;
		hwe->ib1 |= FIELD_PREP(MTK_FOE_IB1_STATE, MTK_FOE_STATE_INVALID);
		dma_wmb();
		mtk_ppe_cache_clear(ppe);

		if (ppe->accounting) {
			struct mtk_foe_accounting *acct;

			acct = ppe->acct_table + entry->hash * sizeof(*acct);
			acct->packets = 0;
			acct->bytes = 0;
		}
	}
	entry->hash = 0xffff;

	if (entry->type != MTK_FLOW_TYPE_L2_SUBFLOW)
		return;

	hlist_del_init(&entry->l2_data.list);
	kfree(entry);
}

static int __mtk_foe_entry_idle_time(struct mtk_ppe *ppe, u32 ib1)
{
	u32 ib1_ts_mask = mtk_get_ib1_ts_mask(ppe->eth);
	u16 now = mtk_eth_timestamp(ppe->eth);
	u16 timestamp = ib1 & ib1_ts_mask;

	if (timestamp > now)
		return ib1_ts_mask + 1 - timestamp + now;
	else
		return now - timestamp;
}

static void
mtk_flow_entry_update_l2(struct mtk_ppe *ppe, struct mtk_flow_entry *entry)
{
	u32 ib1_ts_mask = mtk_get_ib1_ts_mask(ppe->eth);
	struct mtk_flow_entry *cur;
	struct mtk_foe_entry *hwe;
	struct hlist_node *tmp;
	int idle;

	idle = __mtk_foe_entry_idle_time(ppe, entry->data.ib1);
	hlist_for_each_entry_safe(cur, tmp, &entry->l2_flows, l2_data.list) {
		int cur_idle;
		u32 ib1;

		hwe = mtk_foe_get_entry(ppe, cur->hash);
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
		entry->data.ib1 &= ~ib1_ts_mask;
		entry->data.ib1 |= ib1 & ib1_ts_mask;
	}
}

static void
mtk_flow_entry_update(struct mtk_ppe *ppe, struct mtk_flow_entry *entry)
{
	struct mtk_foe_entry foe = {};
	struct mtk_foe_entry *hwe;

	spin_lock_bh(&ppe_lock);

	if (entry->type == MTK_FLOW_TYPE_L2) {
		mtk_flow_entry_update_l2(ppe, entry);
		goto out;
	}

	if (entry->hash == 0xffff)
		goto out;

	hwe = mtk_foe_get_entry(ppe, entry->hash);
	memcpy(&foe, hwe, ppe->eth->soc->foe_entry_size);
	if (!mtk_flow_entry_match(ppe->eth, entry, &foe)) {
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
	struct mtk_eth *eth = ppe->eth;
	u16 timestamp = mtk_eth_timestamp(eth);
	struct mtk_foe_entry *hwe;
	u32 val;

	if (mtk_is_netsys_v2_or_greater(eth)) {
		entry->ib1 &= ~MTK_FOE_IB1_BIND_TIMESTAMP_V2;
		entry->ib1 |= FIELD_PREP(MTK_FOE_IB1_BIND_TIMESTAMP_V2,
					 timestamp);
	} else {
		entry->ib1 &= ~MTK_FOE_IB1_BIND_TIMESTAMP;
		entry->ib1 |= FIELD_PREP(MTK_FOE_IB1_BIND_TIMESTAMP,
					 timestamp);
	}

	hwe = mtk_foe_get_entry(ppe, hash);
	memcpy(&hwe->data, &entry->data, eth->soc->foe_entry_size - sizeof(hwe->ib1));
	wmb();
	hwe->ib1 = entry->ib1;

	if (ppe->accounting) {
		if (mtk_is_netsys_v2_or_greater(eth))
			val = MTK_FOE_IB2_MIB_CNT_V2;
		else
			val = MTK_FOE_IB2_MIB_CNT;
		*mtk_foe_entry_ib2(eth, hwe) |= val;
	}

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
	struct mtk_flow_entry *prev;

	entry->type = MTK_FLOW_TYPE_L2;

	prev = rhashtable_lookup_get_insert_fast(&ppe->l2_flows, &entry->l2_node,
						 mtk_flow_l2_ht_params);
	if (likely(!prev))
		return 0;

	if (IS_ERR(prev))
		return PTR_ERR(prev);

	return rhashtable_replace_fast(&ppe->l2_flows, &prev->l2_node,
				       &entry->l2_node, mtk_flow_l2_ht_params);
}

int mtk_foe_entry_commit(struct mtk_ppe *ppe, struct mtk_flow_entry *entry)
{
	const struct mtk_soc_data *soc = ppe->eth->soc;
	int type = mtk_get_ib1_pkt_type(ppe->eth, entry->data.ib1);
	u32 hash;

	if (type == MTK_PPE_PKT_TYPE_BRIDGE)
		return mtk_foe_entry_commit_l2(ppe, entry);

	hash = mtk_ppe_hash_entry(ppe->eth, &entry->data);
	entry->hash = 0xffff;
	spin_lock_bh(&ppe_lock);
	hlist_add_head(&entry->list, &ppe->foe_flow[hash / soc->hash_offset]);
	spin_unlock_bh(&ppe_lock);

	return 0;
}

static void
mtk_foe_entry_commit_subflow(struct mtk_ppe *ppe, struct mtk_flow_entry *entry,
			     u16 hash)
{
	const struct mtk_soc_data *soc = ppe->eth->soc;
	struct mtk_flow_entry *flow_info;
	struct mtk_foe_entry foe = {}, *hwe;
	struct mtk_foe_mac_info *l2;
	u32 ib1_mask = mtk_get_ib1_pkt_type_mask(ppe->eth) | MTK_FOE_IB1_UDP;
	int type;

	flow_info = kzalloc(sizeof(*flow_info), GFP_ATOMIC);
	if (!flow_info)
		return;

	flow_info->l2_data.base_flow = entry;
	flow_info->type = MTK_FLOW_TYPE_L2_SUBFLOW;
	flow_info->hash = hash;
	hlist_add_head(&flow_info->list,
		       &ppe->foe_flow[hash / soc->hash_offset]);
	hlist_add_head(&flow_info->l2_data.list, &entry->l2_flows);

	hwe = mtk_foe_get_entry(ppe, hash);
	memcpy(&foe, hwe, soc->foe_entry_size);
	foe.ib1 &= ib1_mask;
	foe.ib1 |= entry->data.ib1 & ~ib1_mask;

	l2 = mtk_foe_entry_l2(ppe->eth, &foe);
	memcpy(l2, &entry->data.bridge.l2, sizeof(*l2));

	type = mtk_get_ib1_pkt_type(ppe->eth, foe.ib1);
	if (type == MTK_PPE_PKT_TYPE_IPV4_HNAPT)
		memcpy(&foe.ipv4.new, &foe.ipv4.orig, sizeof(foe.ipv4.new));
	else if (type >= MTK_PPE_PKT_TYPE_IPV6_ROUTE_3T && l2->etype == ETH_P_IP)
		l2->etype = ETH_P_IPV6;

	*mtk_foe_entry_ib2(ppe->eth, &foe) = entry->data.bridge.ib2;

	__mtk_foe_entry_commit(ppe, &foe, hash);
}

void __mtk_ppe_check_skb(struct mtk_ppe *ppe, struct sk_buff *skb, u16 hash)
{
	const struct mtk_soc_data *soc = ppe->eth->soc;
	struct hlist_head *head = &ppe->foe_flow[hash / soc->hash_offset];
	struct mtk_foe_entry *hwe = mtk_foe_get_entry(ppe, hash);
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

		if (found || !mtk_flow_entry_match(ppe->eth, entry, hwe)) {
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

		if (!skb_metadata_dst(skb))
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

int mtk_ppe_prepare_reset(struct mtk_ppe *ppe)
{
	if (!ppe)
		return -EINVAL;

	/* disable KA */
	ppe_clear(ppe, MTK_PPE_TB_CFG, MTK_PPE_TB_CFG_KEEPALIVE);
	ppe_clear(ppe, MTK_PPE_BIND_LMT1, MTK_PPE_NTU_KEEPALIVE);
	ppe_w32(ppe, MTK_PPE_KEEPALIVE, 0);
	usleep_range(10000, 11000);

	/* set KA timer to maximum */
	ppe_set(ppe, MTK_PPE_BIND_LMT1, MTK_PPE_NTU_KEEPALIVE);
	ppe_w32(ppe, MTK_PPE_KEEPALIVE, 0xffffffff);

	/* set KA tick select */
	ppe_set(ppe, MTK_PPE_TB_CFG, MTK_PPE_TB_TICK_SEL);
	ppe_set(ppe, MTK_PPE_TB_CFG, MTK_PPE_TB_CFG_KEEPALIVE);
	usleep_range(10000, 11000);

	/* disable scan mode */
	ppe_clear(ppe, MTK_PPE_TB_CFG, MTK_PPE_TB_CFG_SCAN_MODE);
	usleep_range(10000, 11000);

	return mtk_ppe_wait_busy(ppe);
}

struct mtk_foe_accounting *mtk_foe_entry_get_mib(struct mtk_ppe *ppe, u32 index,
						 struct mtk_foe_accounting *diff)
{
	struct mtk_foe_accounting *acct;
	int size = sizeof(struct mtk_foe_accounting);
	u64 bytes, packets;

	if (!ppe->accounting)
		return NULL;

	if (mtk_mib_entry_read(ppe, index, &bytes, &packets))
		return NULL;

	acct = ppe->acct_table + index * size;

	acct->bytes += bytes;
	acct->packets += packets;

	if (diff) {
		diff->bytes = bytes;
		diff->packets = packets;
	}

	return acct;
}

struct mtk_ppe *mtk_ppe_init(struct mtk_eth *eth, void __iomem *base, int index)
{
	bool accounting = eth->soc->has_accounting;
	const struct mtk_soc_data *soc = eth->soc;
	struct mtk_foe_accounting *acct;
	struct device *dev = eth->dev;
	struct mtk_mib_entry *mib;
	struct mtk_ppe *ppe;
	u32 foe_flow_size;
	void *foe;

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
	ppe->version = eth->soc->offload_version;
	ppe->accounting = accounting;

	foe = dmam_alloc_coherent(ppe->dev,
				  MTK_PPE_ENTRIES * soc->foe_entry_size,
				  &ppe->foe_phys, GFP_KERNEL);
	if (!foe)
		goto err_free_l2_flows;

	ppe->foe_table = foe;

	foe_flow_size = (MTK_PPE_ENTRIES / soc->hash_offset) *
			sizeof(*ppe->foe_flow);
	ppe->foe_flow = devm_kzalloc(dev, foe_flow_size, GFP_KERNEL);
	if (!ppe->foe_flow)
		goto err_free_l2_flows;

	if (accounting) {
		mib = dmam_alloc_coherent(ppe->dev, MTK_PPE_ENTRIES * sizeof(*mib),
					  &ppe->mib_phys, GFP_KERNEL);
		if (!mib)
			return NULL;

		ppe->mib_table = mib;

		acct = devm_kzalloc(dev, MTK_PPE_ENTRIES * sizeof(*acct),
				    GFP_KERNEL);

		if (!acct)
			return NULL;

		ppe->acct_table = acct;
	}

	mtk_ppe_debugfs_init(ppe, index);

	return ppe;

err_free_l2_flows:
	rhashtable_destroy(&ppe->l2_flows);
	return NULL;
}

void mtk_ppe_deinit(struct mtk_eth *eth)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(eth->ppe); i++) {
		if (!eth->ppe[i])
			return;
		rhashtable_destroy(&eth->ppe[i]->l2_flows);
	}
}

static void mtk_ppe_init_foe_table(struct mtk_ppe *ppe)
{
	static const u8 skip[] = { 12, 25, 38, 51, 76, 89, 102 };
	int i, k;

	memset(ppe->foe_table, 0,
	       MTK_PPE_ENTRIES * ppe->eth->soc->foe_entry_size);

	if (!IS_ENABLED(CONFIG_SOC_MT7621))
		return;

	/* skip all entries that cross the 1024 byte boundary */
	for (i = 0; i < MTK_PPE_ENTRIES; i += 128) {
		for (k = 0; k < ARRAY_SIZE(skip); k++) {
			struct mtk_foe_entry *hwe;

			hwe = mtk_foe_get_entry(ppe, i + skip[k]);
			hwe->ib1 |= MTK_FOE_IB1_STATIC;
		}
	}
}

void mtk_ppe_start(struct mtk_ppe *ppe)
{
	u32 val;

	if (!ppe)
		return;

	mtk_ppe_init_foe_table(ppe);
	ppe_w32(ppe, MTK_PPE_TB_BASE, ppe->foe_phys);

	val = MTK_PPE_TB_CFG_AGE_NON_L4 |
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
			 MTK_PPE_SCAN_MODE_CHECK_AGE) |
	      FIELD_PREP(MTK_PPE_TB_CFG_ENTRY_NUM,
			 MTK_PPE_ENTRIES_SHIFT);
	if (mtk_is_netsys_v2_or_greater(ppe->eth))
		val |= MTK_PPE_TB_CFG_INFO_SEL;
	if (!mtk_is_netsys_v3_or_greater(ppe->eth))
		val |= MTK_PPE_TB_CFG_ENTRY_80B;
	ppe_w32(ppe, MTK_PPE_TB_CFG, val);

	ppe_w32(ppe, MTK_PPE_IP_PROTO_CHK,
		MTK_PPE_IP_PROTO_CHK_IPV4 | MTK_PPE_IP_PROTO_CHK_IPV6);

	mtk_ppe_cache_enable(ppe, true);

	val = MTK_PPE_FLOW_CFG_IP6_3T_ROUTE |
	      MTK_PPE_FLOW_CFG_IP6_5T_ROUTE |
	      MTK_PPE_FLOW_CFG_IP6_6RD |
	      MTK_PPE_FLOW_CFG_IP4_NAT |
	      MTK_PPE_FLOW_CFG_IP4_NAPT |
	      MTK_PPE_FLOW_CFG_IP4_DSLITE |
	      MTK_PPE_FLOW_CFG_IP4_NAT_FRAG;
	if (mtk_is_netsys_v2_or_greater(ppe->eth))
		val |= MTK_PPE_MD_TOAP_BYP_CRSN0 |
		       MTK_PPE_MD_TOAP_BYP_CRSN1 |
		       MTK_PPE_MD_TOAP_BYP_CRSN2 |
		       MTK_PPE_FLOW_CFG_IP4_HASH_GRE_KEY;
	else
		val |= MTK_PPE_FLOW_CFG_IP4_TCP_FRAG |
		       MTK_PPE_FLOW_CFG_IP4_UDP_FRAG;
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

	if (mtk_is_netsys_v2_or_greater(ppe->eth)) {
		ppe_w32(ppe, MTK_PPE_DEFAULT_CPU_PORT1, 0xcb777);
		ppe_w32(ppe, MTK_PPE_SBW_CTRL, 0x7f);
	}

	if (ppe->accounting && ppe->mib_phys) {
		ppe_w32(ppe, MTK_PPE_MIB_TB_BASE, ppe->mib_phys);
		ppe_m32(ppe, MTK_PPE_MIB_CFG, MTK_PPE_MIB_CFG_EN,
			MTK_PPE_MIB_CFG_EN);
		ppe_m32(ppe, MTK_PPE_MIB_CFG, MTK_PPE_MIB_CFG_RD_CLR,
			MTK_PPE_MIB_CFG_RD_CLR);
		ppe_m32(ppe, MTK_PPE_MIB_CACHE_CTL, MTK_PPE_MIB_CACHE_CTL_EN,
			MTK_PPE_MIB_CFG_RD_CLR);
	}
}

int mtk_ppe_stop(struct mtk_ppe *ppe)
{
	u32 val;
	int i;

	if (!ppe)
		return 0;

	for (i = 0; i < MTK_PPE_ENTRIES; i++) {
		struct mtk_foe_entry *hwe = mtk_foe_get_entry(ppe, i);

		hwe->ib1 = FIELD_PREP(MTK_FOE_IB1_STATE,
				      MTK_FOE_STATE_INVALID);
	}

	mtk_ppe_cache_enable(ppe, false);

	/* disable aging */
	val = MTK_PPE_TB_CFG_AGE_NON_L4 |
	      MTK_PPE_TB_CFG_AGE_UNBIND |
	      MTK_PPE_TB_CFG_AGE_TCP |
	      MTK_PPE_TB_CFG_AGE_UDP |
	      MTK_PPE_TB_CFG_AGE_TCP_FIN |
		  MTK_PPE_TB_CFG_SCAN_MODE;
	ppe_clear(ppe, MTK_PPE_TB_CFG, val);

	if (mtk_ppe_wait_busy(ppe))
		return -ETIMEDOUT;

	/* disable offload engine */
	ppe_clear(ppe, MTK_PPE_GLO_CFG, MTK_PPE_GLO_CFG_EN);
	ppe_w32(ppe, MTK_PPE_FLOW_CFG, 0);

	return 0;
}
