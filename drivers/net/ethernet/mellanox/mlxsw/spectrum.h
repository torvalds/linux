/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_SPECTRUM_H
#define _MLXSW_SPECTRUM_H

#include <linux/ethtool.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/rhashtable.h>
#include <linux/bitops.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/list.h>
#include <linux/dcbnl.h>
#include <linux/in6.h>
#include <linux/notifier.h>
#include <linux/net_namespace.h>
#include <linux/spinlock.h>
#include <net/psample.h>
#include <net/pkt_cls.h>
#include <net/red.h>
#include <net/vxlan.h>
#include <net/flow_offload.h>

#include "port.h"
#include "core.h"
#include "core_acl_flex_keys.h"
#include "core_acl_flex_actions.h"
#include "reg.h"

#define MLXSW_SP_DEFAULT_VID (VLAN_N_VID - 1)

#define MLXSW_SP_FID_8021D_MAX 1024

#define MLXSW_SP_MID_MAX 7000

#define MLXSW_SP_KVD_LINEAR_SIZE 98304 /* entries */
#define MLXSW_SP_KVD_GRANULARITY 128

#define MLXSW_SP_RESOURCE_NAME_KVD "kvd"
#define MLXSW_SP_RESOURCE_NAME_KVD_LINEAR "linear"
#define MLXSW_SP_RESOURCE_NAME_KVD_HASH_SINGLE "hash_single"
#define MLXSW_SP_RESOURCE_NAME_KVD_HASH_DOUBLE "hash_double"
#define MLXSW_SP_RESOURCE_NAME_KVD_LINEAR_SINGLES "singles"
#define MLXSW_SP_RESOURCE_NAME_KVD_LINEAR_CHUNKS "chunks"
#define MLXSW_SP_RESOURCE_NAME_KVD_LINEAR_LARGE_CHUNKS "large_chunks"

#define MLXSW_SP_RESOURCE_NAME_SPAN "span_agents"

#define MLXSW_SP_RESOURCE_NAME_COUNTERS "counters"
#define MLXSW_SP_RESOURCE_NAME_COUNTERS_FLOW "flow"
#define MLXSW_SP_RESOURCE_NAME_COUNTERS_RIF "rif"

enum mlxsw_sp_resource_id {
	MLXSW_SP_RESOURCE_KVD = MLXSW_CORE_RESOURCE_MAX,
	MLXSW_SP_RESOURCE_KVD_LINEAR,
	MLXSW_SP_RESOURCE_KVD_HASH_SINGLE,
	MLXSW_SP_RESOURCE_KVD_HASH_DOUBLE,
	MLXSW_SP_RESOURCE_KVD_LINEAR_SINGLE,
	MLXSW_SP_RESOURCE_KVD_LINEAR_CHUNKS,
	MLXSW_SP_RESOURCE_KVD_LINEAR_LARGE_CHUNKS,
	MLXSW_SP_RESOURCE_SPAN,
	MLXSW_SP_RESOURCE_COUNTERS,
	MLXSW_SP_RESOURCE_COUNTERS_FLOW,
	MLXSW_SP_RESOURCE_COUNTERS_RIF,
	MLXSW_SP_RESOURCE_GLOBAL_POLICERS,
	MLXSW_SP_RESOURCE_SINGLE_RATE_POLICERS,
};

struct mlxsw_sp_port;
struct mlxsw_sp_rif;
struct mlxsw_sp_span_entry;
enum mlxsw_sp_l3proto;
union mlxsw_sp_l3addr;

struct mlxsw_sp_upper {
	struct net_device *dev;
	unsigned int ref_count;
};

enum mlxsw_sp_rif_type {
	MLXSW_SP_RIF_TYPE_SUBPORT,
	MLXSW_SP_RIF_TYPE_VLAN,
	MLXSW_SP_RIF_TYPE_FID,
	MLXSW_SP_RIF_TYPE_IPIP_LB, /* IP-in-IP loopback. */
	MLXSW_SP_RIF_TYPE_MAX,
};

struct mlxsw_sp_rif_ops;

extern const struct mlxsw_sp_rif_ops *mlxsw_sp1_rif_ops_arr[];
extern const struct mlxsw_sp_rif_ops *mlxsw_sp2_rif_ops_arr[];

enum mlxsw_sp_fid_type {
	MLXSW_SP_FID_TYPE_8021Q,
	MLXSW_SP_FID_TYPE_8021D,
	MLXSW_SP_FID_TYPE_RFID,
	MLXSW_SP_FID_TYPE_DUMMY,
	MLXSW_SP_FID_TYPE_MAX,
};

enum mlxsw_sp_nve_type {
	MLXSW_SP_NVE_TYPE_VXLAN,
};

struct mlxsw_sp_mid {
	struct list_head list;
	unsigned char addr[ETH_ALEN];
	u16 fid;
	u16 mid;
	bool in_hw;
	unsigned long *ports_in_mid; /* bits array */
};

struct mlxsw_sp_sb;
struct mlxsw_sp_bridge;
struct mlxsw_sp_router;
struct mlxsw_sp_mr;
struct mlxsw_sp_acl;
struct mlxsw_sp_counter_pool;
struct mlxsw_sp_fid_core;
struct mlxsw_sp_kvdl;
struct mlxsw_sp_nve;
struct mlxsw_sp_kvdl_ops;
struct mlxsw_sp_mr_tcam_ops;
struct mlxsw_sp_acl_rulei_ops;
struct mlxsw_sp_acl_tcam_ops;
struct mlxsw_sp_nve_ops;
struct mlxsw_sp_sb_ops;
struct mlxsw_sp_sb_vals;
struct mlxsw_sp_port_type_speed_ops;
struct mlxsw_sp_ptp_state;
struct mlxsw_sp_ptp_ops;
struct mlxsw_sp_span_ops;
struct mlxsw_sp_qdisc_state;
struct mlxsw_sp_mall_entry;

struct mlxsw_sp_port_mapping {
	u8 module;
	u8 width;
	u8 lane;
};

struct mlxsw_sp {
	struct mlxsw_sp_port **ports;
	struct mlxsw_core *core;
	const struct mlxsw_bus_info *bus_info;
	unsigned char base_mac[ETH_ALEN];
	const unsigned char *mac_mask;
	struct mlxsw_sp_upper *lags;
	struct mlxsw_sp_port_mapping **port_mapping;
	struct rhashtable sample_trigger_ht;
	struct mlxsw_sp_sb *sb;
	struct mlxsw_sp_bridge *bridge;
	struct mlxsw_sp_router *router;
	struct mlxsw_sp_mr *mr;
	struct mlxsw_afa *afa;
	struct mlxsw_sp_acl *acl;
	struct mlxsw_sp_fid_core *fid_core;
	struct mlxsw_sp_policer_core *policer_core;
	struct mlxsw_sp_kvdl *kvdl;
	struct mlxsw_sp_nve *nve;
	struct notifier_block netdevice_nb;
	struct mlxsw_sp_ptp_clock *clock;
	struct mlxsw_sp_ptp_state *ptp_state;
	struct mlxsw_sp_counter_pool *counter_pool;
	struct mlxsw_sp_span *span;
	struct mlxsw_sp_trap *trap;
	const struct mlxsw_sp_kvdl_ops *kvdl_ops;
	const struct mlxsw_afa_ops *afa_ops;
	const struct mlxsw_afk_ops *afk_ops;
	const struct mlxsw_sp_mr_tcam_ops *mr_tcam_ops;
	const struct mlxsw_sp_acl_rulei_ops *acl_rulei_ops;
	const struct mlxsw_sp_acl_tcam_ops *acl_tcam_ops;
	const struct mlxsw_sp_nve_ops **nve_ops_arr;
	const struct mlxsw_sp_rif_ops **rif_ops_arr;
	const struct mlxsw_sp_sb_vals *sb_vals;
	const struct mlxsw_sp_sb_ops *sb_ops;
	const struct mlxsw_sp_port_type_speed_ops *port_type_speed_ops;
	const struct mlxsw_sp_ptp_ops *ptp_ops;
	const struct mlxsw_sp_span_ops *span_ops;
	const struct mlxsw_sp_policer_core_ops *policer_core_ops;
	const struct mlxsw_sp_trap_ops *trap_ops;
	const struct mlxsw_sp_mall_ops *mall_ops;
	const struct mlxsw_listener *listeners;
	size_t listeners_count;
	u32 lowest_shaper_bs;
};

