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

#define DRV_MODULE_VERSION	"Nov_2010"
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
	int tx_coalesce;

	struct dma_desc *dma_rx ;
	unsigned int cur_rx;
	unsigned int dirty_rx;
	struct sk_buff **rx_skbuff;
	dma_addr_t *rx_skbuff_dma;
	struct sk_buff_head rx_recycle;

	struct net_device *dev;
	dma_addr_t dma_rx_phy;
	unsigned int dma_rx_size;
	unsigned int dma_buf_sz;
	struct device *device;
	struct mac_device_info *hw;
	void __iomem *ioaddr;

	struct stmmac_extra_stats xstats;
	struct napi_struct napi;

	phy_interface_t phy_interface;
	int phy_addr;
	int phy_mask;
	int (*phy_reset) (void *priv);
	int rx_coe;
	int no_csum_insertion;

	int phy_irq;
	struct phy_device *phydev;
	int oldlink;
	int speed;
	int oldduplex;
	unsigned int flow_ctrl;
	unsigned int pause;
	struct mii_bus *mii;

	u32 msg_enable;
	spinlock_t lock;
	int wolopts;
	int wolenabled;
#ifdef CONFIG_STMMAC_TIMER
	struct stmmac_timer *tm;
#endif
#ifdef STMMAC_VLAN_TAG_USED
	struct vlan_group *vlgrp;
#endif
	struct plat_stmmacenet_data *plat;
};

extern int stmmac_mdio_unregister(struct net_device *ndev);
extern int stmmac_mdio_register(struct net_device *ndev);
extern void stmmac_set_ethtool_ops(struct net_device *netdev);
extern const struct stmmac_desc_ops enh_desc_ops;
extern const struct stmmac_desc_ops ndesc_ops;
