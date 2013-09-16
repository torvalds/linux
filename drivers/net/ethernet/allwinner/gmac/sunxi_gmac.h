/*
 * sunxi_gmac.h: SUN6I Gigabit Ethernet Driver
 *
 * Copyright © 2012, Shuge
 *		Author: shuge
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 */

#ifndef __SUNXI_GMAC_H__
#define __SUNXI_GMAC_H__

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/module.h>
#include <linux/init.h>
#include <plat/sys_config.h>

#include "gmac_reg.h"
#include "gmac_desc.h"
#include "gmac_base.h"

#define GMAC_RESOURCE_NAME	"sunxi_gmac"

enum rx_frame_status { /* IPC status */
	good_frame = 0,
	discard_frame = 1,
	csum_none = 2,
	llc_snap = 4,
};

struct gmac_plat_data {
	int bus_id;
	int phy_addr;
	int phy_interface;
	unsigned int phy_mask;
	int probed_phy_irq;
	int clk_csr;

	unsigned int tx_coe;
	int bugged_jumbo;
	int force_sf_dma_mode;
	int pbl;
	struct gmac_mdio_bus_data *mdio_bus_data;
};

enum tx_dma_irq_status {
	tx_hard_error = 1,
	tx_hard_error_bump_tc = 2,
	handle_tx_rx = 3,
};

struct gmac_mdio_bus_data {
	int bus_id;
	int (*phy_reset)(void *priv);
	unsigned int phy_mask;
	int *irqs;
	int probed_phy_irq;
};

#if 0
/* DMA HW capabilities */
struct dma_features {
	unsigned int mbps_10_100;
	unsigned int mbps_1000;
	unsigned int half_duplex;
	unsigned int hash_filter;
	unsigned int multi_addr;
	unsigned int pcs;
	unsigned int sma_mdio;
	unsigned int pmt_remote_wake_up;
	unsigned int pmt_magic_frame;
	unsigned int rmon;
	/* IEEE 1588-2002*/
	unsigned int time_stamp;
	/* IEEE 1588-2008*/
	unsigned int atime_stamp;
	/* 802.3az - Energy-Efficient Ethernet (EEE) */
	unsigned int eee;
	unsigned int av;
	/* TX and RX csum */
	unsigned int tx_coe;
	unsigned int rx_coe_type1;
	unsigned int rx_coe_type2;
	unsigned int rxfifo_over_2048;
	/* TX and RX number of channels */
	unsigned int number_rx_channel;
	unsigned int number_tx_channel;
	/* Alternate (enhanced) DESC mode*/
	unsigned int enh_desc;
};
#endif

struct gmac_priv {

	/* Frequently used values are kept adjacent for cache effect */
	dma_desc_t *dma_tx ____cacheline_aligned;
	dma_addr_t dma_tx_phy;
	struct sk_buff **tx_skbuff;
	unsigned int cur_tx;
	unsigned int dirty_tx;
	unsigned int dma_tx_size;

	dma_desc_t *dma_rx ;
	dma_addr_t dma_rx_phy;
	unsigned int cur_rx;
	unsigned int dirty_rx;
	unsigned int dma_rx_size;
	struct sk_buff **rx_skbuff;
	dma_addr_t *rx_skbuff_dma;
	struct sk_buff_head rx_recycle;

	unsigned int dma_buf_sz;

	struct net_device *ndev;
	struct device *device;
	void __iomem *ioaddr;

#ifndef CONFIG_GMAC_SCRIPT_SYS
	void __iomem *gpiobase;
#else
	int gpio_cnt;
	unsigned int gpio_handle;

#endif
#ifndef CONFIG_GMAC_CLK_SYS
	void __iomem *clkbase;
#else
	struct clk *gmac_ahb_clk;
/*	struct clk *gmac_mod_clk;*/
#endif
	void __iomem *gmac_clk_reg;

	struct gmac_extra_stats xstats;
	struct napi_struct napi;

	int tx_coe;
	int rx_coe;
	int no_csum_insertion;
	unsigned int flow_ctrl;
	unsigned int pause;

	int oldlink;
	int speed;
	int oldduplex;

	struct mii_bus *mii;
	int mii_irq[PHY_MAX_ADDR];

	u32 msg_enable;
	spinlock_t lock;
	spinlock_t tx_lock;
	struct gmac_plat_data *plat;
	//struct dma_features dma_cap;
};

#ifdef CONFIG_PM
int gmac_suspend(struct net_device *ndev);
int gmac_resume(struct net_device *ndev);
int gmac_freeze(struct net_device *ndev);
int gmac_restore(struct net_device *ndev);
#endif /* CONFIG_PM */

int gmac_mdio_unregister(struct net_device *ndev);
int gmac_mdio_register(struct net_device *ndev);
int gmac_dvr_remove(struct net_device *ndev);
struct gmac_priv *gmac_dvr_probe(struct device *device,
								void __iomem *addr, int irqnum);

extern struct platform_driver gmac_driver;
extern struct platform_device gmac_device;
#endif //__SUNXI_GMAC_H__