struct mlxsw_sp_ptp_ops {
	struct mlxsw_sp_ptp_clock *
		(*clock_init)(struct mlxsw_sp *mlxsw_sp, struct device *dev);
	void (*clock_fini)(struct mlxsw_sp_ptp_clock *clock);

	struct mlxsw_sp_ptp_state *(*init)(struct mlxsw_sp *mlxsw_sp);
	void (*fini)(struct mlxsw_sp_ptp_state *ptp_state);

	/* Notify a driver that a packet that might be PTP was received. Driver
	 * is responsible for freeing the passed-in SKB.
	 */
	void (*receive)(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
			u8 local_port);

	/* Notify a driver that a timestamped packet was transmitted. Driver
	 * is responsible for freeing the passed-in SKB.
	 */
	void (*transmitted)(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
			    u8 local_port);

	int (*hwtstamp_get)(struct mlxsw_sp_port *mlxsw_sp_port,
			    struct hwtstamp_config *config);
	int (*hwtstamp_set)(struct mlxsw_sp_port *mlxsw_sp_port,
			    struct hwtstamp_config *config);
	void (*shaper_work)(struct work_struct *work);
	int (*get_ts_info)(struct mlxsw_sp *mlxsw_sp,
			   struct ethtool_ts_info *info);
	int (*get_stats_count)(void);
	void (*get_stats_strings)(u8 **p);
	void (*get_stats)(struct mlxsw_sp_port *mlxsw_sp_port,
			  u64 *data, int data_index);
};

static inline struct mlxsw_sp_upper *
mlxsw_sp_lag_get(struct mlxsw_sp *mlxsw_sp, u16 lag_id)
{
	return &mlxsw_sp->lags[lag_id];
}

struct mlxsw_sp_port_pcpu_stats {
	u64			rx_packets;
	u64			rx_bytes;
	u64			tx_packets;
	u64			tx_bytes;
	struct u64_stats_sync	syncp;
	u32			tx_dropped;
};

enum mlxsw_sp_sample_trigger_type {
	MLXSW_SP_SAMPLE_TRIGGER_TYPE_INGRESS,
	MLXSW_SP_SAMPLE_TRIGGER_TYPE_EGRESS,
};

struct mlxsw_sp_sample_trigger {
	enum mlxsw_sp_sample_trigger_type type;
	u8 local_port;
};

struct mlxsw_sp_sample_params {
	struct psample_group *psample_group;
	u32 trunc_size;
	u32 rate;
	bool truncate;
};

struct mlxsw_sp_bridge_port;
struct mlxsw_sp_fid;

struct mlxsw_sp_port_vlan {
	struct list_head list;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct mlxsw_sp_fid *fid;
	u16 vid;
	struct mlxsw_sp_bridge_port *bridge_port;
	struct list_head bridge_vlan_node;
};

/* No need an internal lock; At worse - miss a single periodic iteration */
struct mlxsw_sp_port_xstats {
	u64 ecn;
	u64 wred_drop[TC_MAX_QUEUE];
	u64 tail_drop[TC_MAX_QUEUE];
	u64 backlog[TC_MAX_QUEUE];
	u64 tx_bytes[IEEE_8021QAZ_MAX_TCS];
	u64 tx_packets[IEEE_8021QAZ_MAX_TCS];
};

struct mlxsw_sp_ptp_port_dir_stats {
	u64 packets;
	u64 timestamps;
};

struct mlxsw_sp_ptp_port_stats {
	struct mlxsw_sp_ptp_port_dir_stats rx_gcd;
	struct mlxsw_sp_ptp_port_dir_stats tx_gcd;
};

struct mlxsw_sp_port {
	struct net_device *dev;
	struct mlxsw_sp_port_pcpu_stats __percpu *pcpu_stats;
	struct mlxsw_sp *mlxsw_sp;
	u8 local_port;
	u8 lagged:1,
	   split:1;
	u16 pvid;
	u16 lag_id;
	struct {
		u8 tx_pause:1,
		   rx_pause:1,
		   autoneg:1;
	} link;
	struct {
		struct ieee_ets *ets;
		struct ieee_maxrate *maxrate;
		struct ieee_pfc *pfc;
		enum mlxsw_reg_qpts_trust_state trust_state;
	} dcb;
	struct mlxsw_sp_port_mapping mapping; /* mapping is constant during the
					       * mlxsw_sp_port lifetime, however
					       * the same localport can have
					       * different mapping.
					       */
	struct {
		#define MLXSW_HW_STATS_UPDATE_TIME HZ
		struct rtnl_link_stats64 stats;
		struct mlxsw_sp_port_xstats xstats;
		struct delayed_work update_dw;
	} periodic_hw_stats;
	struct list_head vlans_list;
	struct mlxsw_sp_port_vlan *default_vlan;
	struct mlxsw_sp_qdisc_state *qdisc;
	unsigned acl_rule_count;
	struct mlxsw_sp_flow_block *ing_flow_block;
	struct mlxsw_sp_flow_block *eg_flow_block;
	struct {
		struct delayed_work shaper_dw;
		struct hwtstamp_config hwtstamp_config;
		u16 ing_types;
		u16 egr_types;
		struct mlxsw_sp_ptp_port_stats stats;
	} ptp;
	u8 split_base_local_port;
	int max_mtu;
	u32 max_speed;
	struct mlxsw_sp_hdroom *hdroom;
	u64 module_overheat_initial_val;
};

struct mlxsw_sp_port_type_speed_ops {
	void (*from_ptys_supported_port)(struct mlxsw_sp *mlxsw_sp,
					 u32 ptys_eth_proto,
					 struct ethtool_link_ksettings *cmd);
	void (*from_ptys_link)(struct mlxsw_sp *mlxsw_sp, u32 ptys_eth_proto,
			       unsigned long *mode);
	u32 (*from_ptys_speed)(struct mlxsw_sp *mlxsw_sp, u32 ptys_eth_proto);
	void (*from_ptys_link_mode)(struct mlxsw_sp *mlxsw_sp,
				    bool carrier_ok, u32 ptys_eth_proto,
				    struct ethtool_link_ksettings *cmd);
	int (*ptys_max_speed)(struct mlxsw_sp_port *mlxsw_sp_port, u32 *p_max_speed);
	u32 (*to_ptys_advert_link)(struct mlxsw_sp *mlxsw_sp,
				   const struct ethtool_link_ksettings *cmd);
	u32 (*to_ptys_speed_lanes)(struct mlxsw_sp *mlxsw_sp, u8 width,
				   const struct ethtool_link_ksettings *cmd);
	void (*reg_ptys_eth_pack)(struct mlxsw_sp *mlxsw_sp, char *payload,
				  u8 local_port, u32 proto_admin, bool autoneg);
	void (*reg_ptys_eth_unpack)(struct mlxsw_sp *mlxsw_sp, char *payload,
				    u32 *p_eth_proto_cap,
				    u32 *p_eth_proto_admin,
				    u32 *p_eth_proto_oper);
	u32 (*ptys_proto_cap_masked_get)(u32 eth_proto_cap);
};

static inline struct net_device *
mlxsw_sp_bridge_vxlan_dev_find(struct net_device *br_dev)
{
	struct net_device *dev;
	struct list_head *iter;

	netdev_for_each_lower_dev(br_dev, dev, iter) {
		if (netif_is_vxlan(dev))
			return dev;
	}

	return NULL;
}

static inline bool mlxsw_sp_bridge_has_vxlan(struct net_device *br_dev)
{
	return !!mlxsw_sp_bridge_vxlan_dev_find(br_dev);
}

