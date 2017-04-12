/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum.h
 * Copyright (c) 2015-2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015-2017 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2015 Ido Schimmel <idosch@mellanox.com>
 * Copyright (c) 2015 Elad Raz <eladr@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MLXSW_SPECTRUM_H
#define _MLXSW_SPECTRUM_H

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/rhashtable.h>
#include <linux/bitops.h>
#include <linux/if_vlan.h>
#include <linux/list.h>
#include <linux/dcbnl.h>
#include <linux/in6.h>
#include <linux/notifier.h>
#include <net/psample.h>
#include <net/pkt_cls.h>

#include "port.h"
#include "core.h"
#include "core_acl_flex_keys.h"
#include "core_acl_flex_actions.h"

#define MLXSW_SP_VFID_BASE VLAN_N_VID
#define MLXSW_SP_VFID_MAX 1024	/* Bridged VLAN interfaces */

#define MLXSW_SP_RFID_BASE 15360
#define MLXSW_SP_INVALID_RIF 0xffff

#define MLXSW_SP_MID_MAX 7000

#define MLXSW_SP_PORTS_PER_CLUSTER_MAX 4

#define MLXSW_SP_LPM_TREE_MIN 2 /* trees 0 and 1 are reserved */
#define MLXSW_SP_LPM_TREE_MAX 22
#define MLXSW_SP_LPM_TREE_COUNT (MLXSW_SP_LPM_TREE_MAX - MLXSW_SP_LPM_TREE_MIN)

#define MLXSW_SP_PORT_BASE_SPEED 25000	/* Mb/s */

#define MLXSW_SP_BYTES_PER_CELL 96

#define MLXSW_SP_BYTES_TO_CELLS(b) DIV_ROUND_UP(b, MLXSW_SP_BYTES_PER_CELL)
#define MLXSW_SP_CELLS_TO_BYTES(c) (c * MLXSW_SP_BYTES_PER_CELL)

#define MLXSW_SP_KVD_LINEAR_SIZE 65536 /* entries */
#define MLXSW_SP_KVD_GRANULARITY 128

/* Maximum delay buffer needed in case of PAUSE frames, in cells.
 * Assumes 100m cable and maximum MTU.
 */
#define MLXSW_SP_PAUSE_DELAY 612

#define MLXSW_SP_CELL_FACTOR 2	/* 2 * cell_size / (IPG + cell_size + 1) */

static inline u16 mlxsw_sp_pfc_delay_get(int mtu, u16 delay)
{
	delay = MLXSW_SP_BYTES_TO_CELLS(DIV_ROUND_UP(delay, BITS_PER_BYTE));
	return MLXSW_SP_CELL_FACTOR * delay + MLXSW_SP_BYTES_TO_CELLS(mtu);
}

struct mlxsw_sp_port;

struct mlxsw_sp_upper {
	struct net_device *dev;
	unsigned int ref_count;
};

struct mlxsw_sp_fid {
	void (*leave)(struct mlxsw_sp_port *mlxsw_sp_vport);
	struct list_head list;
	unsigned int ref_count;
	struct net_device *dev;
	struct mlxsw_sp_rif *r;
	u16 fid;
};

struct mlxsw_sp_rif {
	struct list_head nexthop_list;
	struct list_head neigh_list;
	struct net_device *dev;
	unsigned int ref_count;
	struct mlxsw_sp_fid *f;
	unsigned char addr[ETH_ALEN];
	int mtu;
	u16 rif;
};

struct mlxsw_sp_mid {
	struct list_head list;
	unsigned char addr[ETH_ALEN];
	u16 fid;
	u16 mid;
	unsigned int ref_count;
};

static inline u16 mlxsw_sp_vfid_to_fid(u16 vfid)
{
	return MLXSW_SP_VFID_BASE + vfid;
}

static inline u16 mlxsw_sp_fid_to_vfid(u16 fid)
{
	return fid - MLXSW_SP_VFID_BASE;
}

static inline bool mlxsw_sp_fid_is_vfid(u16 fid)
{
	return fid >= MLXSW_SP_VFID_BASE && fid < MLXSW_SP_RFID_BASE;
}

