/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum.h
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Jiri Pirko <jiri@mellanox.com>
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
#include <linux/bitops.h>
#include <linux/if_vlan.h>
#include <linux/list.h>
#include <net/switchdev.h>
#include <net/devlink.h>

#include "port.h"
#include "core.h"

#define MLXSW_SP_VFID_BASE VLAN_N_VID
#define MLXSW_SP_VFID_PORT_MAX 512	/* Non-bridged VLAN interfaces */
#define MLXSW_SP_VFID_BR_MAX 8192	/* Bridged VLAN interfaces */
#define MLXSW_SP_VFID_MAX (MLXSW_SP_VFID_PORT_MAX + MLXSW_SP_VFID_BR_MAX)

#define MLXSW_SP_LAG_MAX 64
#define MLXSW_SP_PORT_PER_LAG_MAX 16

#define MLXSW_SP_MID_MAX 7000

#define MLXSW_SP_PORTS_PER_CLUSTER_MAX 4

#define MLXSW_SP_PORT_BASE_SPEED 25000	/* Mb/s */

struct mlxsw_sp_port;

struct mlxsw_sp_upper {
	struct net_device *dev;
	unsigned int ref_count;
};

struct mlxsw_sp_vfid {
	struct list_head list;
	u16 nr_vports;
	u16 vfid;	/* Starting at 0 */
	struct net_device *br_dev;
	u16 vid;
};

struct mlxsw_sp_mid {
	struct list_head list;
	unsigned char addr[ETH_ALEN];
	u16 vid;
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
	return fid >= MLXSW_SP_VFID_BASE;
}

struct mlxsw_sp {
	struct {
		struct list_head list;
		unsigned long mapped[BITS_TO_LONGS(MLXSW_SP_VFID_PORT_MAX)];
	} port_vfids;
	struct {
		struct list_head list;
		unsigned long mapped[BITS_TO_LONGS(MLXSW_SP_VFID_BR_MAX)];
	} br_vfids;
	struct {
		struct list_head list;
		unsigned long mapped[BITS_TO_LONGS(MLXSW_SP_MID_MAX)];
	} br_mids;
	unsigned long active_fids[BITS_TO_LONGS(VLAN_N_VID)];
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
	struct mlxsw_sp_upper lags[MLXSW_SP_LAG_MAX];
	u8 port_to_module[MLXSW_PORT_MAX_PORTS];
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

struct mlxsw_sp_port {
	struct net_device *dev;
	struct mlxsw_sp_port_pcpu_stats __percpu *pcpu_stats;
	struct mlxsw_sp *mlxsw_sp;
	u8 local_port;
	u8 stp_state;
	u8 learning:1,
	   learning_sync:1,
	   uc_flood:1,
	   bridged:1,
	   lagged:1,
	   split:1;
	u16 pvid;
	u16 lag_id;
	struct {
		struct list_head list;
		struct mlxsw_sp_vfid *vfid;
		u16 vid;
	} vport;
	/* 802.1Q bridge VLANs */
	unsigned long *active_vlans;
	unsigned long *untagged_vlans;
	/* VLAN interfaces */
	struct list_head vports_list;
	struct devlink_port devlink_port;
};

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

static inline bool
mlxsw_sp_port_is_vport(const struct mlxsw_sp_port *mlxsw_sp_port)
{
	return mlxsw_sp_port->vport.vfid;
}

static inline struct net_device *
mlxsw_sp_vport_br_get(const struct mlxsw_sp_port *mlxsw_sp_vport)
{
	return mlxsw_sp_vport->vport.vfid->br_dev;
}

static inline u16
mlxsw_sp_vport_vid_get(const struct mlxsw_sp_port *mlxsw_sp_vport)
{
	return mlxsw_sp_vport->vport.vid;
}

static inline u16
mlxsw_sp_vport_vfid_get(const struct mlxsw_sp_port *mlxsw_sp_vport)
{
	return mlxsw_sp_vport->vport.vfid->vfid;
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
mlxsw_sp_port_vport_find_by_vfid(const struct mlxsw_sp_port *mlxsw_sp_port,
				 u16 vfid)
{
	struct mlxsw_sp_port *mlxsw_sp_vport;

	list_for_each_entry(mlxsw_sp_vport, &mlxsw_sp_port->vports_list,
			    vport.list) {
		if (mlxsw_sp_vport_vfid_get(mlxsw_sp_vport) == vfid)
			return mlxsw_sp_vport;
	}

	return NULL;
}

enum mlxsw_sp_flood_table {
	MLXSW_SP_FLOOD_TABLE_UC,
	MLXSW_SP_FLOOD_TABLE_BM,
};

int mlxsw_sp_buffers_init(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_port_buffers_init(struct mlxsw_sp_port *mlxsw_sp_port);

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
int mlxsw_sp_port_add_vid(struct net_device *dev, __be16 __always_unused proto,
			  u16 vid);
int mlxsw_sp_port_kill_vid(struct net_device *dev,
			   __be16 __always_unused proto, u16 vid);
int mlxsw_sp_vport_flood_set(struct mlxsw_sp_port *mlxsw_sp_vport, u16 vfid,
			     bool set, bool only_uc);
void mlxsw_sp_port_active_vlans_del(struct mlxsw_sp_port *mlxsw_sp_port);
int mlxsw_sp_port_pvid_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid);

#endif