static inline int
mlxsw_sp_vxlan_mapped_vid(const struct net_device *vxlan_dev, u16 *p_vid)
{
	struct bridge_vlan_info vinfo;
	u16 vid = 0;
	int err;

	err = br_vlan_get_pvid(vxlan_dev, &vid);
	if (err || !vid)
		goto out;

	err = br_vlan_get_info(vxlan_dev, vid, &vinfo);
	if (err || !(vinfo.flags & BRIDGE_VLAN_INFO_UNTAGGED))
		vid = 0;

out:
	*p_vid = vid;
	return err;
}

static inline bool
mlxsw_sp_port_is_pause_en(const struct mlxsw_sp_port *mlxsw_sp_port)
{
	return mlxsw_sp_port->link.tx_pause || mlxsw_sp_port->link.rx_pause;
}

static inline struct mlxsw_sp_port *
mlxsw_sp_port_lagged_get(struct mlxsw_sp *mlxsw_sp, u16 lag_id, u8 port_index)
{
	struct mlxsw_sp_port *mlxsw_sp_port;
	u8 local_port;

	local_port = mlxsw_core_lag_mapping_get(mlxsw_sp->core,
						lag_id, port_index);
	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	return mlxsw_sp_port && mlxsw_sp_port->lagged ? mlxsw_sp_port : NULL;
}

static inline struct mlxsw_sp_port_vlan *
mlxsw_sp_port_vlan_find_by_vid(const struct mlxsw_sp_port *mlxsw_sp_port,
			       u16 vid)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	list_for_each_entry(mlxsw_sp_port_vlan, &mlxsw_sp_port->vlans_list,
			    list) {
		if (mlxsw_sp_port_vlan->vid == vid)
			return mlxsw_sp_port_vlan;
	}

	return NULL;
}

enum mlxsw_sp_flood_type {
	MLXSW_SP_FLOOD_TYPE_UC,
	MLXSW_SP_FLOOD_TYPE_BC,
	MLXSW_SP_FLOOD_TYPE_MC,
};

int mlxsw_sp_port_get_stats_raw(struct net_device *dev, int grp,
				int prio, char *ppcnt_pl);
int mlxsw_sp_port_admin_status_set(struct mlxsw_sp_port *mlxsw_sp_port,
				   bool is_up);
int
mlxsw_sp_port_vlan_classification_set(struct mlxsw_sp_port *mlxsw_sp_port,
				      bool is_8021ad_tagged,
				      bool is_8021q_tagged);

/* spectrum_buffers.c */
struct mlxsw_sp_hdroom_prio {
	/* Number of port buffer associated with this priority. This is the
	 * actually configured value.
	 */
	u8 buf_idx;
	/* Value of buf_idx deduced from the DCB ETS configuration. */
	u8 ets_buf_idx;
	/* Value of buf_idx taken from the dcbnl_setbuffer configuration. */
	u8 set_buf_idx;
	bool lossy;
};

struct mlxsw_sp_hdroom_buf {
	u32 thres_cells;
	u32 size_cells;
	/* Size requirement form dcbnl_setbuffer. */
	u32 set_size_cells;
	bool lossy;
};

enum mlxsw_sp_hdroom_mode {
	MLXSW_SP_HDROOM_MODE_DCB,
	MLXSW_SP_HDROOM_MODE_TC,
};

#define MLXSW_SP_PB_COUNT 10

struct mlxsw_sp_hdroom {
	enum mlxsw_sp_hdroom_mode mode;

	struct {
		struct mlxsw_sp_hdroom_prio prio[IEEE_8021Q_MAX_PRIORITIES];
	} prios;
	struct {
		struct mlxsw_sp_hdroom_buf buf[MLXSW_SP_PB_COUNT];
	} bufs;
	struct {
		/* Size actually configured for the internal buffer. Equal to
		 * reserve when internal buffer is enabled.
		 */
		u32 size_cells;
		/* Space reserved in the headroom for the internal buffer. Port
		 * buffers are not allowed to grow into this space.
		 */
		u32 reserve_cells;
		bool enable;
	} int_buf;
	int delay_bytes;
	int mtu;
};

int mlxsw_sp_buffers_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_buffers_fini(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_port_buffers_init(struct mlxsw_sp_port *mlxsw_sp_port);
void mlxsw_sp_port_buffers_fini(struct mlxsw_sp_port *mlxsw_sp_port);
int mlxsw_sp_sb_pool_get(struct mlxsw_core *mlxsw_core,
			 unsigned int sb_index, u16 pool_index,
			 struct devlink_sb_pool_info *pool_info);
int mlxsw_sp_sb_pool_set(struct mlxsw_core *mlxsw_core,
			 unsigned int sb_index, u16 pool_index, u32 size,
			 enum devlink_sb_threshold_type threshold_type,
			 struct netlink_ext_ack *extack);
int mlxsw_sp_sb_port_pool_get(struct mlxsw_core_port *mlxsw_core_port,
			      unsigned int sb_index, u16 pool_index,
			      u32 *p_threshold);
int mlxsw_sp_sb_port_pool_set(struct mlxsw_core_port *mlxsw_core_port,
			      unsigned int sb_index, u16 pool_index,
			      u32 threshold, struct netlink_ext_ack *extack);
int mlxsw_sp_sb_tc_pool_bind_get(struct mlxsw_core_port *mlxsw_core_port,
				 unsigned int sb_index, u16 tc_index,
				 enum devlink_sb_pool_type pool_type,
				 u16 *p_pool_index, u32 *p_threshold);
int mlxsw_sp_sb_tc_pool_bind_set(struct mlxsw_core_port *mlxsw_core_port,
				 unsigned int sb_index, u16 tc_index,
				 enum devlink_sb_pool_type pool_type,
				 u16 pool_index, u32 threshold,
				 struct netlink_ext_ack *extack);
int mlxsw_sp_sb_occ_snapshot(struct mlxsw_core *mlxsw_core,
			     unsigned int sb_index);
int mlxsw_sp_sb_occ_max_clear(struct mlxsw_core *mlxsw_core,
			      unsigned int sb_index);
int mlxsw_sp_sb_occ_port_pool_get(struct mlxsw_core_port *mlxsw_core_port,
				  unsigned int sb_index, u16 pool_index,
				  u32 *p_cur, u32 *p_max);
int mlxsw_sp_sb_occ_tc_port_bind_get(struct mlxsw_core_port *mlxsw_core_port,
				     unsigned int sb_index, u16 tc_index,
				     enum devlink_sb_pool_type pool_type,
				     u32 *p_cur, u32 *p_max);
u32 mlxsw_sp_cells_bytes(const struct mlxsw_sp *mlxsw_sp, u32 cells);
u32 mlxsw_sp_bytes_cells(const struct mlxsw_sp *mlxsw_sp, u32 bytes);
void mlxsw_sp_hdroom_prios_reset_buf_idx(struct mlxsw_sp_hdroom *hdroom);
void mlxsw_sp_hdroom_bufs_reset_lossiness(struct mlxsw_sp_hdroom *hdroom);
void mlxsw_sp_hdroom_bufs_reset_sizes(struct mlxsw_sp_port *mlxsw_sp_port,
				      struct mlxsw_sp_hdroom *hdroom);
int mlxsw_sp_hdroom_configure(struct mlxsw_sp_port *mlxsw_sp_port,
			      const struct mlxsw_sp_hdroom *hdroom);
struct mlxsw_sp_sample_params *
mlxsw_sp_sample_trigger_params_lookup(struct mlxsw_sp *mlxsw_sp,
				      const struct mlxsw_sp_sample_trigger *trigger);
int
mlxsw_sp_sample_trigger_params_set(struct mlxsw_sp *mlxsw_sp,
				   const struct mlxsw_sp_sample_trigger *trigger,
				   const struct mlxsw_sp_sample_params *params,
				   struct netlink_ext_ack *extack);
void
mlxsw_sp_sample_trigger_params_unset(struct mlxsw_sp *mlxsw_sp,
				     const struct mlxsw_sp_sample_trigger *trigger);

extern const struct mlxsw_sp_sb_vals mlxsw_sp1_sb_vals;
extern const struct mlxsw_sp_sb_vals mlxsw_sp2_sb_vals;

extern const struct mlxsw_sp_sb_ops mlxsw_sp1_sb_ops;
extern const struct mlxsw_sp_sb_ops mlxsw_sp2_sb_ops;
extern const struct mlxsw_sp_sb_ops mlxsw_sp3_sb_ops;

/* spectrum_switchdev.c */
int mlxsw_sp_switchdev_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_switchdev_fini(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_rif_fdb_op(struct mlxsw_sp *mlxsw_sp, const char *mac, u16 fid,
			bool adding);
void
mlxsw_sp_port_vlan_bridge_leave(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan);
int mlxsw_sp_port_bridge_join(struct mlxsw_sp_port *mlxsw_sp_port,
			      struct net_device *brport_dev,
			      struct net_device *br_dev,
			      struct netlink_ext_ack *extack);
void mlxsw_sp_port_bridge_leave(struct mlxsw_sp_port *mlxsw_sp_port,
				struct net_device *brport_dev,
				struct net_device *br_dev);
bool mlxsw_sp_bridge_device_is_offloaded(const struct mlxsw_sp *mlxsw_sp,
					 const struct net_device *br_dev);
int mlxsw_sp_bridge_vxlan_join(struct mlxsw_sp *mlxsw_sp,
			       const struct net_device *br_dev,
			       const struct net_device *vxlan_dev, u16 vid,
			       struct netlink_ext_ack *extack);
void mlxsw_sp_bridge_vxlan_leave(struct mlxsw_sp *mlxsw_sp,
				 const struct net_device *vxlan_dev);
extern struct notifier_block mlxsw_sp_switchdev_notifier;

/* spectrum.c */
void mlxsw_sp_rx_listener_no_mark_func(struct sk_buff *skb,
				       u8 local_port, void *priv);
void mlxsw_sp_ptp_receive(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
			  u8 local_port);
int mlxsw_sp_port_speed_get(struct mlxsw_sp_port *mlxsw_sp_port, u32 *speed);
int mlxsw_sp_port_ets_set(struct mlxsw_sp_port *mlxsw_sp_port,
			  enum mlxsw_reg_qeec_hr hr, u8 index, u8 next_index,
			  bool dwrr, u8 dwrr_weight);
int mlxsw_sp_port_prio_tc_set(struct mlxsw_sp_port *mlxsw_sp_port,
			      u8 switch_prio, u8 tclass);
int mlxsw_sp_port_ets_maxrate_set(struct mlxsw_sp_port *mlxsw_sp_port,
				  enum mlxsw_reg_qeec_hr hr, u8 index,
				  u8 next_index, u32 maxrate, u8 burst_size);
enum mlxsw_reg_spms_state mlxsw_sp_stp_spms_state(u8 stp_state);
int mlxsw_sp_port_vid_stp_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid,
			      u8 state);
int mlxsw_sp_port_vp_mode_set(struct mlxsw_sp_port *mlxsw_sp_port, bool enable);
int mlxsw_sp_port_vid_learning_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid,
				   bool learn_enable);
