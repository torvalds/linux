/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef __HBG_COMMON_H
#define __HBG_COMMON_H

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include "hbg_reg.h"

#define HBG_STATUS_DISABLE		0x0
#define HBG_STATUS_ENABLE		0x1
#define HBG_RX_SKIP1			0x00
#define HBG_RX_SKIP2			0x01
#define HBG_VECTOR_NUM			4
#define HBG_PCU_CACHE_LINE_SIZE		32
#define HBG_TX_TIMEOUT_BUF_LEN		1024
#define HBG_RX_DESCR			0x01

#define HBG_PACKET_HEAD_SIZE	((HBG_RX_SKIP1 + HBG_RX_SKIP2 + \
				  HBG_RX_DESCR) * HBG_PCU_CACHE_LINE_SIZE)

enum hbg_dir {
	HBG_DIR_TX = 1 << 0,
	HBG_DIR_RX = 1 << 1,
	HBG_DIR_TX_RX = HBG_DIR_TX | HBG_DIR_RX,
};

enum hbg_tx_state {
	HBG_TX_STATE_COMPLETE = 0, /* clear state, must fix to 0 */
	HBG_TX_STATE_START,
};

enum hbg_nic_state {
	HBG_NIC_STATE_EVENT_HANDLING = 0,
	HBG_NIC_STATE_RESETTING,
	HBG_NIC_STATE_RESET_FAIL,
};

enum hbg_reset_type {
	HBG_RESET_TYPE_NONE = 0,
	HBG_RESET_TYPE_FLR,
	HBG_RESET_TYPE_FUNCTION,
};

struct hbg_buffer {
	u32 state;
	dma_addr_t state_dma;

	struct sk_buff *skb;
	dma_addr_t skb_dma;
	u32 skb_len;

	enum hbg_dir dir;
	struct hbg_ring *ring;
	struct hbg_priv *priv;
};

struct hbg_ring {
	struct hbg_buffer *queue;
	dma_addr_t queue_dma;

	union {
		u32 head;
		u32 ntc;
	};
	union {
		u32 tail;
		u32 ntu;
	};
	u32 len;

	enum hbg_dir dir;
	struct hbg_priv *priv;
	struct napi_struct napi;
	char *tout_log_buf; /* tx timeout log buffer */
};

enum hbg_hw_event_type {
	HBG_HW_EVENT_NONE = 0,
	HBG_HW_EVENT_INIT, /* driver is loading */
	HBG_HW_EVENT_RESET,
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
	u32 uc_mac_num;

	u32 max_frame_len;
	u32 rx_buf_size;
};

struct hbg_irq_info {
	const char *name;
	u32 mask;
	bool re_enable;
	bool need_print;
	u64 count;

	void (*irq_handle)(struct hbg_priv *priv, struct hbg_irq_info *info);
};

struct hbg_vector {
	char name[HBG_VECTOR_NUM][32];
	struct hbg_irq_info *info_array;
	u32 info_array_len;
};

struct hbg_mac {
	struct mii_bus *mdio_bus;
	struct phy_device *phydev;
	u8 phy_addr;

	u32 speed;
	u32 duplex;
	u32 autoneg;
	u32 link_status;
	u32 pause_autoneg;
};

struct hbg_mac_table_entry {
	u8 addr[ETH_ALEN];
};

struct hbg_mac_filter {
	struct hbg_mac_table_entry *mac_table;
	u32 table_max_len;
	bool enabled;
};

/* saved for restore after rest */
struct hbg_user_def {
	struct ethtool_pauseparam pause_param;
};

struct hbg_priv {
	struct net_device *netdev;
	struct pci_dev *pdev;
	u8 __iomem *io_base;
	struct hbg_dev_specs dev_specs;
	unsigned long state;
	struct hbg_mac mac;
	struct hbg_vector vectors;
	struct hbg_ring tx_ring;
	struct hbg_ring rx_ring;
	struct hbg_mac_filter filter;
	enum hbg_reset_type reset_type;
	struct hbg_user_def user_def;
};

#endif
