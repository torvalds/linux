/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#ifndef __NFP_FLOWER_H__
#define __NFP_FLOWER_H__ 1

#include "cmsg.h"
#include "../nfp_net.h"

#include <linux/circ_buf.h>
#include <linux/hashtable.h>
#include <linux/rhashtable.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <net/flow_offload.h>
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>
#include <net/tcp.h>
#include <linux/workqueue.h>
#include <linux/idr.h>

struct nfp_fl_pre_lag;
struct net_device;
struct nfp_app;

#define NFP_FL_STAT_ID_MU_NUM		GENMASK(31, 22)
#define NFP_FL_STAT_ID_STAT		GENMASK(21, 0)

#define NFP_FL_STATS_ELEM_RS		sizeof_field(struct nfp_fl_stats_id, \
						     init_unalloc)
#define NFP_FLOWER_MASK_ENTRY_RS	256
#define NFP_FLOWER_MASK_ELEMENT_RS	1
#define NFP_FLOWER_MASK_HASH_BITS	10

#define NFP_FLOWER_KEY_MAX_LW		32

#define NFP_FL_META_FLAG_MANAGE_MASK	BIT(7)

#define NFP_FL_MASK_REUSE_TIME_NS	40000
#define NFP_FL_MASK_ID_LOCATION		1

/* Extra features bitmap. */
#define NFP_FL_FEATS_GENEVE		BIT(0)
#define NFP_FL_NBI_MTU_SETTING		BIT(1)
#define NFP_FL_FEATS_GENEVE_OPT		BIT(2)
#define NFP_FL_FEATS_VLAN_PCP		BIT(3)
#define NFP_FL_FEATS_VF_RLIM		BIT(4)
#define NFP_FL_FEATS_FLOW_MOD		BIT(5)
#define NFP_FL_FEATS_PRE_TUN_RULES	BIT(6)
#define NFP_FL_FEATS_IPV6_TUN		BIT(7)
#define NFP_FL_FEATS_VLAN_QINQ		BIT(8)
#define NFP_FL_FEATS_QOS_PPS		BIT(9)
#define NFP_FL_FEATS_QOS_METER		BIT(10)
#define NFP_FL_FEATS_DECAP_V2		BIT(11)
#define NFP_FL_FEATS_TUNNEL_NEIGH_LAG	BIT(12)
#define NFP_FL_FEATS_HOST_ACK		BIT(31)

#define NFP_FL_ENABLE_FLOW_MERGE	BIT(0)
#define NFP_FL_ENABLE_LAG		BIT(1)

#define NFP_FL_FEATS_HOST \
	(NFP_FL_FEATS_GENEVE | \
	NFP_FL_NBI_MTU_SETTING | \
	NFP_FL_FEATS_GENEVE_OPT | \
	NFP_FL_FEATS_VLAN_PCP | \
	NFP_FL_FEATS_VF_RLIM | \
	NFP_FL_FEATS_FLOW_MOD | \
	NFP_FL_FEATS_PRE_TUN_RULES | \
	NFP_FL_FEATS_IPV6_TUN | \
	NFP_FL_FEATS_VLAN_QINQ | \
	NFP_FL_FEATS_QOS_PPS | \
	NFP_FL_FEATS_QOS_METER | \
	NFP_FL_FEATS_DECAP_V2 | \
	NFP_FL_FEATS_TUNNEL_NEIGH_LAG)

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
 * struct nfp_fl_tunnel_offloads - priv data for tunnel offloads
 * @offloaded_macs:	Hashtable of the offloaded MAC addresses
 * @ipv4_off_list:	List of IPv4 addresses to offload
 * @ipv6_off_list:	List of IPv6 addresses to offload
 * @ipv4_off_lock:	Lock for the IPv4 address list
 * @ipv6_off_lock:	Lock for the IPv6 address list
 * @mac_off_ids:	IDA to manage id assignment for offloaded MACs
 * @neigh_nb:		Notifier to monitor neighbour state
 */