int mlxsw_sp_ethtype_to_sver_type(u16 ethtype, u8 *p_sver_type);
int mlxsw_sp_port_pvid_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid,
			   u16 ethtype);
struct mlxsw_sp_port_vlan *
mlxsw_sp_port_vlan_create(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid);
void mlxsw_sp_port_vlan_destroy(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan);
int mlxsw_sp_port_vlan_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid_begin,
			   u16 vid_end, bool is_member, bool untagged);
int mlxsw_sp_flow_counter_get(struct mlxsw_sp *mlxsw_sp,
			      unsigned int counter_index, u64 *packets,
			      u64 *bytes);
int mlxsw_sp_flow_counter_alloc(struct mlxsw_sp *mlxsw_sp,
				unsigned int *p_counter_index);
void mlxsw_sp_flow_counter_free(struct mlxsw_sp *mlxsw_sp,
				unsigned int counter_index);
bool mlxsw_sp_port_dev_check(const struct net_device *dev);
struct mlxsw_sp *mlxsw_sp_lower_get(struct net_device *dev);
struct mlxsw_sp_port *mlxsw_sp_port_dev_lower_find(struct net_device *dev);
struct mlxsw_sp_port *mlxsw_sp_port_lower_dev_hold(struct net_device *dev);
void mlxsw_sp_port_dev_put(struct mlxsw_sp_port *mlxsw_sp_port);
struct mlxsw_sp_port *mlxsw_sp_port_dev_lower_find_rcu(struct net_device *dev);

/* spectrum_dcb.c */
#ifdef CONFIG_MLXSW_SPECTRUM_DCB
int mlxsw_sp_port_dcb_init(struct mlxsw_sp_port *mlxsw_sp_port);
void mlxsw_sp_port_dcb_fini(struct mlxsw_sp_port *mlxsw_sp_port);
#else
static inline int mlxsw_sp_port_dcb_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	return 0;
}
static inline void mlxsw_sp_port_dcb_fini(struct mlxsw_sp_port *mlxsw_sp_port)
{}
#endif

/* spectrum_router.c */
enum mlxsw_sp_l3proto {
	MLXSW_SP_L3_PROTO_IPV4,
	MLXSW_SP_L3_PROTO_IPV6,
#define MLXSW_SP_L3_PROTO_MAX	(MLXSW_SP_L3_PROTO_IPV6 + 1)
};

union mlxsw_sp_l3addr {
	__be32 addr4;
	struct in6_addr addr6;
};

int mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp,
			 struct netlink_ext_ack *extack);
