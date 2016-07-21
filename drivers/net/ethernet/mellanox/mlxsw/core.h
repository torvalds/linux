/*
 * drivers/net/ethernet/mellanox/mlxsw/core.h
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

#ifndef _MLXSW_CORE_H
#define _MLXSW_CORE_H

#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <net/devlink.h>

#include "trap.h"
#include "reg.h"

#include "cmd.h"

#define MLXSW_MODULE_ALIAS_PREFIX "mlxsw-driver-"
#define MODULE_MLXSW_DRIVER_ALIAS(kind)	\
	MODULE_ALIAS(MLXSW_MODULE_ALIAS_PREFIX kind)

#define MLXSW_DEVICE_KIND_SWITCHX2 "switchx2"
#define MLXSW_DEVICE_KIND_SPECTRUM "spectrum"

struct mlxsw_core;
struct mlxsw_driver;
struct mlxsw_bus;
struct mlxsw_bus_info;

void *mlxsw_core_driver_priv(struct mlxsw_core *mlxsw_core);

int mlxsw_core_driver_register(struct mlxsw_driver *mlxsw_driver);
void mlxsw_core_driver_unregister(struct mlxsw_driver *mlxsw_driver);

int mlxsw_core_bus_device_register(const struct mlxsw_bus_info *mlxsw_bus_info,
				   const struct mlxsw_bus *mlxsw_bus,
				   void *bus_priv);
void mlxsw_core_bus_device_unregister(struct mlxsw_core *mlxsw_core);

struct mlxsw_tx_info {
	u8 local_port;
	bool is_emad;
};

bool mlxsw_core_skb_transmit_busy(struct mlxsw_core *mlxsw_core,
				  const struct mlxsw_tx_info *tx_info);
int mlxsw_core_skb_transmit(struct mlxsw_core *mlxsw_core, struct sk_buff *skb,
			    const struct mlxsw_tx_info *tx_info);

struct mlxsw_rx_listener {
	void (*func)(struct sk_buff *skb, u8 local_port, void *priv);
	u8 local_port;
	u16 trap_id;
};

struct mlxsw_event_listener {
	void (*func)(const struct mlxsw_reg_info *reg,
		     char *payload, void *priv);
	enum mlxsw_event_trap_id trap_id;
};

int mlxsw_core_rx_listener_register(struct mlxsw_core *mlxsw_core,
				    const struct mlxsw_rx_listener *rxl,
				    void *priv);
void mlxsw_core_rx_listener_unregister(struct mlxsw_core *mlxsw_core,
				       const struct mlxsw_rx_listener *rxl,
				       void *priv);

int mlxsw_core_event_listener_register(struct mlxsw_core *mlxsw_core,
				       const struct mlxsw_event_listener *el,
				       void *priv);
void mlxsw_core_event_listener_unregister(struct mlxsw_core *mlxsw_core,
					  const struct mlxsw_event_listener *el,
					  void *priv);

typedef void mlxsw_reg_trans_cb_t(struct mlxsw_core *mlxsw_core, char *payload,
				  size_t payload_len, unsigned long cb_priv);

int mlxsw_reg_trans_query(struct mlxsw_core *mlxsw_core,
			  const struct mlxsw_reg_info *reg, char *payload,
			  struct list_head *bulk_list,
			  mlxsw_reg_trans_cb_t *cb, unsigned long cb_priv);
int mlxsw_reg_trans_write(struct mlxsw_core *mlxsw_core,
			  const struct mlxsw_reg_info *reg, char *payload,
			  struct list_head *bulk_list,
			  mlxsw_reg_trans_cb_t *cb, unsigned long cb_priv);
int mlxsw_reg_trans_bulk_wait(struct list_head *bulk_list);

int mlxsw_reg_query(struct mlxsw_core *mlxsw_core,
		    const struct mlxsw_reg_info *reg, char *payload);
int mlxsw_reg_write(struct mlxsw_core *mlxsw_core,
		    const struct mlxsw_reg_info *reg, char *payload);

struct mlxsw_rx_info {
	bool is_lag;
	union {
		u16 sys_port;
		u16 lag_id;
	} u;
	u8 lag_port_index;
	int trap_id;
};

void mlxsw_core_skb_receive(struct mlxsw_core *mlxsw_core, struct sk_buff *skb,
			    struct mlxsw_rx_info *rx_info);

void mlxsw_core_lag_mapping_set(struct mlxsw_core *mlxsw_core,
				u16 lag_id, u8 port_index, u8 local_port);
u8 mlxsw_core_lag_mapping_get(struct mlxsw_core *mlxsw_core,
			      u16 lag_id, u8 port_index);
void mlxsw_core_lag_mapping_clear(struct mlxsw_core *mlxsw_core,
				  u16 lag_id, u8 local_port);

struct mlxsw_core_port {
	struct devlink_port devlink_port;
};

static inline void *
mlxsw_core_port_driver_priv(struct mlxsw_core_port *mlxsw_core_port)
{
	/* mlxsw_core_port is ensured to always be the first field in driver
	 * port structure.
	 */
	return mlxsw_core_port;
}

