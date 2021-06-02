/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2021 Corigine, Inc. */

#ifndef __NFP_FLOWER_CONNTRACK_H__
#define __NFP_FLOWER_CONNTRACK_H__ 1

#include "main.h"

#define NFP_FL_CT_NO_TUN	0xff

extern const struct rhashtable_params nfp_zone_table_params;
extern const struct rhashtable_params nfp_ct_map_params;
extern const struct rhashtable_params nfp_tc_ct_merge_params;

/**
 * struct nfp_fl_ct_zone_entry - Zone entry containing conntrack flow information
 * @zone:	The zone number, used as lookup key in hashtable
 * @hash_node:	Used by the hashtable
 * @priv:	Pointer to nfp_flower_priv data
 * @nft:	Pointer to nf_flowtable for this zone
 *
 * @pre_ct_list:	The pre_ct_list of nfp_fl_ct_flow_entry entries
 * @pre_ct_count:	Keep count of the number of pre_ct entries
 *
 * @post_ct_list:	The post_ct_list of nfp_fl_ct_flow_entry entries
 * @post_ct_count:	Keep count of the number of post_ct entries
 *
 * @tc_merge_tb:	The table of merged tc flows
 * @tc_merge_count:	Keep count of the number of merged tc entries
 */
struct nfp_fl_ct_zone_entry {
	u16 zone;
	struct rhash_head hash_node;

	struct nfp_flower_priv *priv;
	struct nf_flowtable *nft;

	struct list_head pre_ct_list;
	unsigned int pre_ct_count;

	struct list_head post_ct_list;
	unsigned int post_ct_count;

	struct rhashtable tc_merge_tb;
	unsigned int tc_merge_count;
};

enum ct_entry_type {
	CT_TYPE_PRE_CT,
	CT_TYPE_NFT,
	CT_TYPE_POST_CT,
};

/**
 * struct nfp_fl_ct_flow_entry - Flow entry containing conntrack flow information
 * @cookie:	Flow cookie, same as original TC flow, used as key
 * @list_node:	Used by the list
 * @chain_index:	Chain index of the original flow
 * @netdev:	netdev structure.
 * @type:	Type of pre-entry from enum ct_entry_type
 * @zt:		Reference to the zone table this belongs to
 * @children:	List of tc_merge flows this flow forms part of
 * @rule:	Reference to the original TC flow rule
 * @stats:	Used to cache stats for updating
 * @tun_offset: Used to indicate tunnel action offset in action list
 */
struct nfp_fl_ct_flow_entry {
	unsigned long cookie;
	struct list_head list_node;
	u32 chain_index;
	enum ct_entry_type type;
	struct net_device *netdev;
	struct nfp_fl_ct_zone_entry *zt;
	struct list_head children;
	struct flow_rule *rule;
	struct flow_stats stats;
	u8 tun_offset;		// Set to NFP_FL_CT_NO_TUN if no tun
};

/**
 * struct nfp_fl_ct_tc_merge - Merge of two flows from tc
 * @cookie:		Flow cookie, combination of pre and post ct cookies
 * @hash_node:		Used by the hashtable
 * @pre_ct_list:	This entry is part of a pre_ct_list
 * @post_ct_list:	This entry is part of a post_ct_list
 * @zt:			Reference to the zone table this belongs to
 * @pre_ct_parent:	The pre_ct_parent
 * @post_ct_parent:	The post_ct_parent
 * @children:		List of nft merged entries
 */
struct nfp_fl_ct_tc_merge {
	unsigned long cookie[2];
	struct rhash_head hash_node;
	struct list_head pre_ct_list;
	struct list_head post_ct_list;
	struct nfp_fl_ct_zone_entry *zt;
	struct nfp_fl_ct_flow_entry *pre_ct_parent;
	struct nfp_fl_ct_flow_entry *post_ct_parent;
	struct list_head children;
};

/**
 * struct nfp_fl_ct_map_entry - Map between flow cookie and specific ct_flow
 * @cookie:	Flow cookie, same as original TC flow, used as key
 * @hash_node:	Used by the hashtable
 * @ct_entry:	Pointer to corresponding ct_entry
 */
struct nfp_fl_ct_map_entry {
	unsigned long cookie;
	struct rhash_head hash_node;
	struct nfp_fl_ct_flow_entry *ct_entry;
};

bool is_pre_ct_flow(struct flow_cls_offload *flow);
bool is_post_ct_flow(struct flow_cls_offload *flow);

/**
 * nfp_fl_ct_handle_pre_ct() - Handles -trk conntrack rules
 * @priv:	Pointer to app priv
 * @netdev:	netdev structure.
 * @flow:	TC flower classifier offload structure.
 * @extack:	Extack pointer for errors
 *
 * Adds a new entry to the relevant zone table and tries to
 * merge with other +trk+est entries and offload if possible.
 *
 * Return: negative value on error, 0 if configured successfully.
 */
int nfp_fl_ct_handle_pre_ct(struct nfp_flower_priv *priv,
			    struct net_device *netdev,
			    struct flow_cls_offload *flow,
			    struct netlink_ext_ack *extack);
/**
 * nfp_fl_ct_handle_post_ct() - Handles +trk+est conntrack rules
 * @priv:	Pointer to app priv
 * @netdev:	netdev structure.
 * @flow:	TC flower classifier offload structure.
 * @extack:	Extack pointer for errors
 *
 * Adds a new entry to the relevant zone table and tries to
 * merge with other -trk entries and offload if possible.
 *
 * Return: negative value on error, 0 if configured successfully.
 */
int nfp_fl_ct_handle_post_ct(struct nfp_flower_priv *priv,
			     struct net_device *netdev,
			     struct flow_cls_offload *flow,
			     struct netlink_ext_ack *extack);

/**
 * nfp_fl_ct_clean_flow_entry() - Free a nfp_fl_ct_flow_entry
 * @entry:	Flow entry to cleanup
 */
void nfp_fl_ct_clean_flow_entry(struct nfp_fl_ct_flow_entry *entry);
#endif
