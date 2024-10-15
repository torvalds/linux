/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef __HBG_COMMON_H
#define __HBG_COMMON_H

#include <linux/netdevice.h>
#include <linux/pci.h>

enum hbg_nic_state {
	HBG_NIC_STATE_EVENT_HANDLING = 0,
};

enum hbg_hw_event_type {
	HBG_HW_EVENT_NONE = 0,
	HBG_HW_EVENT_INIT, /* driver is loading */
};

struct hbg_dev_specs {
	u32 mac_id;
	struct sockaddr mac_addr;
	u32 phy_addr;
	u32 mdio_frequency;
	u32 rx_fifo_num;
	u32 tx_fifo_num;
	u32 vlan_layers;
	u32 max_mtu;
	u32 min_mtu;

	u32 max_frame_len;
	u32 rx_buf_size;
};

struct hbg_priv {
	struct net_device *netdev;
	struct pci_dev *pdev;
	u8 __iomem *io_base;
	struct hbg_dev_specs dev_specs;
	unsigned long state;
};

#endif
