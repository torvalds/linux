/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#ifndef HINIC_PORT_H
#define HINIC_PORT_H

#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/bitops.h>

#include "hinic_dev.h"

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

int hinic_port_set_tso(struct hinic_dev *nic_dev, enum hinic_tso_state state);

int hinic_set_rx_csum_offload(struct hinic_dev *nic_dev, u32 en);
#endif
