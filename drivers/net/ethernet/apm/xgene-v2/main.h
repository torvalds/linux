/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Applied Micro X-Gene SoC Ethernet v2 Driver
 *
 * Copyright (c) 2017, Applied Micro Circuits Corporation
 * Author(s): Iyappan Subramanian <isubramanian@apm.com>
 *	      Keyur Chudgar <kchudgar@apm.com>
 */

#ifndef __XGENE_ENET_V2_MAIN_H__
#define __XGENE_ENET_V2_MAIN_H__

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/efi.h>
#include <linux/if_vlan.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/prefetch.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <net/ip.h>
#include "mac.h"
#include "enet.h"
#include "ring.h"
#include "ethtool.h"

#define XGENE_ENET_STD_MTU	1536
#define XGENE_ENET_MIN_FRAME	60
#define IRQ_ID_SIZE             16

struct xge_resource {
	void __iomem *base_addr;
	int phy_mode;
	u32 irq;
};

struct xge_stats {
	u64 tx_packets;
	u64 tx_bytes;
	u64 rx_packets;
	u64 rx_bytes;
	u64 rx_errors;
};

/* ethernet private data */
struct xge_pdata {
	struct xge_resource resources;
	struct xge_desc_ring *tx_ring;
	struct xge_desc_ring *rx_ring;
	struct platform_device *pdev;
	char irq_name[IRQ_ID_SIZE];
	struct mii_bus *mdio_bus;
	struct net_device *ndev;
	struct napi_struct napi;
	struct xge_stats stats;
	int phy_speed;
	u8 nbufs;
};

int xge_mdio_config(struct net_device *ndev);
void xge_mdio_remove(struct net_device *ndev);

#endif /* __XGENE_ENET_V2_MAIN_H__ */
