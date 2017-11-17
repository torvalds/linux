/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
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

#ifndef __NFP_FLOWER_H__
#define __NFP_FLOWER_H__ 1

#include <linux/circ_buf.h>
#include <linux/hashtable.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <net/pkt_cls.h>
#include <linux/workqueue.h>

struct net_device;
struct nfp_app;

#define NFP_FL_STATS_ENTRY_RS		BIT(20)
#define NFP_FL_STATS_ELEM_RS		4
#define NFP_FL_REPEATED_HASH_MAX	BIT(17)
#define NFP_FLOWER_HASH_BITS		19
#define NFP_FLOWER_MASK_ENTRY_RS	256
#define NFP_FLOWER_MASK_ELEMENT_RS	1
#define NFP_FLOWER_MASK_HASH_BITS	10

#define NFP_FL_META_FLAG_MANAGE_MASK	BIT(7)

#define NFP_FL_MASK_REUSE_TIME_NS	40000
#define NFP_FL_MASK_ID_LOCATION		1

struct nfp_fl_mask_id {
	struct circ_buf mask_id_free_list;
	struct timespec64 *last_used;
	u8 init_unallocated;
};

struct nfp_fl_stats_id {
	struct circ_buf free_list;
	u32 init_unalloc;
	u8 repeated_em_count;
};

/**
 * struct nfp_flower_priv - Flower APP per-vNIC priv data
 * @app:		Back pointer to app
 * @nn:			Pointer to vNIC
 * @mask_id_seed:	Seed used for mask hash table
 * @flower_version:	HW version of flower
 * @stats_ids:		List of free stats ids
 * @mask_ids:		List of free mask ids
 * @mask_table:		Hash table used to store masks
 * @flow_table:		Hash table used to store flower rules
 * @cmsg_work:		Workqueue for control messages processing
 * @cmsg_skbs:		List of skbs for control message processing
 */
struct nfp_flower_priv {
	struct nfp_app *app;
	struct nfp_net *nn;
	u32 mask_id_seed;
	u64 flower_version;
	struct nfp_fl_stats_id stats_ids;
	struct nfp_fl_mask_id mask_ids;
	DECLARE_HASHTABLE(mask_table, NFP_FLOWER_MASK_HASH_BITS);
	DECLARE_HASHTABLE(flow_table, NFP_FLOWER_HASH_BITS);
	struct work_struct cmsg_work;
	struct sk_buff_head cmsg_skbs;
};

struct nfp_fl_key_ls {
	u32 key_layer_two;
	u8 key_layer;
	int key_size;
};

struct nfp_fl_rule_metadata {
	u8 key_len;
	u8 mask_len;
	u8 act_len;
	u8 flags;
	__be32 host_ctx_id;
	__be64 host_cookie __packed;
	__be64 flow_version __packed;
	__be32 shortcut;
};

struct nfp_fl_stats {
	u64 pkts;
	u64 bytes;
	u64 used;
};

struct nfp_fl_payload {
	struct nfp_fl_rule_metadata meta;
	unsigned long tc_flower_cookie;
	struct hlist_node link;
	struct rcu_head rcu;
	spinlock_t lock; /* lock stats */
	struct nfp_fl_stats stats;
	char *unmasked_data;
	char *mask_data;
	char *action_data;
};

struct nfp_fl_stats_frame {
	__be32 stats_con_id;
	__be32 pkt_count;
	__be64 byte_count;
	__be64 stats_cookie;
};

int nfp_flower_metadata_init(struct nfp_app *app);
void nfp_flower_metadata_cleanup(struct nfp_app *app);

int nfp_flower_setup_tc(struct nfp_app *app, struct net_device *netdev,
			enum tc_setup_type type, void *type_data);
int nfp_flower_compile_flow_match(struct tc_cls_flower_offload *flow,
				  struct nfp_fl_key_ls *key_ls,
				  struct net_device *netdev,
				  struct nfp_fl_payload *nfp_flow);
int nfp_flower_compile_action(struct tc_cls_flower_offload *flow,
			      struct net_device *netdev,
			      struct nfp_fl_payload *nfp_flow);
int nfp_compile_flow_metadata(struct nfp_app *app,
			      struct tc_cls_flower_offload *flow,
			      struct nfp_fl_payload *nfp_flow);
int nfp_modify_flow_metadata(struct nfp_app *app,
			     struct nfp_fl_payload *nfp_flow);

struct nfp_fl_payload *
nfp_flower_search_fl_table(struct nfp_app *app, unsigned long tc_flower_cookie);
struct nfp_fl_payload *
nfp_flower_remove_fl_table(struct nfp_app *app, unsigned long tc_flower_cookie);

void nfp_flower_rx_flow_stats(struct nfp_app *app, struct sk_buff *skb);

#endif