int mlxsw_core_port_init(struct mlxsw_core *mlxsw_core,
			 struct mlxsw_core_port *mlxsw_core_port, u8 local_port,
			 struct net_device *dev, bool split, u32 split_group);
void mlxsw_core_port_fini(struct mlxsw_core_port *mlxsw_core_port);

int mlxsw_core_schedule_dw(struct delayed_work *dwork, unsigned long delay);

#define MLXSW_CONFIG_PROFILE_SWID_COUNT 8

struct mlxsw_swid_config {
	u8	used_type:1,
		used_properties:1;
	u8	type;
	u8	properties;
};

struct mlxsw_config_profile {
	u16	used_max_vepa_channels:1,
		used_max_lag:1,
		used_max_port_per_lag:1,
		used_max_mid:1,
		used_max_pgt:1,
		used_max_system_port:1,
		used_max_vlan_groups:1,
		used_max_regions:1,
		used_flood_tables:1,
		used_flood_mode:1,
		used_max_ib_mc:1,
		used_max_pkey:1,
		used_ar_sec:1,
		used_adaptive_routing_group_cap:1,
		used_kvd_sizes:1;
	u8	max_vepa_channels;
	u16	max_lag;
	u16	max_port_per_lag;
	u16	max_mid;
	u16	max_pgt;
	u16	max_system_port;
	u16	max_vlan_groups;
	u16	max_regions;
	u8	max_flood_tables;
	u8	max_vid_flood_tables;
	u8	flood_mode;
	u8	max_fid_offset_flood_tables;
	u16	fid_offset_flood_table_size;
	u8	max_fid_flood_tables;
	u16	fid_flood_table_size;
	u16	max_ib_mc;
	u16	max_pkey;
	u8	ar_sec;
	u16	adaptive_routing_group_cap;
	u8	arn;
	u32	kvd_linear_size;
	u32	kvd_hash_single_size;
	u32	kvd_hash_double_size;
	u8	resource_query_enable;
	struct mlxsw_swid_config swid_config[MLXSW_CONFIG_PROFILE_SWID_COUNT];
};

