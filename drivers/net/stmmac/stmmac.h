/*******************************************************************************
  Copyright (C) 2007-2009  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#define DRV_MODULE_VERSION	"Apr_2010"
#include <linux/platform_device.h>
#include <linux/stmmac.h>

#include "common.h"
#ifdef CONFIG_STMMAC_TIMER
#include "stmmac_timer.h"
#endif

struct stmmac_priv {
	/* Frequently used values are kept adjacent for cache effect */
	struct dma_desc *dma_tx ____cacheline_aligned;
	dma_addr_t dma_tx_phy;
	struct sk_buff **tx_skbuff;
	unsigned int cur_tx;
	unsigned int dirty_tx;
	unsigned int dma_tx_size;
	int tx_coe;
	int tx_coalesce;

	struct dma_desc *dma_rx ;
	unsigned int cur_rx;
	unsigned int dirty_rx;
	struct sk_buff **rx_skbuff;
	dma_addr_t *rx_skbuff_dma;
	struct sk_buff_head rx_recycle;

	struct net_device *dev;
	int is_gmac;
	dma_addr_t dma_rx_phy;
	unsigned int dma_rx_size;
	unsigned int dma_buf_sz;
	struct device *device;
	struct mac_device_info *hw;
	void __iomem *ioaddr;

	struct stmmac_extra_stats xstats;
	struct napi_struct napi;

	phy_interface_t phy_interface;
	int pbl;
	int bus_id;
	int phy_addr;
	int phy_mask;
	int (*phy_reset) (void *priv);
	void (*fix_mac_speed) (void *priv, unsigned int speed);
	void (*bus_setup)(void __iomem *ioaddr);
	void *bsp_priv;

	int phy_irq;
	struct phy_device *phydev;
	int oldlink;
	int speed;
	int oldduplex;
	unsigned int flow_ctrl;
	unsigned int pause;
	struct mii_bus *mii;
	int mii_clk_csr;

	u32 msg_enable;
	spinlock_t lock;
	int wolopts;
	int wolenabled;
	int shutdown;
#ifdef CONFIG_STMMAC_TIMER
	struct stmmac_timer *tm;
#endif
#ifdef STMMAC_VLAN_TAG_USED
	struct vlan_group *vlgrp;
#endif
	int enh_desc;
	int rx_coe;
	int bugged_jumbo;
	int no_csum_insertion;
};

#ifdef CONFIG_STM_DRIVERS
#include <linux/stm/pad.h>
static inline int stmmac_claim_resource(struct platform_device *pdev)
{
	int ret = 0;
	struct plat_stmmacenet_data *plat_dat = pdev->dev.platform_data;

	/* Pad routing setup */
	if (IS_ERR(devm_stm_pad_claim(&pdev->dev, plat_dat->pad_config,
			dev_name(&pdev->dev)))) {
		printk(KERN_ERR "%s: Failed to request pads!\n", __func__);
		ret = -ENODEV;
	}
	return ret;
}
#else
static inline int stmmac_claim_resource(struct platform_device *pdev)
{
	return 0;
}
#endif

extern int stmmac_mdio_unregister(struct net_device *ndev);
extern int stmmac_mdio_register(struct net_device *ndev);
extern void stmmac_set_ethtool_ops(struct net_device *netdev);
extern const struct stmmac_desc_ops enh_desc_ops;
extern const struct stmmac_desc_ops ndesc_ops;
