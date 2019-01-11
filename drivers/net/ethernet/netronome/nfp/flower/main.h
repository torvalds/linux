/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#ifndef __NFP_FLOWER_H__
#define __NFP_FLOWER_H__ 1

#include "cmsg.h"

#include <linux/circ_buf.h>
#include <linux/hashtable.h>
#include <linux/rhashtable.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <net/pkt_cls.h>
#include <net/tcp.h>
#include <linux/workqueue.h>
#include <linux/idr.h>

struct nfp_fl_pre_lag;
struct net_device;
struct nfp_app;

#define NFP_FL_STATS_ELEM_RS		FIELD_SIZEOF(struct nfp_fl_stats_id, \
						     init_unalloc)
#define NFP_FLOWER_MASK_ENTRY_RS	256
#define NFP_FLOWER_MASK_ELEMENT_RS	1
#define NFP_FLOWER_MASK_HASH_BITS	10

#define NFP_FL_META_FLAG_MANAGE_MASK	BIT(7)

#define NFP_FL_MASK_REUSE_TIME_NS	40000
#define NFP_FL_MASK_ID_LOCATION		1

#define NFP_FL_VXLAN_PORT		4789
#define NFP_FL_GENEVE_PORT		6081

/* Extra features bitmap. */
#define NFP_FL_FEATS_GENEVE		BIT(0)
#define NFP_FL_NBI_MTU_SETTING		BIT(1)
#define NFP_FL_FEATS_GENEVE_OPT		BIT(2)
#define NFP_FL_FEATS_VLAN_PCP		BIT(3)
#define NFP_FL_FEATS_LAG		BIT(31)

struct nfp_fl_mask_id {
	struct circ_buf mask_id_free_list;
	ktime_t *last_used;
	u8 init_unallocated;
};

struct nfp_fl_stats_id {
	struct circ_buf free_list;
	u32 init_unalloc;
	u8 repeated_em_count;
};

/**
 * struct nfp_mtu_conf - manage MTU setting
 * @portnum:		NFP port number of repr with requested MTU change
 * @requested_val:	MTU value requested for repr
 * @ack:		Received ack that MTU has been correctly set
 * @wait_q:		Wait queue for MTU acknowledgements
 * @lock:		Lock for setting/reading MTU variables
 */
struct nfp_mtu_conf {
	u32 portnum;
	unsigned int requested_val;
	bool ack;
	wait_queue_head_t wait_q;
	spinlock_t lock;
};

/**
 * struct nfp_fl_lag - Flower APP priv data for link aggregation
 * @work:		Work queue for writing configs to the HW
 * @lock:		Lock to protect lag_group_list
 * @group_list:		List of all master/slave groups offloaded
 * @ida_handle:		IDA to handle group ids
 * @pkt_num:		Incremented for each config packet sent
 * @batch_ver:		Incremented for each batch of config packets
 * @global_inst:	Instance allocator for groups
 * @rst_cfg:		Marker to reset HW LAG config
 * @retrans_skbs:	Cmsgs that could not be processed by HW and require
 *			retransmission
 */
struct nfp_fl_lag {
	struct delayed_work work;
	struct mutex lock;
	struct list_head group_list;
	struct ida ida_handle;
	unsigned int pkt_num;
	unsigned int batch_ver;
	u8 global_inst;
	bool rst_cfg;
	struct sk_buff_head retrans_skbs;
};

/**
 * struct nfp_flower_priv - Flower APP per-vNIC priv data
 * @app:		Back pointer to app
 * @nn:			Pointer to vNIC
 * @mask_id_seed:	Seed used for mask hash table
 * @flower_version:	HW version of flower
 * @flower_ext_feats:	Bitmap of extra features the HW supports
 * @stats_ids:		List of free stats ids
 * @mask_ids:		List of free mask ids
 * @mask_table:		Hash table used to store masks
 * @stats_ring_size:	Maximum number of allowed stats ids
 * @flow_table:		Hash table used to store flower rules
 * @stats:		Stored stats updates for flower rules
 * @stats_lock:		Lock for flower rule stats updates
 * @cmsg_work:		Workqueue for control messages processing
 * @cmsg_skbs_high:	List of higher priority skbs for control message
 *			processing
 * @cmsg_skbs_low:	List of lower priority skbs for control message
 *			processing
 * @nfp_mac_off_list:	List of MAC addresses to offload
 * @nfp_mac_index_list:	List of unique 8-bit indexes for non NFP netdevs
 * @nfp_ipv4_off_list:	List of IPv4 addresses to offload
 * @nfp_neigh_off_list:	List of neighbour offloads
 * @nfp_mac_off_lock:	Lock for the MAC address list
 * @nfp_mac_index_lock:	Lock for the MAC index list
 * @nfp_ipv4_off_lock:	Lock for the IPv4 address list
 * @nfp_neigh_off_lock:	Lock for the neighbour address list
 * @nfp_mac_off_ids:	IDA to manage id assignment for offloaded macs
 * @nfp_mac_off_count:	Number of MACs in address list
 * @nfp_tun_neigh_nb:	Notifier to monitor neighbour state
 * @reify_replies:	atomically stores the number of replies received
 *			from firmware for repr reify
 * @reify_wait_queue:	wait queue for repr reify response counting
 * @mtu_conf:		Configuration of repr MTU value
 * @nfp_lag:		Link aggregation data block
 * @indr_block_cb_priv:	List of priv data passed to indirect block cbs
 */