static inline bool mlxsw_sp_fid_is_rfid(u16 fid)
{
	return fid >= MLXSW_SP_RFID_BASE;
}

static inline u16 mlxsw_sp_rif_sp_to_fid(u16 rif)
{
	return MLXSW_SP_RFID_BASE + rif;
}

struct mlxsw_sp_sb_pr {
	enum mlxsw_reg_sbpr_mode mode;
	u32 size;
};

struct mlxsw_cp_sb_occ {
	u32 cur;
	u32 max;
};

struct mlxsw_sp_sb_cm {
	u32 min_buff;
	u32 max_buff;
	u8 pool;
	struct mlxsw_cp_sb_occ occ;
};

struct mlxsw_sp_sb_pm {
	u32 min_buff;
	u32 max_buff;
	struct mlxsw_cp_sb_occ occ;
};

#define MLXSW_SP_SB_POOL_COUNT	4
#define MLXSW_SP_SB_TC_COUNT	8

struct mlxsw_sp_sb {
	struct mlxsw_sp_sb_pr prs[2][MLXSW_SP_SB_POOL_COUNT];
	struct {
		struct mlxsw_sp_sb_cm cms[2][MLXSW_SP_SB_TC_COUNT];
		struct mlxsw_sp_sb_pm pms[2][MLXSW_SP_SB_POOL_COUNT];
	} ports[MLXSW_PORT_MAX_PORTS];
};

#define MLXSW_SP_PREFIX_COUNT (sizeof(struct in6_addr) * BITS_PER_BYTE)

struct mlxsw_sp_prefix_usage {
	DECLARE_BITMAP(b, MLXSW_SP_PREFIX_COUNT);
};

enum mlxsw_sp_l3proto {
	MLXSW_SP_L3_PROTO_IPV4,
	MLXSW_SP_L3_PROTO_IPV6,
};

struct mlxsw_sp_lpm_tree {
	u8 id; /* tree ID */
	unsigned int ref_count;
	enum mlxsw_sp_l3proto proto;
	struct mlxsw_sp_prefix_usage prefix_usage;
};

struct mlxsw_sp_fib;

struct mlxsw_sp_vr {
	u16 id; /* virtual router ID */
	bool used;
	enum mlxsw_sp_l3proto proto;
	u32 tb_id; /* kernel fib table id */
	struct mlxsw_sp_lpm_tree *lpm_tree;
	struct mlxsw_sp_fib *fib;
};

enum mlxsw_sp_span_type {
	MLXSW_SP_SPAN_EGRESS,
	MLXSW_SP_SPAN_INGRESS
};

struct mlxsw_sp_span_inspected_port {
	struct list_head list;
	enum mlxsw_sp_span_type type;
	u8 local_port;
};

struct mlxsw_sp_span_entry {
	u8 local_port;
	bool used;
	struct list_head bound_ports_list;
	int ref_count;
	int id;
};

enum mlxsw_sp_port_mall_action_type {
	MLXSW_SP_PORT_MALL_MIRROR,
	MLXSW_SP_PORT_MALL_SAMPLE,
};

struct mlxsw_sp_port_mall_mirror_tc_entry {
	u8 to_local_port;
	bool ingress;
};

struct mlxsw_sp_port_mall_tc_entry {
	struct list_head list;
	unsigned long cookie;
	enum mlxsw_sp_port_mall_action_type type;
	union {
		struct mlxsw_sp_port_mall_mirror_tc_entry mirror;
	};
};

struct mlxsw_sp_router {
	struct mlxsw_sp_lpm_tree lpm_trees[MLXSW_SP_LPM_TREE_COUNT];
	struct mlxsw_sp_vr *vrs;
	struct rhashtable neigh_ht;
	struct rhashtable nexthop_group_ht;
	struct rhashtable nexthop_ht;
	struct {
		struct delayed_work dw;
		unsigned long interval;	/* ms */
	} neighs_update;
	struct delayed_work nexthop_probe_dw;
#define MLXSW_SP_UNRESOLVED_NH_PROBE_INTERVAL 5000 /* ms */
	struct list_head nexthop_neighs_list;
	bool aborted;
};