struct mlxsw_driver {
	struct list_head list;
	const char *kind;
	struct module *owner;
	size_t priv_size;
	int (*init)(struct mlxsw_core *mlxsw_core,
		    const struct mlxsw_bus_info *mlxsw_bus_info);
	void (*fini)(struct mlxsw_core *mlxsw_core);
	int (*port_split)(struct mlxsw_core *mlxsw_core, u8 local_port,
			  unsigned int count);
	int (*port_unsplit)(struct mlxsw_core *mlxsw_core, u8 local_port);
	int (*sb_pool_get)(struct mlxsw_core *mlxsw_core,
			   unsigned int sb_index, u16 pool_index,
			   struct devlink_sb_pool_info *pool_info);
	int (*sb_pool_set)(struct mlxsw_core *mlxsw_core,
			   unsigned int sb_index, u16 pool_index, u32 size,
			   enum devlink_sb_threshold_type threshold_type);
	int (*sb_port_pool_get)(struct mlxsw_core_port *mlxsw_core_port,
				unsigned int sb_index, u16 pool_index,
				u32 *p_threshold);
	int (*sb_port_pool_set)(struct mlxsw_core_port *mlxsw_core_port,
				unsigned int sb_index, u16 pool_index,
				u32 threshold);
	int (*sb_tc_pool_bind_get)(struct mlxsw_core_port *mlxsw_core_port,
				   unsigned int sb_index, u16 tc_index,
				   enum devlink_sb_pool_type pool_type,
				   u16 *p_pool_index, u32 *p_threshold);
	int (*sb_tc_pool_bind_set)(struct mlxsw_core_port *mlxsw_core_port,
				   unsigned int sb_index, u16 tc_index,
				   enum devlink_sb_pool_type pool_type,
				   u16 pool_index, u32 threshold);
	int (*sb_occ_snapshot)(struct mlxsw_core *mlxsw_core,
			       unsigned int sb_index);
	int (*sb_occ_max_clear)(struct mlxsw_core *mlxsw_core,
				unsigned int sb_index);
	int (*sb_occ_port_pool_get)(struct mlxsw_core_port *mlxsw_core_port,
				    unsigned int sb_index, u16 pool_index,
				    u32 *p_cur, u32 *p_max);
	int (*sb_occ_tc_port_bind_get)(struct mlxsw_core_port *mlxsw_core_port,
				       unsigned int sb_index, u16 tc_index,
				       enum devlink_sb_pool_type pool_type,
				       u32 *p_cur, u32 *p_max);
	void (*txhdr_construct)(struct sk_buff *skb,
				const struct mlxsw_tx_info *tx_info);
	u8 txhdr_len;
	const struct mlxsw_config_profile *profile;
};

struct mlxsw_resources {
	u8	max_span_valid:1;
	u8      max_span;
};

struct mlxsw_resources *mlxsw_core_resources_get(struct mlxsw_core *mlxsw_core);

struct mlxsw_bus {
	const char *kind;
	int (*init)(void *bus_priv, struct mlxsw_core *mlxsw_core,
		    const struct mlxsw_config_profile *profile,
		    struct mlxsw_resources *resources);
	void (*fini)(void *bus_priv);
	bool (*skb_transmit_busy)(void *bus_priv,
				  const struct mlxsw_tx_info *tx_info);
	int (*skb_transmit)(void *bus_priv, struct sk_buff *skb,
			    const struct mlxsw_tx_info *tx_info);
	int (*cmd_exec)(void *bus_priv, u16 opcode, u8 opcode_mod,
			u32 in_mod, bool out_mbox_direct,
			char *in_mbox, size_t in_mbox_size,
			char *out_mbox, size_t out_mbox_size,
			u8 *p_status);
};

struct mlxsw_bus_info {
	const char *device_kind;
	const char *device_name;
	struct device *dev;
	struct {
		u16 major;
		u16 minor;
		u16 subminor;
	} fw_rev;
	u8 vsd[MLXSW_CMD_BOARDINFO_VSD_LEN];
	u8 psid[MLXSW_CMD_BOARDINFO_PSID_LEN];
};

struct mlxsw_hwmon;

#ifdef CONFIG_MLXSW_CORE_HWMON

int mlxsw_hwmon_init(struct mlxsw_core *mlxsw_core,
		     const struct mlxsw_bus_info *mlxsw_bus_info,
		     struct mlxsw_hwmon **p_hwmon);
void mlxsw_hwmon_fini(struct mlxsw_hwmon *mlxsw_hwmon);

#else

static inline int mlxsw_hwmon_init(struct mlxsw_core *mlxsw_core,
				   const struct mlxsw_bus_info *mlxsw_bus_info,
				   struct mlxsw_hwmon **p_hwmon)
{
	return 0;
}

#endif

#endif