struct nfp_fl_tunnel_offloads {
	struct rhashtable offloaded_macs;
	struct list_head ipv4_off_list;
	struct list_head ipv6_off_list;
	struct mutex ipv4_off_lock;
	struct mutex ipv6_off_lock;
	struct ida mac_off_ids;
	struct notifier_block neigh_nb;
};

/**
 * struct nfp_tun_neigh_lag - lag info
 * @lag_version:	lag version
 * @lag_instance:	lag instance
 */
struct nfp_tun_neigh_lag {
	u8 lag_version[3];
	u8 lag_instance;
};

/**
 * struct nfp_tun_neigh - basic neighbour data
 * @dst_addr:	Destination MAC address
 * @src_addr:	Source MAC address
 * @port_id:	NFP port to output packet on - associated with source IPv4
 */
struct nfp_tun_neigh {
	u8 dst_addr[ETH_ALEN];
	u8 src_addr[ETH_ALEN];
	__be32 port_id;
};

/**
 * struct nfp_tun_neigh_ext - extended neighbour data
 * @vlan_tpid:	VLAN_TPID match field
 * @vlan_tci:	VLAN_TCI match field
 * @host_ctx:	Host context ID to be saved here
 */
struct nfp_tun_neigh_ext {
	__be16 vlan_tpid;
	__be16 vlan_tci;
	__be32 host_ctx;
};

/**
 * struct nfp_tun_neigh_v4 - neighbour/route entry on the NFP for IPv4
 * @dst_ipv4:	Destination IPv4 address
 * @src_ipv4:	Source IPv4 address
 * @common:	Neighbour/route common info
 * @ext:	Neighbour/route extended info
 * @lag:	lag port info
 */
struct nfp_tun_neigh_v4 {
	__be32 dst_ipv4;
	__be32 src_ipv4;
	struct nfp_tun_neigh common;
	struct nfp_tun_neigh_ext ext;
	struct nfp_tun_neigh_lag lag;
};

/**
 * struct nfp_tun_neigh_v6 - neighbour/route entry on the NFP for IPv6
 * @dst_ipv6:	Destination IPv6 address
 * @src_ipv6:	Source IPv6 address
 * @common:	Neighbour/route common info
 * @ext:	Neighbour/route extended info
 * @lag:	lag port info
 */
struct nfp_tun_neigh_v6 {
	struct in6_addr dst_ipv6;
	struct in6_addr src_ipv6;
	struct nfp_tun_neigh common;
	struct nfp_tun_neigh_ext ext;
	struct nfp_tun_neigh_lag lag;
};

/**
 * struct nfp_neigh_entry
 * @neigh_cookie:	Cookie for hashtable lookup
 * @ht_node:		rhash_head entry for hashtable
 * @list_head:		Needed as member of linked_nn_entries list
 * @payload:		The neighbour info payload
 * @flow:		Linked flow rule
 * @is_ipv6:		Flag to indicate if payload is ipv6 or ipv4
 */
struct nfp_neigh_entry {
	unsigned long neigh_cookie;
	struct rhash_head ht_node;
	struct list_head list_head;
	char *payload;
	struct nfp_predt_entry *flow;
	bool is_ipv6;
};

/**
 * struct nfp_predt_entry
 * @list_head:		List head to attach to predt_list
 * @flow_pay:		Direct link to flow_payload
 * @nn_list:		List of linked nfp_neigh_entries
 */