struct mlxsw_sp_acl;

struct mlxsw_sp {
	struct {
		struct list_head list;
		DECLARE_BITMAP(mapped, MLXSW_SP_VFID_MAX);
	} vfids;
	struct {
		struct list_head list;
		DECLARE_BITMAP(mapped, MLXSW_SP_MID_MAX);
	} br_mids;
	struct list_head fids;	/* VLAN-aware bridge FIDs */
	struct mlxsw_sp_rif **rifs;
	struct mlxsw_sp_port **ports;
	struct mlxsw_core *core;
	const struct mlxsw_bus_info *bus_info;
	unsigned char base_mac[ETH_ALEN];
	struct {
		struct delayed_work dw;
#define MLXSW_SP_DEFAULT_LEARNING_INTERVAL 100
		unsigned int interval; /* ms */
	} fdb_notify;
#define MLXSW_SP_MIN_AGEING_TIME 10
#define MLXSW_SP_MAX_AGEING_TIME 1000000
#define MLXSW_SP_DEFAULT_AGEING_TIME 300
	u32 ageing_time;
	struct mlxsw_sp_upper master_bridge;
	struct mlxsw_sp_upper *lags;
	u8 port_to_module[MLXSW_PORT_MAX_PORTS];
	struct mlxsw_sp_sb sb;
	struct mlxsw_sp_router router;
	struct mlxsw_sp_acl *acl;
	struct {
		DECLARE_BITMAP(usage, MLXSW_SP_KVD_LINEAR_SIZE);
	} kvdl;