void mlxsw_sp_router_fini(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_netdevice_router_port_event(struct net_device *dev,
					 unsigned long event, void *ptr);
void mlxsw_sp_rif_macvlan_del(struct mlxsw_sp *mlxsw_sp,
			      const struct net_device *macvlan_dev);
int mlxsw_sp_inetaddr_valid_event(struct notifier_block *unused,
				  unsigned long event, void *ptr);
int mlxsw_sp_inet6addr_valid_event(struct notifier_block *unused,
				   unsigned long event, void *ptr);
int mlxsw_sp_netdevice_vrf_event(struct net_device *l3_dev, unsigned long event,
				 struct netdev_notifier_changeupper_info *info);
bool mlxsw_sp_netdev_is_ipip_ol(const struct mlxsw_sp *mlxsw_sp,
				const struct net_device *dev);
bool mlxsw_sp_netdev_is_ipip_ul(struct mlxsw_sp *mlxsw_sp,
				const struct net_device *dev);
int mlxsw_sp_netdevice_ipip_ol_event(struct mlxsw_sp *mlxsw_sp,
				     struct net_device *l3_dev,
				     unsigned long event,
				     struct netdev_notifier_info *info);
int
mlxsw_sp_netdevice_ipip_ul_event(struct mlxsw_sp *mlxsw_sp,
				 struct net_device *l3_dev,
				 unsigned long event,
				 struct netdev_notifier_info *info);
int
mlxsw_sp_port_vlan_router_join(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan,
			       struct net_device *l3_dev,
			       struct netlink_ext_ack *extack);
void
mlxsw_sp_port_vlan_router_leave(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan);
void mlxsw_sp_rif_destroy_by_dev(struct mlxsw_sp *mlxsw_sp,
				 struct net_device *dev);
bool mlxsw_sp_rif_exists(struct mlxsw_sp *mlxsw_sp,
			 const struct net_device *dev);
u16 mlxsw_sp_rif_vid(struct mlxsw_sp *mlxsw_sp, const struct net_device *dev);
u8 mlxsw_sp_router_port(const struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_router_nve_promote_decap(struct mlxsw_sp *mlxsw_sp, u32 ul_tb_id,
				      enum mlxsw_sp_l3proto ul_proto,
				      const union mlxsw_sp_l3addr *ul_sip,
				      u32 tunnel_index);
void mlxsw_sp_router_nve_demote_decap(struct mlxsw_sp *mlxsw_sp, u32 ul_tb_id,
				      enum mlxsw_sp_l3proto ul_proto,
				      const union mlxsw_sp_l3addr *ul_sip);
int mlxsw_sp_router_tb_id_vr_id(struct mlxsw_sp *mlxsw_sp, u32 tb_id,
				u16 *vr_id);
int mlxsw_sp_router_ul_rif_get(struct mlxsw_sp *mlxsw_sp, u32 ul_tb_id,
			       u16 *ul_rif_index);
void mlxsw_sp_router_ul_rif_put(struct mlxsw_sp *mlxsw_sp, u16 ul_rif_index);

/* spectrum_kvdl.c */
enum mlxsw_sp_kvdl_entry_type {
	MLXSW_SP_KVDL_ENTRY_TYPE_ADJ,
	MLXSW_SP_KVDL_ENTRY_TYPE_ACTSET,
	MLXSW_SP_KVDL_ENTRY_TYPE_PBS,
	MLXSW_SP_KVDL_ENTRY_TYPE_MCRIGR,
	MLXSW_SP_KVDL_ENTRY_TYPE_TNUMT,
};

static inline unsigned int
mlxsw_sp_kvdl_entry_size(enum mlxsw_sp_kvdl_entry_type type)
{
	switch (type) {
	case MLXSW_SP_KVDL_ENTRY_TYPE_ADJ:
	case MLXSW_SP_KVDL_ENTRY_TYPE_ACTSET:
	case MLXSW_SP_KVDL_ENTRY_TYPE_PBS:
	case MLXSW_SP_KVDL_ENTRY_TYPE_MCRIGR:
	case MLXSW_SP_KVDL_ENTRY_TYPE_TNUMT:
	default:
		return 1;
	}
}

struct mlxsw_sp_kvdl_ops {
	size_t priv_size;
	int (*init)(struct mlxsw_sp *mlxsw_sp, void *priv);
	void (*fini)(struct mlxsw_sp *mlxsw_sp, void *priv);
	int (*alloc)(struct mlxsw_sp *mlxsw_sp, void *priv,
		     enum mlxsw_sp_kvdl_entry_type type,
		     unsigned int entry_count, u32 *p_entry_index);
	void (*free)(struct mlxsw_sp *mlxsw_sp, void *priv,
		     enum mlxsw_sp_kvdl_entry_type type,
		     unsigned int entry_count, int entry_index);
	int (*alloc_size_query)(struct mlxsw_sp *mlxsw_sp, void *priv,
				enum mlxsw_sp_kvdl_entry_type type,
				unsigned int entry_count,
				unsigned int *p_alloc_count);
	int (*resources_register)(struct mlxsw_sp *mlxsw_sp, void *priv);
};

int mlxsw_sp_kvdl_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_kvdl_fini(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_kvdl_alloc(struct mlxsw_sp *mlxsw_sp,
			enum mlxsw_sp_kvdl_entry_type type,
			unsigned int entry_count, u32 *p_entry_index);
void mlxsw_sp_kvdl_free(struct mlxsw_sp *mlxsw_sp,
			enum mlxsw_sp_kvdl_entry_type type,
			unsigned int entry_count, int entry_index);
int mlxsw_sp_kvdl_alloc_count_query(struct mlxsw_sp *mlxsw_sp,
				    enum mlxsw_sp_kvdl_entry_type type,
				    unsigned int entry_count,
				    unsigned int *p_alloc_count);

/* spectrum1_kvdl.c */
extern const struct mlxsw_sp_kvdl_ops mlxsw_sp1_kvdl_ops;
int mlxsw_sp1_kvdl_resources_register(struct mlxsw_core *mlxsw_core);

/* spectrum2_kvdl.c */
extern const struct mlxsw_sp_kvdl_ops mlxsw_sp2_kvdl_ops;

struct mlxsw_sp_acl_rule_info {
	unsigned int priority;
	struct mlxsw_afk_element_values values;
	struct mlxsw_afa_block *act_block;
	u8 action_created:1,
	   ingress_bind_blocker:1,
	   egress_bind_blocker:1,
	   counter_valid:1,
	   policer_index_valid:1;
	unsigned int counter_index;
	u16 policer_index;
};

/* spectrum_flow.c */
struct mlxsw_sp_flow_block {
	struct list_head binding_list;
	struct {
		struct list_head list;
		unsigned int min_prio;
		unsigned int max_prio;
	} mall;
	struct mlxsw_sp_acl_ruleset *ruleset_zero;
	struct mlxsw_sp *mlxsw_sp;
	unsigned int rule_count;
	unsigned int disable_count;
	unsigned int ingress_blocker_rule_count;
	unsigned int egress_blocker_rule_count;
	unsigned int ingress_binding_count;
	unsigned int egress_binding_count;
	struct net *net;
};

struct mlxsw_sp_flow_block_binding {
	struct list_head list;
	struct mlxsw_sp_port *mlxsw_sp_port;
	bool ingress;
};

static inline struct mlxsw_sp *
mlxsw_sp_flow_block_mlxsw_sp(struct mlxsw_sp_flow_block *block)
{
	return block->mlxsw_sp;
}

static inline unsigned int
mlxsw_sp_flow_block_rule_count(const struct mlxsw_sp_flow_block *block)
{
	return block ? block->rule_count : 0;
}

static inline void
mlxsw_sp_flow_block_disable_inc(struct mlxsw_sp_flow_block *block)
{
	if (block)
		block->disable_count++;
}

static inline void
mlxsw_sp_flow_block_disable_dec(struct mlxsw_sp_flow_block *block)
{
	if (block)
		block->disable_count--;
}

static inline bool
mlxsw_sp_flow_block_disabled(const struct mlxsw_sp_flow_block *block)
{
	return block->disable_count;
}

static inline bool
mlxsw_sp_flow_block_is_egress_bound(const struct mlxsw_sp_flow_block *block)
{
	return block->egress_binding_count;
}

static inline bool
mlxsw_sp_flow_block_is_ingress_bound(const struct mlxsw_sp_flow_block *block)
{
	return block->ingress_binding_count;
}

static inline bool
mlxsw_sp_flow_block_is_mixed_bound(const struct mlxsw_sp_flow_block *block)
{
	return block->ingress_binding_count && block->egress_binding_count;
}

struct mlxsw_sp_flow_block *mlxsw_sp_flow_block_create(struct mlxsw_sp *mlxsw_sp,
						       struct net *net);
void mlxsw_sp_flow_block_destroy(struct mlxsw_sp_flow_block *block);
int mlxsw_sp_setup_tc_block_clsact(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct flow_block_offload *f,
				   bool ingress);

/* spectrum_acl.c */
struct mlxsw_sp_acl_ruleset;

enum mlxsw_sp_acl_profile {
	MLXSW_SP_ACL_PROFILE_FLOWER,
	MLXSW_SP_ACL_PROFILE_MR,
};

struct mlxsw_afk *mlxsw_sp_acl_afk(struct mlxsw_sp_acl *acl);

int mlxsw_sp_acl_ruleset_bind(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_flow_block *block,
			      struct mlxsw_sp_flow_block_binding *binding);
void mlxsw_sp_acl_ruleset_unbind(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_flow_block *block,
				 struct mlxsw_sp_flow_block_binding *binding);
struct mlxsw_sp_acl_ruleset *
mlxsw_sp_acl_ruleset_lookup(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_flow_block *block, u32 chain_index,
			    enum mlxsw_sp_acl_profile profile);
struct mlxsw_sp_acl_ruleset *
mlxsw_sp_acl_ruleset_get(struct mlxsw_sp *mlxsw_sp,
			 struct mlxsw_sp_flow_block *block, u32 chain_index,
			 enum mlxsw_sp_acl_profile profile,
			 struct mlxsw_afk_element_usage *tmplt_elusage);
void mlxsw_sp_acl_ruleset_put(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_acl_ruleset *ruleset);
u16 mlxsw_sp_acl_ruleset_group_id(struct mlxsw_sp_acl_ruleset *ruleset);
void mlxsw_sp_acl_ruleset_prio_get(struct mlxsw_sp_acl_ruleset *ruleset,
				   unsigned int *p_min_prio,
				   unsigned int *p_max_prio);

struct mlxsw_sp_acl_rule_info *
mlxsw_sp_acl_rulei_create(struct mlxsw_sp_acl *acl,
			  struct mlxsw_afa_block *afa_block);
void mlxsw_sp_acl_rulei_destroy(struct mlxsw_sp_acl_rule_info *rulei);
int mlxsw_sp_acl_rulei_commit(struct mlxsw_sp_acl_rule_info *rulei);
void mlxsw_sp_acl_rulei_priority(struct mlxsw_sp_acl_rule_info *rulei,
				 unsigned int priority);
void mlxsw_sp_acl_rulei_keymask_u32(struct mlxsw_sp_acl_rule_info *rulei,
				    enum mlxsw_afk_element element,
				    u32 key_value, u32 mask_value);
void mlxsw_sp_acl_rulei_keymask_buf(struct mlxsw_sp_acl_rule_info *rulei,
				    enum mlxsw_afk_element element,
				    const char *key_value,
				    const char *mask_value, unsigned int len);
int mlxsw_sp_acl_rulei_act_continue(struct mlxsw_sp_acl_rule_info *rulei);
int mlxsw_sp_acl_rulei_act_jump(struct mlxsw_sp_acl_rule_info *rulei,
				u16 group_id);
int mlxsw_sp_acl_rulei_act_terminate(struct mlxsw_sp_acl_rule_info *rulei);
int mlxsw_sp_acl_rulei_act_drop(struct mlxsw_sp_acl_rule_info *rulei,
				bool ingress,
				const struct flow_action_cookie *fa_cookie,
				struct netlink_ext_ack *extack);
int mlxsw_sp_acl_rulei_act_trap(struct mlxsw_sp_acl_rule_info *rulei);
int mlxsw_sp_acl_rulei_act_mirror(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_rule_info *rulei,
				  struct mlxsw_sp_flow_block *block,
				  struct net_device *out_dev,
				  struct netlink_ext_ack *extack);
int mlxsw_sp_acl_rulei_act_fwd(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_rule_info *rulei,
			       struct net_device *out_dev,
			       struct netlink_ext_ack *extack);
int mlxsw_sp_acl_rulei_act_vlan(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_rule_info *rulei,
				u32 action, u16 vid, u16 proto, u8 prio,
				struct netlink_ext_ack *extack);
int mlxsw_sp_acl_rulei_act_priority(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_acl_rule_info *rulei,
				    u32 prio, struct netlink_ext_ack *extack);
int mlxsw_sp_acl_rulei_act_mangle(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_rule_info *rulei,
				  enum flow_action_mangle_base htype,
				  u32 offset, u32 mask, u32 val,
				  struct netlink_ext_ack *extack);
int mlxsw_sp_acl_rulei_act_police(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_rule_info *rulei,
				  u32 index, u64 rate_bytes_ps,
				  u32 burst, struct netlink_ext_ack *extack);
int mlxsw_sp_acl_rulei_act_count(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_rule_info *rulei,
				 struct netlink_ext_ack *extack);
int mlxsw_sp_acl_rulei_act_fid_set(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_acl_rule_info *rulei,
				   u16 fid, struct netlink_ext_ack *extack);

struct mlxsw_sp_acl_rule;

struct mlxsw_sp_acl_rule *
mlxsw_sp_acl_rule_create(struct mlxsw_sp *mlxsw_sp,
			 struct mlxsw_sp_acl_ruleset *ruleset,
			 unsigned long cookie,
			 struct mlxsw_afa_block *afa_block,
			 struct netlink_ext_ack *extack);
void mlxsw_sp_acl_rule_destroy(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_rule *rule);
int mlxsw_sp_acl_rule_add(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_acl_rule *rule);
void mlxsw_sp_acl_rule_del(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_acl_rule *rule);
int mlxsw_sp_acl_rule_action_replace(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_acl_rule *rule,
				     struct mlxsw_afa_block *afa_block);
struct mlxsw_sp_acl_rule *
mlxsw_sp_acl_rule_lookup(struct mlxsw_sp *mlxsw_sp,
			 struct mlxsw_sp_acl_ruleset *ruleset,
			 unsigned long cookie);
struct mlxsw_sp_acl_rule_info *
mlxsw_sp_acl_rule_rulei(struct mlxsw_sp_acl_rule *rule);
int mlxsw_sp_acl_rule_get_stats(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_rule *rule,
				u64 *packets, u64 *bytes, u64 *drops,
				u64 *last_use,
				enum flow_action_hw_stats *used_hw_stats);

struct mlxsw_sp_fid *mlxsw_sp_acl_dummy_fid(struct mlxsw_sp *mlxsw_sp);

static inline const struct flow_action_cookie *
mlxsw_sp_acl_act_cookie_lookup(struct mlxsw_sp *mlxsw_sp, u32 cookie_index)
{
	return mlxsw_afa_cookie_lookup(mlxsw_sp->afa, cookie_index);
}

int mlxsw_sp_acl_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_acl_fini(struct mlxsw_sp *mlxsw_sp);
u32 mlxsw_sp_acl_region_rehash_intrvl_get(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_acl_region_rehash_intrvl_set(struct mlxsw_sp *mlxsw_sp, u32 val);

struct mlxsw_sp_acl_mangle_action;

struct mlxsw_sp_acl_rulei_ops {
	int (*act_mangle_field)(struct mlxsw_sp *mlxsw_sp, struct mlxsw_sp_acl_rule_info *rulei,
				struct mlxsw_sp_acl_mangle_action *mact, u32 val,
				struct netlink_ext_ack *extack);
};

extern struct mlxsw_sp_acl_rulei_ops mlxsw_sp1_acl_rulei_ops;
extern struct mlxsw_sp_acl_rulei_ops mlxsw_sp2_acl_rulei_ops;

/* spectrum_acl_tcam.c */
struct mlxsw_sp_acl_tcam;
struct mlxsw_sp_acl_tcam_region;

struct mlxsw_sp_acl_tcam_ops {
	enum mlxsw_reg_ptar_key_type key_type;
	size_t priv_size;
	int (*init)(struct mlxsw_sp *mlxsw_sp, void *priv,
		    struct mlxsw_sp_acl_tcam *tcam);
	void (*fini)(struct mlxsw_sp *mlxsw_sp, void *priv);
	size_t region_priv_size;
	int (*region_init)(struct mlxsw_sp *mlxsw_sp, void *region_priv,
			   void *tcam_priv,
			   struct mlxsw_sp_acl_tcam_region *region,
			   void *hints_priv);
	void (*region_fini)(struct mlxsw_sp *mlxsw_sp, void *region_priv);
	int (*region_associate)(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_tcam_region *region);
	void * (*region_rehash_hints_get)(void *region_priv);
	void (*region_rehash_hints_put)(void *hints_priv);
	size_t chunk_priv_size;
	void (*chunk_init)(void *region_priv, void *chunk_priv,
			   unsigned int priority);
	void (*chunk_fini)(void *chunk_priv);
	size_t entry_priv_size;
	int (*entry_add)(struct mlxsw_sp *mlxsw_sp,
			 void *region_priv, void *chunk_priv,
			 void *entry_priv,
			 struct mlxsw_sp_acl_rule_info *rulei);
	void (*entry_del)(struct mlxsw_sp *mlxsw_sp,
			  void *region_priv, void *chunk_priv,
			  void *entry_priv);
	int (*entry_action_replace)(struct mlxsw_sp *mlxsw_sp,
				    void *region_priv, void *entry_priv,
				    struct mlxsw_sp_acl_rule_info *rulei);
	int (*entry_activity_get)(struct mlxsw_sp *mlxsw_sp,
				  void *region_priv, void *entry_priv,
				  bool *activity);
};

/* spectrum1_acl_tcam.c */
extern const struct mlxsw_sp_acl_tcam_ops mlxsw_sp1_acl_tcam_ops;

/* spectrum2_acl_tcam.c */
extern const struct mlxsw_sp_acl_tcam_ops mlxsw_sp2_acl_tcam_ops;

/* spectrum_acl_flex_actions.c */
extern const struct mlxsw_afa_ops mlxsw_sp1_act_afa_ops;
extern const struct mlxsw_afa_ops mlxsw_sp2_act_afa_ops;

/* spectrum_acl_flex_keys.c */
extern const struct mlxsw_afk_ops mlxsw_sp1_afk_ops;
extern const struct mlxsw_afk_ops mlxsw_sp2_afk_ops;

/* spectrum_matchall.c */
struct mlxsw_sp_mall_ops {
	int (*sample_add)(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_port *mlxsw_sp_port,
			  struct mlxsw_sp_mall_entry *mall_entry,
			  struct netlink_ext_ack *extack);
	void (*sample_del)(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_port *mlxsw_sp_port,
			   struct mlxsw_sp_mall_entry *mall_entry);
};

extern const struct mlxsw_sp_mall_ops mlxsw_sp1_mall_ops;
extern const struct mlxsw_sp_mall_ops mlxsw_sp2_mall_ops;

enum mlxsw_sp_mall_action_type {
	MLXSW_SP_MALL_ACTION_TYPE_MIRROR,
	MLXSW_SP_MALL_ACTION_TYPE_SAMPLE,
	MLXSW_SP_MALL_ACTION_TYPE_TRAP,
};

struct mlxsw_sp_mall_mirror_entry {
	const struct net_device *to_dev;
	int span_id;
};

struct mlxsw_sp_mall_trap_entry {
	int span_id;
};

struct mlxsw_sp_mall_sample_entry {
	struct mlxsw_sp_sample_params params;
	int span_id;	/* Relevant for Spectrum-2 onwards. */
};

struct mlxsw_sp_mall_entry {
	struct list_head list;
	unsigned long cookie;
	unsigned int priority;
	enum mlxsw_sp_mall_action_type type;
	bool ingress;
	union {
		struct mlxsw_sp_mall_mirror_entry mirror;
		struct mlxsw_sp_mall_trap_entry trap;
		struct mlxsw_sp_mall_sample_entry sample;
	};
	struct rcu_head rcu;
};

int mlxsw_sp_mall_replace(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_flow_block *block,
			  struct tc_cls_matchall_offload *f);
void mlxsw_sp_mall_destroy(struct mlxsw_sp_flow_block *block,
			   struct tc_cls_matchall_offload *f);
int mlxsw_sp_mall_port_bind(struct mlxsw_sp_flow_block *block,
			    struct mlxsw_sp_port *mlxsw_sp_port,
			    struct netlink_ext_ack *extack);
void mlxsw_sp_mall_port_unbind(struct mlxsw_sp_flow_block *block,
			       struct mlxsw_sp_port *mlxsw_sp_port);
int mlxsw_sp_mall_prio_get(struct mlxsw_sp_flow_block *block, u32 chain_index,
			   unsigned int *p_min_prio, unsigned int *p_max_prio);

/* spectrum_flower.c */
int mlxsw_sp_flower_replace(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_flow_block *block,
			    struct flow_cls_offload *f);
void mlxsw_sp_flower_destroy(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_flow_block *block,
			     struct flow_cls_offload *f);
int mlxsw_sp_flower_stats(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_flow_block *block,
			  struct flow_cls_offload *f);
int mlxsw_sp_flower_tmplt_create(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_flow_block *block,
				 struct flow_cls_offload *f);
void mlxsw_sp_flower_tmplt_destroy(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_flow_block *block,
				   struct flow_cls_offload *f);
int mlxsw_sp_flower_prio_get(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_flow_block *block,
			     u32 chain_index, unsigned int *p_min_prio,
			     unsigned int *p_max_prio);

/* spectrum_qdisc.c */
int mlxsw_sp_tc_qdisc_init(struct mlxsw_sp_port *mlxsw_sp_port);
void mlxsw_sp_tc_qdisc_fini(struct mlxsw_sp_port *mlxsw_sp_port);
int mlxsw_sp_setup_tc_red(struct mlxsw_sp_port *mlxsw_sp_port,
			  struct tc_red_qopt_offload *p);
int mlxsw_sp_setup_tc_prio(struct mlxsw_sp_port *mlxsw_sp_port,
			   struct tc_prio_qopt_offload *p);
int mlxsw_sp_setup_tc_ets(struct mlxsw_sp_port *mlxsw_sp_port,
			  struct tc_ets_qopt_offload *p);
int mlxsw_sp_setup_tc_tbf(struct mlxsw_sp_port *mlxsw_sp_port,
			  struct tc_tbf_qopt_offload *p);
int mlxsw_sp_setup_tc_fifo(struct mlxsw_sp_port *mlxsw_sp_port,
			   struct tc_fifo_qopt_offload *p);
int mlxsw_sp_setup_tc_block_qevent_early_drop(struct mlxsw_sp_port *mlxsw_sp_port,
					      struct flow_block_offload *f);

/* spectrum_fid.c */
bool mlxsw_sp_fid_is_dummy(struct mlxsw_sp *mlxsw_sp, u16 fid_index);
bool mlxsw_sp_fid_lag_vid_valid(const struct mlxsw_sp_fid *fid);
struct mlxsw_sp_fid *mlxsw_sp_fid_lookup_by_index(struct mlxsw_sp *mlxsw_sp,
						  u16 fid_index);
int mlxsw_sp_fid_nve_ifindex(const struct mlxsw_sp_fid *fid, int *nve_ifindex);
int mlxsw_sp_fid_nve_type(const struct mlxsw_sp_fid *fid,
			  enum mlxsw_sp_nve_type *p_type);
struct mlxsw_sp_fid *mlxsw_sp_fid_lookup_by_vni(struct mlxsw_sp *mlxsw_sp,
						__be32 vni);
int mlxsw_sp_fid_vni(const struct mlxsw_sp_fid *fid, __be32 *vni);
int mlxsw_sp_fid_nve_flood_index_set(struct mlxsw_sp_fid *fid,
				     u32 nve_flood_index);
void mlxsw_sp_fid_nve_flood_index_clear(struct mlxsw_sp_fid *fid);
bool mlxsw_sp_fid_nve_flood_index_is_set(const struct mlxsw_sp_fid *fid);
int mlxsw_sp_fid_vni_set(struct mlxsw_sp_fid *fid, enum mlxsw_sp_nve_type type,
			 __be32 vni, int nve_ifindex);
void mlxsw_sp_fid_vni_clear(struct mlxsw_sp_fid *fid);
bool mlxsw_sp_fid_vni_is_set(const struct mlxsw_sp_fid *fid);
void mlxsw_sp_fid_fdb_clear_offload(const struct mlxsw_sp_fid *fid,
				    const struct net_device *nve_dev);
int mlxsw_sp_fid_flood_set(struct mlxsw_sp_fid *fid,
			   enum mlxsw_sp_flood_type packet_type, u8 local_port,
			   bool member);
int mlxsw_sp_fid_port_vid_map(struct mlxsw_sp_fid *fid,
			      struct mlxsw_sp_port *mlxsw_sp_port, u16 vid);
void mlxsw_sp_fid_port_vid_unmap(struct mlxsw_sp_fid *fid,
				 struct mlxsw_sp_port *mlxsw_sp_port, u16 vid);
u16 mlxsw_sp_fid_index(const struct mlxsw_sp_fid *fid);
enum mlxsw_sp_fid_type mlxsw_sp_fid_type(const struct mlxsw_sp_fid *fid);
void mlxsw_sp_fid_rif_set(struct mlxsw_sp_fid *fid, struct mlxsw_sp_rif *rif);
struct mlxsw_sp_rif *mlxsw_sp_fid_rif(const struct mlxsw_sp_fid *fid);
enum mlxsw_sp_rif_type
mlxsw_sp_fid_type_rif_type(const struct mlxsw_sp *mlxsw_sp,
			   enum mlxsw_sp_fid_type type);
u16 mlxsw_sp_fid_8021q_vid(const struct mlxsw_sp_fid *fid);
struct mlxsw_sp_fid *mlxsw_sp_fid_8021q_get(struct mlxsw_sp *mlxsw_sp, u16 vid);
struct mlxsw_sp_fid *mlxsw_sp_fid_8021d_get(struct mlxsw_sp *mlxsw_sp,
					    int br_ifindex);
struct mlxsw_sp_fid *mlxsw_sp_fid_8021q_lookup(struct mlxsw_sp *mlxsw_sp,
					       u16 vid);
struct mlxsw_sp_fid *mlxsw_sp_fid_8021d_lookup(struct mlxsw_sp *mlxsw_sp,
					       int br_ifindex);
struct mlxsw_sp_fid *mlxsw_sp_fid_rfid_get(struct mlxsw_sp *mlxsw_sp,
					   u16 rif_index);
struct mlxsw_sp_fid *mlxsw_sp_fid_dummy_get(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_fid_put(struct mlxsw_sp_fid *fid);
int mlxsw_sp_port_fids_init(struct mlxsw_sp_port *mlxsw_sp_port);
void mlxsw_sp_port_fids_fini(struct mlxsw_sp_port *mlxsw_sp_port);
int mlxsw_sp_fids_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_fids_fini(struct mlxsw_sp *mlxsw_sp);

/* spectrum_mr.c */
enum mlxsw_sp_mr_route_prio {
	MLXSW_SP_MR_ROUTE_PRIO_SG,
	MLXSW_SP_MR_ROUTE_PRIO_STARG,
	MLXSW_SP_MR_ROUTE_PRIO_CATCHALL,
	__MLXSW_SP_MR_ROUTE_PRIO_MAX
};

#define MLXSW_SP_MR_ROUTE_PRIO_MAX (__MLXSW_SP_MR_ROUTE_PRIO_MAX - 1)

struct mlxsw_sp_mr_route_key;

struct mlxsw_sp_mr_tcam_ops {
	size_t priv_size;
	int (*init)(struct mlxsw_sp *mlxsw_sp, void *priv);
	void (*fini)(void *priv);
	size_t route_priv_size;
	int (*route_create)(struct mlxsw_sp *mlxsw_sp, void *priv,
			    void *route_priv,
			    struct mlxsw_sp_mr_route_key *key,
			    struct mlxsw_afa_block *afa_block,
			    enum mlxsw_sp_mr_route_prio prio);
	void (*route_destroy)(struct mlxsw_sp *mlxsw_sp, void *priv,
			      void *route_priv,
			      struct mlxsw_sp_mr_route_key *key);
	int (*route_update)(struct mlxsw_sp *mlxsw_sp, void *route_priv,
			    struct mlxsw_sp_mr_route_key *key,
			    struct mlxsw_afa_block *afa_block);
};

/* spectrum1_mr_tcam.c */
extern const struct mlxsw_sp_mr_tcam_ops mlxsw_sp1_mr_tcam_ops;

/* spectrum2_mr_tcam.c */
extern const struct mlxsw_sp_mr_tcam_ops mlxsw_sp2_mr_tcam_ops;

/* spectrum_nve.c */
struct mlxsw_sp_nve_params {
	enum mlxsw_sp_nve_type type;
	__be32 vni;
	const struct net_device *dev;
	u16 ethertype;
};

extern const struct mlxsw_sp_nve_ops *mlxsw_sp1_nve_ops_arr[];
extern const struct mlxsw_sp_nve_ops *mlxsw_sp2_nve_ops_arr[];

int mlxsw_sp_nve_learned_ip_resolve(struct mlxsw_sp *mlxsw_sp, u32 uip,
				    enum mlxsw_sp_l3proto proto,
				    union mlxsw_sp_l3addr *addr);
int mlxsw_sp_nve_flood_ip_add(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_fid *fid,
			      enum mlxsw_sp_l3proto proto,
			      union mlxsw_sp_l3addr *addr);
void mlxsw_sp_nve_flood_ip_del(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_fid *fid,
			       enum mlxsw_sp_l3proto proto,
			       union mlxsw_sp_l3addr *addr);
int mlxsw_sp_nve_fid_enable(struct mlxsw_sp *mlxsw_sp, struct mlxsw_sp_fid *fid,
			    struct mlxsw_sp_nve_params *params,
			    struct netlink_ext_ack *extack);
void mlxsw_sp_nve_fid_disable(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_fid *fid);
int mlxsw_sp_port_nve_init(struct mlxsw_sp_port *mlxsw_sp_port);
void mlxsw_sp_port_nve_fini(struct mlxsw_sp_port *mlxsw_sp_port);
int mlxsw_sp_nve_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_nve_fini(struct mlxsw_sp *mlxsw_sp);

/* spectrum_nve_vxlan.c */
int mlxsw_sp_nve_inc_parsing_depth_get(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_nve_inc_parsing_depth_put(struct mlxsw_sp *mlxsw_sp);

/* spectrum_trap.c */
int mlxsw_sp_devlink_traps_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_devlink_traps_fini(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_trap_init(struct mlxsw_core *mlxsw_core,
		       const struct devlink_trap *trap, void *trap_ctx);
void mlxsw_sp_trap_fini(struct mlxsw_core *mlxsw_core,
			const struct devlink_trap *trap, void *trap_ctx);
int mlxsw_sp_trap_action_set(struct mlxsw_core *mlxsw_core,
			     const struct devlink_trap *trap,
			     enum devlink_trap_action action,
			     struct netlink_ext_ack *extack);
int mlxsw_sp_trap_group_init(struct mlxsw_core *mlxsw_core,
			     const struct devlink_trap_group *group);
int mlxsw_sp_trap_group_set(struct mlxsw_core *mlxsw_core,
			    const struct devlink_trap_group *group,
			    const struct devlink_trap_policer *policer,
			    struct netlink_ext_ack *extack);
int
mlxsw_sp_trap_policer_init(struct mlxsw_core *mlxsw_core,
			   const struct devlink_trap_policer *policer);
void mlxsw_sp_trap_policer_fini(struct mlxsw_core *mlxsw_core,
				const struct devlink_trap_policer *policer);
int
mlxsw_sp_trap_policer_set(struct mlxsw_core *mlxsw_core,
			  const struct devlink_trap_policer *policer,
			  u64 rate, u64 burst, struct netlink_ext_ack *extack);
int
mlxsw_sp_trap_policer_counter_get(struct mlxsw_core *mlxsw_core,
				  const struct devlink_trap_policer *policer,
				  u64 *p_drops);
int mlxsw_sp_trap_group_policer_hw_id_get(struct mlxsw_sp *mlxsw_sp, u16 id,
					  bool *p_enabled, u16 *p_hw_id);

static inline struct net *mlxsw_sp_net(struct mlxsw_sp *mlxsw_sp)
{
	return mlxsw_core_net(mlxsw_sp->core);
}

/* spectrum_ethtool.c */
extern const struct ethtool_ops mlxsw_sp_port_ethtool_ops;
extern const struct mlxsw_sp_port_type_speed_ops mlxsw_sp1_port_type_speed_ops;
extern const struct mlxsw_sp_port_type_speed_ops mlxsw_sp2_port_type_speed_ops;

/* spectrum_policer.c */
extern const struct mlxsw_sp_policer_core_ops mlxsw_sp1_policer_core_ops;
extern const struct mlxsw_sp_policer_core_ops mlxsw_sp2_policer_core_ops;

enum mlxsw_sp_policer_type {
	MLXSW_SP_POLICER_TYPE_SINGLE_RATE,

	__MLXSW_SP_POLICER_TYPE_MAX,
	MLXSW_SP_POLICER_TYPE_MAX = __MLXSW_SP_POLICER_TYPE_MAX - 1,
};

struct mlxsw_sp_policer_params {
	u64 rate;
	u64 burst;
	bool bytes;
};

int mlxsw_sp_policer_add(struct mlxsw_sp *mlxsw_sp,
			 enum mlxsw_sp_policer_type type,
			 const struct mlxsw_sp_policer_params *params,
			 struct netlink_ext_ack *extack, u16 *p_policer_index);
void mlxsw_sp_policer_del(struct mlxsw_sp *mlxsw_sp,
			  enum mlxsw_sp_policer_type type,
			  u16 policer_index);
int mlxsw_sp_policer_drops_counter_get(struct mlxsw_sp *mlxsw_sp,
				       enum mlxsw_sp_policer_type type,
				       u16 policer_index, u64 *p_drops);
int mlxsw_sp_policers_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_policers_fini(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_policer_resources_register(struct mlxsw_core *mlxsw_core);

#endif