struct nfp_predt_entry {
	struct list_head list_head;
	struct nfp_fl_payload *flow_pay;
	struct list_head nn_list;
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
 * struct nfp_fl_internal_ports - Flower APP priv data for additional ports
 * @port_ids:	Assignment of ids to any additional ports
 * @lock:	Lock for extra ports list
 */
struct nfp_fl_internal_ports {
	struct idr port_ids;
	spinlock_t lock;
};

/**
 * struct nfp_flower_priv - Flower APP per-vNIC priv data
 * @app:		Back pointer to app
 * @nn:			Pointer to vNIC
 * @mask_id_seed:	Seed used for mask hash table
 * @flower_version:	HW version of flower
 * @flower_ext_feats:	Bitmap of extra features the HW supports
 * @flower_en_feats:	Bitmap of features enabled by HW
 * @stats_ids:		List of free stats ids
 * @mask_ids:		List of free mask ids
 * @mask_table:		Hash table used to store masks
 * @stats_ring_size:	Maximum number of allowed stats ids
 * @flow_table:		Hash table used to store flower rules
 * @stats:		Stored stats updates for flower rules
 * @stats_lock:		Lock for flower rule stats updates
 * @stats_ctx_table:	Hash table to map stats contexts to its flow rule
 * @cmsg_work:		Workqueue for control messages processing
 * @cmsg_skbs_high:	List of higher priority skbs for control message
 *			processing
 * @cmsg_skbs_low:	List of lower priority skbs for control message
 *			processing
 * @tun:		Tunnel offload data
 * @reify_replies:	atomically stores the number of replies received
 *			from firmware for repr reify
 * @reify_wait_queue:	wait queue for repr reify response counting
 * @mtu_conf:		Configuration of repr MTU value
 * @nfp_lag:		Link aggregation data block
 * @indr_block_cb_priv:	List of priv data passed to indirect block cbs
 * @non_repr_priv:	List of offloaded non-repr ports and their priv data
 * @active_mem_unit:	Current active memory unit for flower rules
 * @total_mem_units:	Total number of available memory units for flower rules
 * @internal_ports:	Internal port ids used in offloaded rules
 * @qos_stats_work:	Workqueue for qos stats processing
 * @qos_rate_limiters:	Current active qos rate limiters
 * @qos_stats_lock:	Lock on qos stats updates
 * @meter_stats_lock:   Lock on meter stats updates
 * @meter_table:	Hash table used to store the meter table
 * @pre_tun_rule_cnt:	Number of pre-tunnel rules offloaded
 * @merge_table:	Hash table to store merged flows
 * @ct_zone_table:	Hash table used to store the different zones
 * @ct_zone_wc:		Special zone entry for wildcarded zone matches
 * @ct_map_table:	Hash table used to referennce ct flows
 * @predt_list:		List to keep track of decap pretun flows
 * @neigh_table:	Table to keep track of neighbor entries
 * @predt_lock:		Lock to serialise predt/neigh table updates
 * @nfp_fl_lock:	Lock to protect the flow offload operation
 */
struct nfp_flower_priv {
	struct nfp_app *app;
	struct nfp_net *nn;
	u32 mask_id_seed;
	u64 flower_version;
	u64 flower_ext_feats;
	u8 flower_en_feats;
	struct nfp_fl_stats_id stats_ids;
	struct nfp_fl_mask_id mask_ids;
	DECLARE_HASHTABLE(mask_table, NFP_FLOWER_MASK_HASH_BITS);
	u32 stats_ring_size;
	struct rhashtable flow_table;
	struct nfp_fl_stats *stats;
	spinlock_t stats_lock; /* lock stats */
	struct rhashtable stats_ctx_table;
	struct work_struct cmsg_work;
	struct sk_buff_head cmsg_skbs_high;
	struct sk_buff_head cmsg_skbs_low;
	struct nfp_fl_tunnel_offloads tun;
	atomic_t reify_replies;
	wait_queue_head_t reify_wait_queue;
	struct nfp_mtu_conf mtu_conf;
	struct nfp_fl_lag nfp_lag;
	struct list_head indr_block_cb_priv;
	struct list_head non_repr_priv;
	unsigned int active_mem_unit;
	unsigned int total_mem_units;
	struct nfp_fl_internal_ports internal_ports;
	struct delayed_work qos_stats_work;
	unsigned int qos_rate_limiters;
	spinlock_t qos_stats_lock; /* Protect the qos stats */
	struct mutex meter_stats_lock; /* Protect the meter stats */
	struct rhashtable meter_table;
	int pre_tun_rule_cnt;
	struct rhashtable merge_table;
	struct rhashtable ct_zone_table;
	struct nfp_fl_ct_zone_entry *ct_zone_wc;
	struct rhashtable ct_map_table;
	struct list_head predt_list;
	struct rhashtable neigh_table;
	spinlock_t predt_lock; /* Lock to serialise predt/neigh table updates */
	struct mutex nfp_fl_lock; /* Protect the flow operation */
};

/**
 * struct nfp_fl_qos - Flower APP priv data for quality of service
 * @netdev_port_id:	NFP port number of repr with qos info
 * @curr_stats:		Currently stored stats updates for qos info
 * @prev_stats:		Previously stored updates for qos info
 * @last_update:	Stored time when last stats were updated
 */
struct nfp_fl_qos {
	u32 netdev_port_id;
	struct nfp_stat_pair curr_stats;
	struct nfp_stat_pair prev_stats;
	u64 last_update;
};

/**
 * struct nfp_flower_repr_priv - Flower APP per-repr priv data
 * @nfp_repr:		Back pointer to nfp_repr
 * @lag_port_flags:	Extended port flags to record lag state of repr
 * @mac_offloaded:	Flag indicating a MAC address is offloaded for repr
 * @offloaded_mac_addr:	MAC address that has been offloaded for repr
 * @block_shared:	Flag indicating if offload applies to shared blocks
 * @mac_list:		List entry of reprs that share the same offloaded MAC
 * @qos_table:		Stored info on filters implementing qos
 * @on_bridge:		Indicates if the repr is attached to a bridge
 */
struct nfp_flower_repr_priv {
	struct nfp_repr *nfp_repr;
	unsigned long lag_port_flags;
	bool mac_offloaded;
	u8 offloaded_mac_addr[ETH_ALEN];
	bool block_shared;
	struct list_head mac_list;
	struct nfp_fl_qos qos_table;
	bool on_bridge;
};

/**
 * struct nfp_flower_non_repr_priv - Priv data for non-repr offloaded ports
 * @list:		List entry of offloaded reprs
 * @netdev:		Pointer to non-repr net_device
 * @ref_count:		Number of references held for this priv data
 * @mac_offloaded:	Flag indicating a MAC address is offloaded for device
 * @offloaded_mac_addr:	MAC address that has been offloaded for dev
 */
struct nfp_flower_non_repr_priv {
	struct list_head list;
	struct net_device *netdev;
	int ref_count;
	bool mac_offloaded;
	u8 offloaded_mac_addr[ETH_ALEN];
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

/**
 * struct nfp_ipv6_addr_entry - cached IPv6 addresses
 * @ipv6_addr:	IP address
 * @ref_count:	number of rules currently using this IP
 * @list:	list pointer
 */
struct nfp_ipv6_addr_entry {
	struct in6_addr ipv6_addr;
	int ref_count;
	struct list_head list;
};

struct nfp_fl_payload {
	struct nfp_fl_rule_metadata meta;
	unsigned long tc_flower_cookie;
	struct rhash_head fl_node;
	struct rcu_head rcu;
	__be32 nfp_tun_ipv4_addr;
	struct nfp_ipv6_addr_entry *nfp_tun_ipv6;
	struct net_device *ingress_dev;
	char *unmasked_data;
	char *mask_data;
	char *action_data;
	struct list_head linked_flows;
	bool in_hw;
	struct {
		struct nfp_predt_entry *predt;
		struct net_device *dev;
		__be16 vlan_tpid;
		__be16 vlan_tci;
		__be16 port_idx;
		u8 loc_mac[ETH_ALEN];
		u8 rem_mac[ETH_ALEN];
		bool is_ipv6;
	} pre_tun_rule;
};

struct nfp_fl_payload_link {
	/* A link contains a pointer to a merge flow and an associated sub_flow.
	 * Each merge flow will feature in 2 links to its underlying sub_flows.
	 * A sub_flow will have at least 1 link to a merge flow or more if it
	 * has been used to create multiple merge flows.
	 *
	 * For a merge flow, 'linked_flows' in its nfp_fl_payload struct lists
	 * all links to sub_flows (sub_flow.flow) via merge.list.
	 * For a sub_flow, 'linked_flows' gives all links to merge flows it has
	 * formed (merge_flow.flow) via sub_flow.list.
	 */
	struct {
		struct list_head list;
		struct nfp_fl_payload *flow;
	} merge_flow, sub_flow;
};

extern const struct rhashtable_params nfp_flower_table_params;
extern const struct rhashtable_params merge_table_params;
extern const struct rhashtable_params neigh_table_params;

struct nfp_merge_info {
	u64 parent_ctx;
	struct rhash_head ht_node;
};

struct nfp_fl_stats_frame {
	__be32 stats_con_id;
	__be32 pkt_count;
	__be64 byte_count;
	__be64 stats_cookie;
};

struct nfp_meter_stats_entry {
	u64 pkts;
	u64 bytes;
	u64 drops;
};

struct nfp_meter_entry {
	struct rhash_head ht_node;
	u32 meter_id;
	bool bps;
	u32 rate;
	u32 burst;
	u64 used;
	struct nfp_meter_stats {
		u64 update;
		struct nfp_meter_stats_entry curr;
		struct nfp_meter_stats_entry prev;
	} stats;
};

enum nfp_meter_op {
	NFP_METER_ADD,
	NFP_METER_DEL,
};

static inline bool
nfp_flower_internal_port_can_offload(struct nfp_app *app,
				     struct net_device *netdev)
{
	struct nfp_flower_priv *app_priv = app->priv;