	struct {
		struct mlxsw_sp_span_entry *entries;
		int entries_count;
	} span;
	struct notifier_block fib_nb;
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

struct mlxsw_sp_port_sample {
	struct psample_group __rcu *psample_group;
	u32 trunc_size;
	u32 rate;
	bool truncate;
};

struct mlxsw_sp_port {
	struct net_device *dev;
	struct mlxsw_sp_port_pcpu_stats __percpu *pcpu_stats;
	struct mlxsw_sp *mlxsw_sp;
	u8 local_port;
	u8 stp_state;
	u16 learning:1,
	   learning_sync:1,
	   uc_flood:1,
	   mc_flood:1,
	   mc_router:1,
	   mc_disabled:1,
	   bridged:1,
	   lagged:1,
	   split:1;
	u16 pvid;
	u16 lag_id;
	struct {
		struct list_head list;
		struct mlxsw_sp_fid *f;
		u16 vid;
	} vport;
	struct {
		u8 tx_pause:1,
		   rx_pause:1,
		   autoneg:1;
	} link;
	struct {
		struct ieee_ets *ets;
		struct ieee_maxrate *maxrate;
		struct ieee_pfc *pfc;
	} dcb;
	struct {
		u8 module;
		u8 width;
		u8 lane;
	} mapping;
	/* 802.1Q bridge VLANs */
	unsigned long *active_vlans;
	unsigned long *untagged_vlans;
	/* VLAN interfaces */
	struct list_head vports_list;
	/* TC handles */
	struct list_head mall_tc_list;
	struct {
		#define MLXSW_HW_STATS_UPDATE_TIME HZ
		struct rtnl_link_stats64 *cache;
		struct delayed_work update_dw;
	} hw_stats;
	struct mlxsw_sp_port_sample *sample;
};

bool mlxsw_sp_port_dev_check(const struct net_device *dev);
struct mlxsw_sp_port *mlxsw_sp_port_lower_dev_hold(struct net_device *dev);
void mlxsw_sp_port_dev_put(struct mlxsw_sp_port *mlxsw_sp_port);

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

static inline u16
mlxsw_sp_vport_vid_get(const struct mlxsw_sp_port *mlxsw_sp_vport)
{
	return mlxsw_sp_vport->vport.vid;
}

static inline bool
mlxsw_sp_port_is_vport(const struct mlxsw_sp_port *mlxsw_sp_port)
{
	u16 vid = mlxsw_sp_vport_vid_get(mlxsw_sp_port);

	return vid != 0;
}

static inline void mlxsw_sp_vport_fid_set(struct mlxsw_sp_port *mlxsw_sp_vport,
					  struct mlxsw_sp_fid *f)
{
	mlxsw_sp_vport->vport.f = f;
}

static inline struct mlxsw_sp_fid *
mlxsw_sp_vport_fid_get(const struct mlxsw_sp_port *mlxsw_sp_vport)
{
	return mlxsw_sp_vport->vport.f;
}

static inline struct net_device *
mlxsw_sp_vport_dev_get(const struct mlxsw_sp_port *mlxsw_sp_vport)
{
	struct mlxsw_sp_fid *f = mlxsw_sp_vport_fid_get(mlxsw_sp_vport);

	return f ? f->dev : NULL;
}

static inline struct mlxsw_sp_port *
mlxsw_sp_port_vport_find(const struct mlxsw_sp_port *mlxsw_sp_port, u16 vid)
{
	struct mlxsw_sp_port *mlxsw_sp_vport;

	list_for_each_entry(mlxsw_sp_vport, &mlxsw_sp_port->vports_list,
			    vport.list) {
		if (mlxsw_sp_vport_vid_get(mlxsw_sp_vport) == vid)
			return mlxsw_sp_vport;
	}

	return NULL;
}

static inline struct mlxsw_sp_port *
mlxsw_sp_port_vport_find_by_fid(const struct mlxsw_sp_port *mlxsw_sp_port,
				u16 fid)
{
	struct mlxsw_sp_port *mlxsw_sp_vport;

	list_for_each_entry(mlxsw_sp_vport, &mlxsw_sp_port->vports_list,
			    vport.list) {
		struct mlxsw_sp_fid *f = mlxsw_sp_vport_fid_get(mlxsw_sp_vport);

		if (f && f->fid == fid)
			return mlxsw_sp_vport;
	}

	return NULL;
}

static inline struct mlxsw_sp_fid *mlxsw_sp_fid_find(struct mlxsw_sp *mlxsw_sp,
						     u16 fid)
{
	struct mlxsw_sp_fid *f;

	list_for_each_entry(f, &mlxsw_sp->fids, list)
		if (f->fid == fid)
			return f;

	return NULL;
}

static inline struct mlxsw_sp_fid *
mlxsw_sp_vfid_find(const struct mlxsw_sp *mlxsw_sp,
		   const struct net_device *br_dev)
{
	struct mlxsw_sp_fid *f;

	list_for_each_entry(f, &mlxsw_sp->vfids.list, list)
		if (f->dev == br_dev)
			return f;

	return NULL;
}

static inline struct mlxsw_sp_rif *
mlxsw_sp_rif_find_by_dev(const struct mlxsw_sp *mlxsw_sp,
			 const struct net_device *dev)
{
	int i;

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS); i++)
		if (mlxsw_sp->rifs[i] && mlxsw_sp->rifs[i]->dev == dev)
			return mlxsw_sp->rifs[i];

	return NULL;
}

enum mlxsw_sp_flood_table {
	MLXSW_SP_FLOOD_TABLE_UC,
	MLXSW_SP_FLOOD_TABLE_BC,
	MLXSW_SP_FLOOD_TABLE_MC,
};

