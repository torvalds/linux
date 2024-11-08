/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2021 Corigine, Inc. */

#ifndef __NFP_FLOWER_CONNTRACK_H__
#define __NFP_FLOWER_CONNTRACK_H__ 1

#include <net/netfilter/nf_flow_table.h>
#include "main.h"

#define NFP_FL_CT_NO_TUN	0xff

#define COMPARE_UNMASKED_FIELDS(__match1, __match2, __out)	\
	do {							\
		typeof(__match1) _match1 = (__match1);		\
		typeof(__match2) _match2 = (__match2);		\
		bool *_out = (__out);		\
		int i, size = sizeof(*(_match1).key);		\
		char *k1, *m1, *k2, *m2;			\
		*_out = false;					\
		k1 = (char *)_match1.key;			\
		m1 = (char *)_match1.mask;			\
		k2 = (char *)_match2.key;			\
		m2 = (char *)_match2.mask;			\
		for (i = 0; i < size; i++)			\
			if ((k1[i] & m1[i] & m2[i]) ^		\
			    (k2[i] & m1[i] & m2[i])) {		\
				*_out = true;			\
				break;				\
			}					\
	} while (0)						\

extern const struct rhashtable_params nfp_zone_table_params;
extern const struct rhashtable_params nfp_ct_map_params;
extern const struct rhashtable_params nfp_tc_ct_merge_params;
extern const struct rhashtable_params nfp_nft_ct_merge_params;

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
 *
 * @nft_flows_list:	The list of nft relatednfp_fl_ct_flow_entry entries
 * @nft_flows_count:	Keep count of the number of nft_flow entries
 *
 * @nft_merge_tb:	The table of merged tc+nft flows
 * @nft_merge_count:	Keep count of the number of merged tc+nft entries
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

	struct list_head nft_flows_list;
	unsigned int nft_flows_count;

	struct rhashtable nft_merge_tb;
	unsigned int nft_merge_count;
};

enum ct_entry_type {
	CT_TYPE_PRE_CT,
	CT_TYPE_NFT,
	CT_TYPE_POST_CT,
	_CT_TYPE_MAX,
};

enum nfp_nfp_layer_name {
	FLOW_PAY_META_TCI =    0,
	FLOW_PAY_INPORT,
	FLOW_PAY_EXT_META,
	FLOW_PAY_MAC_MPLS,
	FLOW_PAY_L4,
	FLOW_PAY_IPV4,
	FLOW_PAY_IPV6,
	FLOW_PAY_CT,
	FLOW_PAY_GRE,
	FLOW_PAY_QINQ,
	FLOW_PAY_UDP_TUN,
	FLOW_PAY_GENEVE_OPT,

	_FLOW_PAY_LAYERS_MAX
};

/* NFP flow entry flags. */
#define NFP_FL_ACTION_DO_NAT		BIT(0)
#define NFP_FL_ACTION_DO_MANGLE		BIT(1)

/**
 * struct nfp_fl_ct_flow_entry - Flow entry containing conntrack flow information
 * @cookie:	Flow cookie, same as original TC flow, used as key
 * @list_node:	Used by the list
 * @chain_index:	Chain index of the original flow
 * @goto_chain_index:	goto chain index of the flow
 * @netdev:	netdev structure.
 * @type:	Type of pre-entry from enum ct_entry_type
 * @zt:		Reference to the zone table this belongs to
 * @children:	List of tc_merge flows this flow forms part of
 * @rule:	Reference to the original TC flow rule
 * @stats:	Used to cache stats for updating
 * @tun_offset: Used to indicate tunnel action offset in action list
 * @flags:	Used to indicate flow flag like NAT which used by merge.
 */
struct nfp_fl_ct_flow_entry {
	unsigned long cookie;
	struct list_head list_node;
	u32 chain_index;
	u32 goto_chain_index;
	enum ct_entry_type type;
	struct net_device *netdev;
	struct nfp_fl_ct_zone_entry *zt;
	struct list_head children;
	struct flow_rule *rule;
	struct flow_stats stats;
	u8 tun_offset;		// Set to NFP_FL_CT_NO_TUN if no tun
	u8 flags;
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
 * struct nfp_fl_nft_tc_merge - Merge of tc_merge flows with nft flow
 * @netdev:		Ingress netdev name
 * @cookie:		Flow cookie, combination of tc_merge and nft cookies
 * @hash_node:		Used by the hashtable
 * @zt:	Reference to the zone table this belongs to
 * @nft_flow_list:	This entry is part of a nft_flows_list
 * @tc_merge_list:	This entry is part of a ct_merge_list
 * @tc_m_parent:	The tc_merge parent
 * @nft_parent:	The nft_entry parent
 * @tc_flower_cookie:	The cookie of the flow offloaded to the nfp
 * @flow_pay:	Reference to the offloaded flow struct
 */
struct nfp_fl_nft_tc_merge {
	struct net_device *netdev;
	unsigned long cookie[3];
	struct rhash_head hash_node;
	struct nfp_fl_ct_zone_entry *zt;
	struct list_head nft_flow_list;
	struct list_head tc_merge_list;
	struct nfp_fl_ct_tc_merge *tc_m_parent;
	struct nfp_fl_ct_flow_entry *nft_parent;
	unsigned long tc_flower_cookie;
	struct nfp_fl_payload *flow_pay;
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

/**
 * nfp_fl_ct_del_flow() - Handle flow_del callbacks for conntrack
 * @ct_map_ent:	ct map entry for the flow that needs deleting
 */
int nfp_fl_ct_del_flow(struct nfp_fl_ct_map_entry *ct_map_ent);

/**
 * nfp_fl_ct_handle_nft_flow() - Handle flower flow callbacks for nft table
 * @type:	Type provided by callback
 * @type_data:	Callback data
 * @cb_priv:	Pointer to data provided when registering the callback, in this
 *		case it's the zone table.
 */
int nfp_fl_ct_handle_nft_flow(enum tc_setup_type type, void *type_data,
			      void *cb_priv);

/**
 * nfp_fl_ct_stats() - Handle flower stats callbacks for ct flows
 * @flow:	TC flower classifier offload structure.
 * @ct_map_ent:	ct map entry for the flow that needs deleting
 */
int nfp_fl_ct_stats(struct flow_cls_offload *flow,
		    struct nfp_fl_ct_map_entry *ct_map_ent);
#endif
