// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Felix Fietkau <nbd@nbd.name> */

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include "mtk_eth_soc.h"

struct mtk_flow_addr_info
{
	void *src, *dest;
	u16 *src_port, *dest_port;
	bool ipv6;
};

static const char *mtk_foe_entry_state_str(int state)
{
	static const char * const state_str[] = {
		[MTK_FOE_STATE_INVALID] = "INV",
		[MTK_FOE_STATE_UNBIND] = "UNB",
		[MTK_FOE_STATE_BIND] = "BND",
		[MTK_FOE_STATE_FIN] = "FIN",
	};

	if (state >= ARRAY_SIZE(state_str) || !state_str[state])
		return "UNK";

	return state_str[state];
}

static const char *mtk_foe_pkt_type_str(int type)
{
	static const char * const type_str[] = {
		[MTK_PPE_PKT_TYPE_IPV4_HNAPT] = "IPv4 5T",
		[MTK_PPE_PKT_TYPE_IPV4_ROUTE] = "IPv4 3T",
		[MTK_PPE_PKT_TYPE_IPV4_DSLITE] = "DS-LITE",
		[MTK_PPE_PKT_TYPE_IPV6_ROUTE_3T] = "IPv6 3T",
		[MTK_PPE_PKT_TYPE_IPV6_ROUTE_5T] = "IPv6 5T",
		[MTK_PPE_PKT_TYPE_IPV6_6RD] = "6RD",
	};

	if (type >= ARRAY_SIZE(type_str) || !type_str[type])
		return "UNKNOWN";

	return type_str[type];
}

static void
mtk_print_addr(struct seq_file *m, u32 *addr, bool ipv6)
{
	u32 n_addr[4];
	int i;

	if (!ipv6) {
		seq_printf(m, "%pI4h", addr);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(n_addr); i++)
		n_addr[i] = htonl(addr[i]);
	seq_printf(m, "%pI6", n_addr);
}

static void
mtk_print_addr_info(struct seq_file *m, struct mtk_flow_addr_info *ai)
{
	mtk_print_addr(m, ai->src, ai->ipv6);
	if (ai->src_port)
		seq_printf(m, ":%d", *ai->src_port);
	seq_printf(m, "->");
	mtk_print_addr(m, ai->dest, ai->ipv6);
	if (ai->dest_port)
		seq_printf(m, ":%d", *ai->dest_port);
}

static int
mtk_ppe_debugfs_foe_show(struct seq_file *m, void *private, bool bind)
{
	struct mtk_ppe *ppe = m->private;
	int i;

	for (i = 0; i < MTK_PPE_ENTRIES; i++) {
		struct mtk_foe_entry *entry = mtk_foe_get_entry(ppe, i);
		struct mtk_foe_mac_info *l2;
		struct mtk_flow_addr_info ai = {};
		unsigned char h_source[ETH_ALEN];
		unsigned char h_dest[ETH_ALEN];
		int type, state;
		u32 ib2;


		state = FIELD_GET(MTK_FOE_IB1_STATE, entry->ib1);
		if (!state)
			continue;

		if (bind && state != MTK_FOE_STATE_BIND)
			continue;

		type = FIELD_GET(MTK_FOE_IB1_PACKET_TYPE, entry->ib1);
		seq_printf(m, "%05x %s %7s", i,
			   mtk_foe_entry_state_str(state),
			   mtk_foe_pkt_type_str(type));

		switch (type) {
		case MTK_PPE_PKT_TYPE_IPV4_HNAPT:
		case MTK_PPE_PKT_TYPE_IPV4_DSLITE:
			ai.src_port = &entry->ipv4.orig.src_port;
			ai.dest_port = &entry->ipv4.orig.dest_port;
			fallthrough;
		case MTK_PPE_PKT_TYPE_IPV4_ROUTE:
			ai.src = &entry->ipv4.orig.src_ip;
			ai.dest = &entry->ipv4.orig.dest_ip;
			break;
		case MTK_PPE_PKT_TYPE_IPV6_ROUTE_5T:
			ai.src_port = &entry->ipv6.src_port;
			ai.dest_port = &entry->ipv6.dest_port;
			fallthrough;
		case MTK_PPE_PKT_TYPE_IPV6_ROUTE_3T:
		case MTK_PPE_PKT_TYPE_IPV6_6RD:
			ai.src = &entry->ipv6.src_ip;
			ai.dest = &entry->ipv6.dest_ip;
			ai.ipv6 = true;
			break;
		}

		seq_printf(m, " orig=");
		mtk_print_addr_info(m, &ai);

		switch (type) {
		case MTK_PPE_PKT_TYPE_IPV4_HNAPT:
		case MTK_PPE_PKT_TYPE_IPV4_DSLITE:
			ai.src_port = &entry->ipv4.new.src_port;
			ai.dest_port = &entry->ipv4.new.dest_port;
			fallthrough;
		case MTK_PPE_PKT_TYPE_IPV4_ROUTE:
			ai.src = &entry->ipv4.new.src_ip;
			ai.dest = &entry->ipv4.new.dest_ip;
			seq_printf(m, " new=");
			mtk_print_addr_info(m, &ai);
			break;
		}

		if (type >= MTK_PPE_PKT_TYPE_IPV4_DSLITE) {
			l2 = &entry->ipv6.l2;
			ib2 = entry->ipv6.ib2;
		} else {
			l2 = &entry->ipv4.l2;
			ib2 = entry->ipv4.ib2;
		}

		*((__be32 *)h_source) = htonl(l2->src_mac_hi);
		*((__be16 *)&h_source[4]) = htons(l2->src_mac_lo);
		*((__be32 *)h_dest) = htonl(l2->dest_mac_hi);
		*((__be16 *)&h_dest[4]) = htons(l2->dest_mac_lo);

		seq_printf(m, " eth=%pM->%pM etype=%04x"
			      " vlan=%d,%d ib1=%08x ib2=%08x\n",
			   h_source, h_dest, ntohs(l2->etype),
			   l2->vlan1, l2->vlan2, entry->ib1, ib2);
	}

	return 0;
}

static int
mtk_ppe_debugfs_foe_all_show(struct seq_file *m, void *private)
{
	return mtk_ppe_debugfs_foe_show(m, private, false);
}
DEFINE_SHOW_ATTRIBUTE(mtk_ppe_debugfs_foe_all);

static int
mtk_ppe_debugfs_foe_bind_show(struct seq_file *m, void *private)
{
	return mtk_ppe_debugfs_foe_show(m, private, true);
}
DEFINE_SHOW_ATTRIBUTE(mtk_ppe_debugfs_foe_bind);

int mtk_ppe_debugfs_init(struct mtk_ppe *ppe, int index)
{
	struct dentry *root;

	snprintf(ppe->dirname, sizeof(ppe->dirname), "ppe%d", index);

	root = debugfs_create_dir(ppe->dirname, NULL);
	debugfs_create_file("entries", S_IRUGO, root, ppe, &mtk_ppe_debugfs_foe_all_fops);
	debugfs_create_file("bind", S_IRUGO, root, ppe, &mtk_ppe_debugfs_foe_bind_fops);

	return 0;
}