int mlxsw_sp_buffers_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_buffers_fini(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_port_buffers_init(struct mlxsw_sp_port *mlxsw_sp_port);
int mlxsw_sp_sb_pool_get(struct mlxsw_core *mlxsw_core,
			 unsigned int sb_index, u16 pool_index,
			 struct devlink_sb_pool_info *pool_info);
int mlxsw_sp_sb_pool_set(struct mlxsw_core *mlxsw_core,
			 unsigned int sb_index, u16 pool_index, u32 size,
			 enum devlink_sb_threshold_type threshold_type);
int mlxsw_sp_sb_port_pool_get(struct mlxsw_core_port *mlxsw_core_port,
			      unsigned int sb_index, u16 pool_index,
			      u32 *p_threshold);
int mlxsw_sp_sb_port_pool_set(struct mlxsw_core_port *mlxsw_core_port,
			      unsigned int sb_index, u16 pool_index,
			      u32 threshold);
int mlxsw_sp_sb_tc_pool_bind_get(struct mlxsw_core_port *mlxsw_core_port,
				 unsigned int sb_index, u16 tc_index,
				 enum devlink_sb_pool_type pool_type,
				 u16 *p_pool_index, u32 *p_threshold);
int mlxsw_sp_sb_tc_pool_bind_set(struct mlxsw_core_port *mlxsw_core_port,
				 unsigned int sb_index, u16 tc_index,
				 enum devlink_sb_pool_type pool_type,
				 u16 pool_index, u32 threshold);
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

int mlxsw_sp_switchdev_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_switchdev_fini(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_port_vlan_init(struct mlxsw_sp_port *mlxsw_sp_port);
void mlxsw_sp_port_switchdev_init(struct mlxsw_sp_port *mlxsw_sp_port);
void mlxsw_sp_port_switchdev_fini(struct mlxsw_sp_port *mlxsw_sp_port);
int mlxsw_sp_port_vid_to_fid_set(struct mlxsw_sp_port *mlxsw_sp_port,
				 enum mlxsw_reg_svfa_mt mt, bool valid, u16 fid,
				 u16 vid);
int mlxsw_sp_port_vlan_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid_begin,
			   u16 vid_end, bool is_member, bool untagged);
int mlxsw_sp_vport_flood_set(struct mlxsw_sp_port *mlxsw_sp_vport, u16 fid,
			     bool set);
void mlxsw_sp_port_active_vlans_del(struct mlxsw_sp_port *mlxsw_sp_port);
int mlxsw_sp_port_pvid_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid);
int mlxsw_sp_port_fdb_flush(struct mlxsw_sp_port *mlxsw_sp_port, u16 fid);
int mlxsw_sp_rif_fdb_op(struct mlxsw_sp *mlxsw_sp, const char *mac, u16 fid,
			bool adding);
struct mlxsw_sp_fid *mlxsw_sp_fid_create(struct mlxsw_sp *mlxsw_sp, u16 fid);
void mlxsw_sp_fid_destroy(struct mlxsw_sp *mlxsw_sp, struct mlxsw_sp_fid *f);
void mlxsw_sp_rif_bridge_destroy(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_rif *r);
int mlxsw_sp_port_ets_set(struct mlxsw_sp_port *mlxsw_sp_port,
			  enum mlxsw_reg_qeec_hr hr, u8 index, u8 next_index,
			  bool dwrr, u8 dwrr_weight);
int mlxsw_sp_port_prio_tc_set(struct mlxsw_sp_port *mlxsw_sp_port,
			      u8 switch_prio, u8 tclass);
int __mlxsw_sp_port_headroom_set(struct mlxsw_sp_port *mlxsw_sp_port, int mtu,
				 u8 *prio_tc, bool pause_en,
				 struct ieee_pfc *my_pfc);
int mlxsw_sp_port_ets_maxrate_set(struct mlxsw_sp_port *mlxsw_sp_port,
				  enum mlxsw_reg_qeec_hr hr, u8 index,
				  u8 next_index, u32 maxrate);
int __mlxsw_sp_port_vid_learning_set(struct mlxsw_sp_port *mlxsw_sp_port,
				     u16 vid_begin, u16 vid_end,
				     bool learn_enable);

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

int mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_router_fini(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_router_netevent_event(struct notifier_block *unused,
				   unsigned long event, void *ptr);
void mlxsw_sp_router_rif_gone_sync(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_rif *r);

int mlxsw_sp_kvdl_alloc(struct mlxsw_sp *mlxsw_sp, unsigned int entry_count);
void mlxsw_sp_kvdl_free(struct mlxsw_sp *mlxsw_sp, int entry_index);

struct mlxsw_afk *mlxsw_sp_acl_afk(struct mlxsw_sp_acl *acl);

struct mlxsw_sp_acl_rule_info {
	unsigned int priority;
	struct mlxsw_afk_element_values values;
	struct mlxsw_afa_block *act_block;
};

enum mlxsw_sp_acl_profile {
	MLXSW_SP_ACL_PROFILE_FLOWER,
};

struct mlxsw_sp_acl_profile_ops {
	size_t ruleset_priv_size;
	int (*ruleset_add)(struct mlxsw_sp *mlxsw_sp,
			   void *priv, void *ruleset_priv);
	void (*ruleset_del)(struct mlxsw_sp *mlxsw_sp, void *ruleset_priv);
	int (*ruleset_bind)(struct mlxsw_sp *mlxsw_sp, void *ruleset_priv,
			    struct net_device *dev, bool ingress);
	void (*ruleset_unbind)(struct mlxsw_sp *mlxsw_sp, void *ruleset_priv);
	size_t rule_priv_size;
	int (*rule_add)(struct mlxsw_sp *mlxsw_sp,
			void *ruleset_priv, void *rule_priv,
			struct mlxsw_sp_acl_rule_info *rulei);
	void (*rule_del)(struct mlxsw_sp *mlxsw_sp, void *rule_priv);
};

struct mlxsw_sp_acl_ops {
	size_t priv_size;
	int (*init)(struct mlxsw_sp *mlxsw_sp, void *priv);
	void (*fini)(struct mlxsw_sp *mlxsw_sp, void *priv);
	const struct mlxsw_sp_acl_profile_ops *
			(*profile_ops)(struct mlxsw_sp *mlxsw_sp,
				       enum mlxsw_sp_acl_profile profile);
};

struct mlxsw_sp_acl_ruleset;

struct mlxsw_sp_acl_ruleset *
mlxsw_sp_acl_ruleset_get(struct mlxsw_sp *mlxsw_sp,
			 struct net_device *dev, bool ingress,
			 enum mlxsw_sp_acl_profile profile);
void mlxsw_sp_acl_ruleset_put(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_acl_ruleset *ruleset);

struct mlxsw_sp_acl_rule_info *
mlxsw_sp_acl_rulei_create(struct mlxsw_sp_acl *acl);
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
void mlxsw_sp_acl_rulei_act_continue(struct mlxsw_sp_acl_rule_info *rulei);
void mlxsw_sp_acl_rulei_act_jump(struct mlxsw_sp_acl_rule_info *rulei,
				 u16 group_id);
int mlxsw_sp_acl_rulei_act_drop(struct mlxsw_sp_acl_rule_info *rulei);
int mlxsw_sp_acl_rulei_act_fwd(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_rule_info *rulei,
			       struct net_device *out_dev);

struct mlxsw_sp_acl_rule;

struct mlxsw_sp_acl_rule *
mlxsw_sp_acl_rule_create(struct mlxsw_sp *mlxsw_sp,
			 struct mlxsw_sp_acl_ruleset *ruleset,
			 unsigned long cookie);
void mlxsw_sp_acl_rule_destroy(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_rule *rule);
int mlxsw_sp_acl_rule_add(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_acl_rule *rule);
void mlxsw_sp_acl_rule_del(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_acl_rule *rule);
struct mlxsw_sp_acl_rule *
mlxsw_sp_acl_rule_lookup(struct mlxsw_sp *mlxsw_sp,
			 struct mlxsw_sp_acl_ruleset *ruleset,
			 unsigned long cookie);
struct mlxsw_sp_acl_rule_info *
mlxsw_sp_acl_rule_rulei(struct mlxsw_sp_acl_rule *rule);

int mlxsw_sp_acl_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_acl_fini(struct mlxsw_sp *mlxsw_sp);

extern const struct mlxsw_sp_acl_ops mlxsw_sp_acl_tcam_ops;

int mlxsw_sp_flower_replace(struct mlxsw_sp_port *mlxsw_sp_port, bool ingress,
			    __be16 protocol, struct tc_cls_flower_offload *f);
void mlxsw_sp_flower_destroy(struct mlxsw_sp_port *mlxsw_sp_port, bool ingress,
			     struct tc_cls_flower_offload *f);

#endif
