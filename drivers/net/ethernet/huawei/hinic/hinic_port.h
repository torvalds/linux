/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_PORT_H
#define HINIC_PORT_H

#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/bitops.h>

#include "hinic_dev.h"

#define HINIC_RSS_KEY_SIZE	40
#define HINIC_RSS_INDIR_SIZE	256

enum hinic_rx_mode {
	HINIC_RX_MODE_UC        = BIT(0),
	HINIC_RX_MODE_MC        = BIT(1),
	HINIC_RX_MODE_BC        = BIT(2),
	HINIC_RX_MODE_MC_ALL    = BIT(3),
	HINIC_RX_MODE_PROMISC   = BIT(4),
};

enum hinic_port_link_state {
	HINIC_LINK_STATE_DOWN,
	HINIC_LINK_STATE_UP,
};

enum hinic_port_state {
	HINIC_PORT_DISABLE      = 0,
	HINIC_PORT_ENABLE       = 3,
};

enum hinic_func_port_state {
	HINIC_FUNC_PORT_DISABLE = 0,
	HINIC_FUNC_PORT_ENABLE  = 2,
};

enum hinic_autoneg_cap {
	HINIC_AUTONEG_UNSUPPORTED,
	HINIC_AUTONEG_SUPPORTED,
};

enum hinic_autoneg_state {
	HINIC_AUTONEG_DISABLED,
	HINIC_AUTONEG_ACTIVE,
};

enum hinic_duplex {
	HINIC_DUPLEX_HALF,
	HINIC_DUPLEX_FULL,
};

enum hinic_speed {
	HINIC_SPEED_10MB_LINK = 0,
	HINIC_SPEED_100MB_LINK,
	HINIC_SPEED_1000MB_LINK,
	HINIC_SPEED_10GB_LINK,
	HINIC_SPEED_25GB_LINK,
	HINIC_SPEED_40GB_LINK,
	HINIC_SPEED_100GB_LINK,

	HINIC_SPEED_UNKNOWN = 0xFF,
};

enum hinic_tso_state {
	HINIC_TSO_DISABLE = 0,
	HINIC_TSO_ENABLE  = 1,
};

struct hinic_port_mac_cmd {
	u8              status;
	u8              version;
	u8              rsvd0[6];

	u16             func_idx;
	u16             vlan_id;
	u16             rsvd1;
	unsigned char   mac[ETH_ALEN];
};

struct hinic_port_mtu_cmd {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u16     rsvd1;
	u32     mtu;
};

struct hinic_port_vlan_cmd {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u16     vlan_id;
};

struct hinic_port_rx_mode_cmd {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u16     rsvd;
	u32     rx_mode;
};

struct hinic_port_link_cmd {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u8      state;
	u8      rsvd1;
};

struct hinic_port_state_cmd {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u8      state;
	u8      rsvd1[3];
};

struct hinic_port_link_status {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     rsvd1;
	u8      link;
	u8      rsvd2;
};

struct hinic_port_func_state_cmd {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u16     rsvd1;
	u8      state;
	u8      rsvd2[3];
};

struct hinic_port_cap {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u16     rsvd1;
	u8      port_type;
	u8      autoneg_cap;
	u8      autoneg_state;
	u8      duplex;
	u8      speed;
	u8      rsvd2[3];
};

struct hinic_tso_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u8	tso_en;
	u8	resv2[3];
};

struct hinic_checksum_offload {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u32	rx_csum_offload;
};

struct hinic_rq_num {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1[33];
	u32	num_rqs;
	u32	rq_depth;
};

struct hinic_lro_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u8	lro_ipv4_en;
	u8	lro_ipv6_en;
	u8	lro_max_wqe_num;
	u8	resv2[13];
};

struct hinic_lro_timer {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	type;   /* 0: set timer value, 1: get timer value */
	u8	enable; /* when set lro time, enable should be 1 */
	u16	rsvd1;
	u32	timer;
};

struct hinic_rss_template_mgmt {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	cmd;
	u8	template_id;
	u8	rsvd1[4];
};

struct hinic_rss_context_tbl {
	u32 group_index;
	u32 offset;
	u32 size;
	u32 rsvd;
	u32 ctx;
};

struct hinic_rss_indirect_tbl {
	u32 group_index;
	u32 offset;
	u32 size;
	u32 rsvd;
	u8 entry[HINIC_RSS_INDIR_SIZE];
};

struct hinic_rss_key {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	template_id;
	u8	rsvd1;
	u8	key[HINIC_RSS_KEY_SIZE];
};

struct hinic_rss_engine_type {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	template_id;
	u8	hash_engine;
	u8	rsvd1[4];
};

struct hinic_rss_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	rss_en;
	u8	template_id;
	u8	rq_priority_number;
	u8	rsvd1[11];
};

int hinic_port_add_mac(struct hinic_dev *nic_dev, const u8 *addr,
		       u16 vlan_id);

int hinic_port_del_mac(struct hinic_dev *nic_dev, const u8 *addr,
		       u16 vlan_id);

int hinic_port_get_mac(struct hinic_dev *nic_dev, u8 *addr);

int hinic_port_set_mtu(struct hinic_dev *nic_dev, int new_mtu);

int hinic_port_add_vlan(struct hinic_dev *nic_dev, u16 vlan_id);

int hinic_port_del_vlan(struct hinic_dev *nic_dev, u16 vlan_id);

int hinic_port_set_rx_mode(struct hinic_dev *nic_dev, u32 rx_mode);

int hinic_port_link_state(struct hinic_dev *nic_dev,
			  enum hinic_port_link_state *link_state);

int hinic_port_set_state(struct hinic_dev *nic_dev,
			 enum hinic_port_state state);

int hinic_port_set_func_state(struct hinic_dev *nic_dev,
			      enum hinic_func_port_state state);

int hinic_port_get_cap(struct hinic_dev *nic_dev,
		       struct hinic_port_cap *port_cap);

int hinic_set_max_qnum(struct hinic_dev *nic_dev, u8 num_rqs);

int hinic_port_set_tso(struct hinic_dev *nic_dev, enum hinic_tso_state state);

int hinic_set_rx_csum_offload(struct hinic_dev *nic_dev, u32 en);

int hinic_set_rx_lro_state(struct hinic_dev *nic_dev, u8 lro_en,
			   u32 lro_timer, u32 wqe_num);

int hinic_set_rss_type(struct hinic_dev *nic_dev, u32 tmpl_idx,
		       struct hinic_rss_type rss_type);

int hinic_rss_set_indir_tbl(struct hinic_dev *nic_dev, u32 tmpl_idx,
			    const u32 *indir_table);

int hinic_rss_set_template_tbl(struct hinic_dev *nic_dev, u32 template_id,
			       const u8 *temp);

int hinic_rss_set_hash_engine(struct hinic_dev *nic_dev, u8 template_id,
			      u8 type);

int hinic_rss_cfg(struct hinic_dev *nic_dev, u8 rss_en, u8 template_id);

int hinic_rss_template_alloc(struct hinic_dev *nic_dev, u8 *tmpl_idx);

int hinic_rss_template_free(struct hinic_dev *nic_dev, u8 tmpl_idx);

void hinic_set_ethtool_ops(struct net_device *netdev);
#endif
