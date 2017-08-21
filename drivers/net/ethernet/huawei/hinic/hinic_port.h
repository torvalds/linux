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

#endif
