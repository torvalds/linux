/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_TC_H
#define BNXT_TC_H

#ifdef CONFIG_BNXT_FLOWER_OFFLOAD

/* Structs used for storing the filter/actions of the TC cmd.
 */
struct bnxt_tc_l2_key {
	u8		dmac[ETH_ALEN];
	u8		smac[ETH_ALEN];
	__be16		inner_vlan_tpid;
	__be16		inner_vlan_tci;
	__be16		ether_type;
	u8		num_vlans;
};

struct bnxt_tc_l3_key {
	union {
		struct {
			struct in_addr daddr;
			struct in_addr saddr;
		} ipv4;
		struct {
			struct in6_addr daddr;
			struct in6_addr saddr;
		} ipv6;
	};
};

struct bnxt_tc_l4_key {
	u8  ip_proto;
	union {
		struct {
			__be16 sport;
			__be16 dport;
		} ports;
		struct {
			u8 type;
			u8 code;
		} icmp;
	};
};

struct bnxt_tc_actions {
	u32				flags;
#define BNXT_TC_ACTION_FLAG_FWD			BIT(0)
#define BNXT_TC_ACTION_FLAG_FWD_VXLAN		BIT(1)
#define BNXT_TC_ACTION_FLAG_PUSH_VLAN		BIT(3)
#define BNXT_TC_ACTION_FLAG_POP_VLAN		BIT(4)
#define BNXT_TC_ACTION_FLAG_DROP		BIT(5)

	u16				dst_fid;
	struct net_device		*dst_dev;
	__be16				push_vlan_tpid;
	__be16				push_vlan_tci;
};

struct bnxt_tc_flow_stats {
	u64		packets;
	u64		bytes;
};

struct bnxt_tc_flow {
	u32				flags;
#define BNXT_TC_FLOW_FLAGS_ETH_ADDRS		BIT(1)
#define BNXT_TC_FLOW_FLAGS_IPV4_ADDRS		BIT(2)
#define BNXT_TC_FLOW_FLAGS_IPV6_ADDRS		BIT(3)
#define BNXT_TC_FLOW_FLAGS_PORTS		BIT(4)
#define BNXT_TC_FLOW_FLAGS_ICMP			BIT(5)

	/* flow applicable to pkts ingressing on this fid */
	u16				src_fid;
	struct bnxt_tc_l2_key		l2_key;
	struct bnxt_tc_l2_key		l2_mask;
	struct bnxt_tc_l3_key		l3_key;
	struct bnxt_tc_l3_key		l3_mask;
	struct bnxt_tc_l4_key		l4_key;
	struct bnxt_tc_l4_key		l4_mask;

	struct bnxt_tc_actions		actions;

	/* updated stats accounting for hw-counter wrap-around */
	struct bnxt_tc_flow_stats	stats;
	/* previous snap-shot of stats */
	struct bnxt_tc_flow_stats	prev_stats;
	unsigned long			lastused; /* jiffies */
};

/* L2 hash table
 * This data-struct is used for L2-flow table.
 * The L2 part of a flow is stored in a hash table.
 * A flow that shares the same L2 key/mask with an
 * already existing flow must refer to it's flow handle.
 */
struct bnxt_tc_l2_node {
	/* hash key: first 16b of key */
#define BNXT_TC_L2_KEY_LEN			16
	struct bnxt_tc_l2_key	key;
	struct rhash_head	node;

	/* a linked list of flows that share the same l2 key */
	struct list_head	common_l2_flows;

	/* number of flows sharing the l2 key */
	u16			refcount;

	struct rcu_head		rcu;
};

struct bnxt_tc_flow_node {
	/* hash key: provided by TC */
	unsigned long			cookie;
	struct rhash_head		node;

	struct bnxt_tc_flow		flow;

	__le16				flow_handle;

	/* L2 node in l2 hashtable that shares flow's l2 key */
	struct bnxt_tc_l2_node		*l2_node;
	/* for the shared_flows list maintained in l2_node */
	struct list_head		l2_list_node;

	struct rcu_head			rcu;
};

int bnxt_tc_setup_flower(struct bnxt *bp, u16 src_fid,
			 struct tc_cls_flower_offload *cls_flower);
int bnxt_init_tc(struct bnxt *bp);
void bnxt_shutdown_tc(struct bnxt *bp);

#else /* CONFIG_BNXT_FLOWER_OFFLOAD */

static inline int bnxt_tc_setup_flower(struct bnxt *bp, u16 src_fid,
				       struct tc_cls_flower_offload *cls_flower)
{
	return -EOPNOTSUPP;
}

static inline int bnxt_init_tc(struct bnxt *bp)
{
	return 0;
}

static inline void bnxt_shutdown_tc(struct bnxt *bp)
{
}
#endif /* CONFIG_BNXT_FLOWER_OFFLOAD */
#endif /* BNXT_TC_H */
