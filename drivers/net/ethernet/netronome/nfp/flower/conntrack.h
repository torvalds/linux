/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2021 Corigine, Inc. */

#ifndef __NFP_FLOWER_CONNTRACK_H__
#define __NFP_FLOWER_CONNTRACK_H__ 1

#include "main.h"

extern const struct rhashtable_params nfp_zone_table_params;

/**
 * struct nfp_fl_ct_zone_entry - Zone entry containing conntrack flow information
 * @zone:	The zone number, used as lookup key in hashtable
 * @hash_node:	Used by the hashtable
 * @priv:	Pointer to nfp_flower_priv data
 * @nft:	Pointer to nf_flowtable for this zone
 */
struct nfp_fl_ct_zone_entry {
	u16 zone;
	struct rhash_head hash_node;

	struct nfp_flower_priv *priv;
	struct nf_flowtable *nft;
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

#endif