	if (!(app_priv->flower_en_feats & NFP_FL_ENABLE_FLOW_MERGE))
		return false;
	if (!netdev->rtnl_link_ops)
		return false;
	if (!strcmp(netdev->rtnl_link_ops->kind, "openvswitch"))
		return true;

	return false;
}

/* The address of the merged flow acts as its cookie.
 * Cookies supplied to us by TC flower are also addresses to allocated
 * memory and thus this scheme should not generate any collisions.
 */
static inline bool nfp_flower_is_merge_flow(struct nfp_fl_payload *flow_pay)
{
	return flow_pay->tc_flower_cookie == (unsigned long)flow_pay;
}

static inline bool nfp_flower_is_supported_bridge(struct net_device *netdev)
{
	return netif_is_ovs_master(netdev);
}

int nfp_flower_metadata_init(struct nfp_app *app, u64 host_ctx_count,
			     unsigned int host_ctx_split);
void nfp_flower_metadata_cleanup(struct nfp_app *app);

int nfp_flower_setup_tc(struct nfp_app *app, struct net_device *netdev,
			enum tc_setup_type type, void *type_data);
int nfp_flower_merge_offloaded_flows(struct nfp_app *app,
				     struct nfp_fl_payload *sub_flow1,
				     struct nfp_fl_payload *sub_flow2);
void
nfp_flower_compile_meta(struct nfp_flower_meta_tci *ext,
			struct nfp_flower_meta_tci *msk, u8 key_type);
void
nfp_flower_compile_tci(struct nfp_flower_meta_tci *ext,
		       struct nfp_flower_meta_tci *msk,
		       struct flow_rule *rule);
void
nfp_flower_compile_ext_meta(struct nfp_flower_ext_meta *frame, u32 key_ext);
int
nfp_flower_compile_port(struct nfp_flower_in_port *frame, u32 cmsg_port,
			bool mask_version, enum nfp_flower_tun_type tun_type,
			struct netlink_ext_ack *extack);
void
nfp_flower_compile_mac(struct nfp_flower_mac_mpls *ext,
		       struct nfp_flower_mac_mpls *msk,
		       struct flow_rule *rule);
int
nfp_flower_compile_mpls(struct nfp_flower_mac_mpls *ext,
			struct nfp_flower_mac_mpls *msk,
			struct flow_rule *rule,
			struct netlink_ext_ack *extack);
void
nfp_flower_compile_tport(struct nfp_flower_tp_ports *ext,
			 struct nfp_flower_tp_ports *msk,
			 struct flow_rule *rule);
void
nfp_flower_compile_vlan(struct nfp_flower_vlan *ext,
			struct nfp_flower_vlan *msk,
			struct flow_rule *rule);
void
nfp_flower_compile_ipv4(struct nfp_flower_ipv4 *ext,
			struct nfp_flower_ipv4 *msk, struct flow_rule *rule);
void
nfp_flower_compile_ipv6(struct nfp_flower_ipv6 *ext,
			struct nfp_flower_ipv6 *msk, struct flow_rule *rule);
void
nfp_flower_compile_geneve_opt(u8 *ext, u8 *msk, struct flow_rule *rule);
void
nfp_flower_compile_ipv4_gre_tun(struct nfp_flower_ipv4_gre_tun *ext,
				struct nfp_flower_ipv4_gre_tun *msk,
				struct flow_rule *rule);
void
nfp_flower_compile_ipv4_udp_tun(struct nfp_flower_ipv4_udp_tun *ext,
				struct nfp_flower_ipv4_udp_tun *msk,
				struct flow_rule *rule);
void
nfp_flower_compile_ipv6_udp_tun(struct nfp_flower_ipv6_udp_tun *ext,
				struct nfp_flower_ipv6_udp_tun *msk,
				struct flow_rule *rule);
void
nfp_flower_compile_ipv6_gre_tun(struct nfp_flower_ipv6_gre_tun *ext,
				struct nfp_flower_ipv6_gre_tun *msk,
				struct flow_rule *rule);
int nfp_flower_compile_flow_match(struct nfp_app *app,
				  struct flow_rule *rule,
				  struct nfp_fl_key_ls *key_ls,
				  struct net_device *netdev,
				  struct nfp_fl_payload *nfp_flow,
				  enum nfp_flower_tun_type tun_type,
				  struct netlink_ext_ack *extack);
int nfp_flower_compile_action(struct nfp_app *app,
			      struct flow_rule *rule,
			      struct net_device *netdev,
			      struct nfp_fl_payload *nfp_flow,
			      struct netlink_ext_ack *extack);
int nfp_compile_flow_metadata(struct nfp_app *app, u32 cookie,
			      struct nfp_fl_payload *nfp_flow,
			      struct net_device *netdev,
			      struct netlink_ext_ack *extack);
void __nfp_modify_flow_metadata(struct nfp_flower_priv *priv,
				struct nfp_fl_payload *nfp_flow);
int nfp_modify_flow_metadata(struct nfp_app *app,
			     struct nfp_fl_payload *nfp_flow);

struct nfp_fl_payload *
nfp_flower_search_fl_table(struct nfp_app *app, unsigned long tc_flower_cookie,
			   struct net_device *netdev);
struct nfp_fl_payload *
nfp_flower_get_fl_payload_from_ctx(struct nfp_app *app, u32 ctx_id);
struct nfp_fl_payload *
nfp_flower_remove_fl_table(struct nfp_app *app, unsigned long tc_flower_cookie);

void nfp_flower_rx_flow_stats(struct nfp_app *app, struct sk_buff *skb);

int nfp_tunnel_config_start(struct nfp_app *app);
void nfp_tunnel_config_stop(struct nfp_app *app);
int nfp_tunnel_mac_event_handler(struct nfp_app *app,
				 struct net_device *netdev,
				 unsigned long event, void *ptr);
void nfp_tunnel_del_ipv4_off(struct nfp_app *app, __be32 ipv4);
void nfp_tunnel_add_ipv4_off(struct nfp_app *app, __be32 ipv4);
void
nfp_tunnel_put_ipv6_off(struct nfp_app *app, struct nfp_ipv6_addr_entry *entry);
struct nfp_ipv6_addr_entry *
nfp_tunnel_add_ipv6_off(struct nfp_app *app, struct in6_addr *ipv6);
void nfp_tunnel_request_route_v4(struct nfp_app *app, struct sk_buff *skb);
void nfp_tunnel_request_route_v6(struct nfp_app *app, struct sk_buff *skb);
void nfp_tunnel_keep_alive(struct nfp_app *app, struct sk_buff *skb);
void nfp_tunnel_keep_alive_v6(struct nfp_app *app, struct sk_buff *skb);
void nfp_flower_lag_init(struct nfp_fl_lag *lag);
void nfp_flower_lag_cleanup(struct nfp_fl_lag *lag);
int nfp_flower_lag_reset(struct nfp_fl_lag *lag);
int nfp_flower_lag_netdev_event(struct nfp_flower_priv *priv,
				struct net_device *netdev,
				unsigned long event, void *ptr);
bool nfp_flower_lag_unprocessed_msg(struct nfp_app *app, struct sk_buff *skb);
int nfp_flower_lag_populate_pre_action(struct nfp_app *app,
				       struct net_device *master,
				       struct nfp_fl_pre_lag *pre_act,
				       struct netlink_ext_ack *extack);
int nfp_flower_lag_get_output_id(struct nfp_app *app,
				 struct net_device *master);
void nfp_flower_lag_get_info_from_netdev(struct nfp_app *app,
					 struct net_device *netdev,
					 struct nfp_tun_neigh_lag *lag);
void nfp_flower_qos_init(struct nfp_app *app);
void nfp_flower_qos_cleanup(struct nfp_app *app);
int nfp_flower_setup_qos_offload(struct nfp_app *app, struct net_device *netdev,
				 struct tc_cls_matchall_offload *flow);
void nfp_flower_stats_rlim_reply(struct nfp_app *app, struct sk_buff *skb);
int nfp_flower_indr_setup_tc_cb(struct net_device *netdev, struct Qdisc *sch, void *cb_priv,
				enum tc_setup_type type, void *type_data,
				void *data,
				void (*cleanup)(struct flow_block_cb *block_cb));
void nfp_flower_setup_indr_tc_release(void *cb_priv);

void
__nfp_flower_non_repr_priv_get(struct nfp_flower_non_repr_priv *non_repr_priv);
struct nfp_flower_non_repr_priv *
nfp_flower_non_repr_priv_get(struct nfp_app *app, struct net_device *netdev);
void
__nfp_flower_non_repr_priv_put(struct nfp_flower_non_repr_priv *non_repr_priv);
void
nfp_flower_non_repr_priv_put(struct nfp_app *app, struct net_device *netdev);
u32 nfp_flower_get_port_id_from_netdev(struct nfp_app *app,
				       struct net_device *netdev);
void nfp_tun_link_and_update_nn_entries(struct nfp_app *app,
					struct nfp_predt_entry *predt);
void nfp_tun_unlink_and_update_nn_entries(struct nfp_app *app,
					  struct nfp_predt_entry *predt);
int nfp_flower_xmit_pre_tun_flow(struct nfp_app *app,
				 struct nfp_fl_payload *flow);
int nfp_flower_xmit_pre_tun_del_flow(struct nfp_app *app,
				     struct nfp_fl_payload *flow);

struct nfp_fl_payload *
nfp_flower_allocate_new(struct nfp_fl_key_ls *key_layer);
int nfp_flower_calculate_key_layers(struct nfp_app *app,
				    struct net_device *netdev,
				    struct nfp_fl_key_ls *ret_key_ls,
				    struct flow_rule *flow,
				    enum nfp_flower_tun_type *tun_type,
				    struct netlink_ext_ack *extack);
void
nfp_flower_del_linked_merge_flows(struct nfp_app *app,
				  struct nfp_fl_payload *sub_flow);
int
nfp_flower_xmit_flow(struct nfp_app *app, struct nfp_fl_payload *nfp_flow,
		     u8 mtype);
void
nfp_flower_update_merge_stats(struct nfp_app *app,
			      struct nfp_fl_payload *sub_flow);

int nfp_setup_tc_act_offload(struct nfp_app *app,
			     struct flow_offload_action *fl_act);
int nfp_init_meter_table(struct nfp_app *app);
void nfp_flower_stats_meter_request_all(struct nfp_flower_priv *fl_priv);
void nfp_act_stats_reply(struct nfp_app *app, void *pmsg);
int nfp_flower_offload_one_police(struct nfp_app *app, bool ingress,
				  bool pps, u32 id, u32 rate, u32 burst);
int nfp_flower_setup_meter_entry(struct nfp_app *app,
				 const struct flow_action_entry *action,
				 enum nfp_meter_op op,
				 u32 meter_id);
struct nfp_meter_entry *
nfp_flower_search_meter_entry(struct nfp_app *app, u32 meter_id);
#endif