struct nfp_flower_priv {
	struct nfp_app *app;
	struct nfp_net *nn;
	u32 mask_id_seed;
	u64 flower_version;
	u64 flower_ext_feats;
	struct nfp_fl_stats_id stats_ids;
	struct nfp_fl_mask_id mask_ids;
	DECLARE_HASHTABLE(mask_table, NFP_FLOWER_MASK_HASH_BITS);
	u32 stats_ring_size;
	struct rhashtable flow_table;
	struct nfp_fl_stats *stats;
	spinlock_t stats_lock; /* lock stats */
	struct work_struct cmsg_work;
	struct sk_buff_head cmsg_skbs_high;
	struct sk_buff_head cmsg_skbs_low;
	struct list_head nfp_mac_off_list;
	struct list_head nfp_mac_index_list;
	struct list_head nfp_ipv4_off_list;
	struct list_head nfp_neigh_off_list;
	struct mutex nfp_mac_off_lock;
	struct mutex nfp_mac_index_lock;
	struct mutex nfp_ipv4_off_lock;
	spinlock_t nfp_neigh_off_lock;
	struct ida nfp_mac_off_ids;
	int nfp_mac_off_count;
	struct notifier_block nfp_tun_neigh_nb;
	atomic_t reify_replies;
	wait_queue_head_t reify_wait_queue;
	struct nfp_mtu_conf mtu_conf;
	struct nfp_fl_lag nfp_lag;
	struct list_head indr_block_cb_priv;
};

/**
 * struct nfp_flower_repr_priv - Flower APP per-repr priv data
 * @lag_port_flags:	Extended port flags to record lag state of repr
 */
struct nfp_flower_repr_priv {
	unsigned long lag_port_flags;
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
	struct rhash_head fl_node;
	struct rcu_head rcu;
	__be32 nfp_tun_ipv4_addr;
	struct net_device *ingress_dev;
	char *unmasked_data;
	char *mask_data;
	char *action_data;
};

extern const struct rhashtable_params nfp_flower_table_params;

struct nfp_fl_stats_frame {
	__be32 stats_con_id;
	__be32 pkt_count;
	__be64 byte_count;
	__be64 stats_cookie;
};

int nfp_flower_metadata_init(struct nfp_app *app, u64 host_ctx_count);
void nfp_flower_metadata_cleanup(struct nfp_app *app);

int nfp_flower_setup_tc(struct nfp_app *app, struct net_device *netdev,
			enum tc_setup_type type, void *type_data);
int nfp_flower_compile_flow_match(struct nfp_app *app,
				  struct tc_cls_flower_offload *flow,
				  struct nfp_fl_key_ls *key_ls,
				  struct net_device *netdev,
				  struct nfp_fl_payload *nfp_flow,
				  enum nfp_flower_tun_type tun_type);
int nfp_flower_compile_action(struct nfp_app *app,
			      struct tc_cls_flower_offload *flow,
			      struct net_device *netdev,
			      struct nfp_fl_payload *nfp_flow);
int nfp_compile_flow_metadata(struct nfp_app *app,
			      struct tc_cls_flower_offload *flow,
			      struct nfp_fl_payload *nfp_flow,
			      struct net_device *netdev);
int nfp_modify_flow_metadata(struct nfp_app *app,
			     struct nfp_fl_payload *nfp_flow);

struct nfp_fl_payload *
nfp_flower_search_fl_table(struct nfp_app *app, unsigned long tc_flower_cookie,
			   struct net_device *netdev);
struct nfp_fl_payload *
nfp_flower_remove_fl_table(struct nfp_app *app, unsigned long tc_flower_cookie);

void nfp_flower_rx_flow_stats(struct nfp_app *app, struct sk_buff *skb);

int nfp_tunnel_config_start(struct nfp_app *app);
void nfp_tunnel_config_stop(struct nfp_app *app);
int nfp_tunnel_mac_event_handler(struct nfp_app *app,
				 struct net_device *netdev,
				 unsigned long event, void *ptr);
void nfp_tunnel_write_macs(struct nfp_app *app);
void nfp_tunnel_del_ipv4_off(struct nfp_app *app, __be32 ipv4);
void nfp_tunnel_add_ipv4_off(struct nfp_app *app, __be32 ipv4);
void nfp_tunnel_request_route(struct nfp_app *app, struct sk_buff *skb);
void nfp_tunnel_keep_alive(struct nfp_app *app, struct sk_buff *skb);
void nfp_flower_lag_init(struct nfp_fl_lag *lag);
void nfp_flower_lag_cleanup(struct nfp_fl_lag *lag);
int nfp_flower_lag_reset(struct nfp_fl_lag *lag);
int nfp_flower_lag_netdev_event(struct nfp_flower_priv *priv,
				struct net_device *netdev,
				unsigned long event, void *ptr);
bool nfp_flower_lag_unprocessed_msg(struct nfp_app *app, struct sk_buff *skb);
int nfp_flower_lag_populate_pre_action(struct nfp_app *app,
				       struct net_device *master,
				       struct nfp_fl_pre_lag *pre_act);
int nfp_flower_lag_get_output_id(struct nfp_app *app,
				 struct net_device *master);
int nfp_flower_reg_indir_block_handler(struct nfp_app *app,
				       struct net_device *netdev,
				       unsigned long event);

#endif
