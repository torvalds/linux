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

#ifndef __STMMAC_H__
#define __STMMAC_H__

#define STMMAC_RESOURCE_NAME   "stmmaceth"
#define DRV_MODULE_VERSION	"March_2012"

#include <linux/clk.h>
#include <linux/stmmac.h>
#include <linux/phy.h>
#include <linux/pci.h>
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

	struct net_device *dev;
	dma_addr_t dma_rx_phy;
	unsigned int dma_rx_size;
	unsigned int dma_buf_sz;
	struct device *device;
	struct mac_device_info *hw;
	void __iomem *ioaddr;

	struct stmmac_extra_stats xstats;
	struct napi_struct napi;
	int no_csum_insertion;

	struct phy_device *phydev;
	int oldlink;
	int speed;
	int oldduplex;
	unsigned int flow_ctrl;
	unsigned int pause;
	struct mii_bus *mii;
	int mii_irq[PHY_MAX_ADDR];

	u32 msg_enable;
	spinlock_t lock;
	spinlock_t tx_lock;
	int wolopts;
	int wol_irq;
#ifdef CONFIG_STMMAC_TIMER
	struct stmmac_timer *tm;
#endif
	struct plat_stmmacenet_data *plat;
	struct stmmac_counters mmc;
	struct dma_features dma_cap;
	int hw_cap_support;
	struct clk *stmmac_clk;
	int clk_csr;
	int synopsys_id;
	struct timer_list eee_ctrl_timer;
	bool tx_path_in_lpi_mode;
	int lpi_irq;
	int eee_enabled;
	int eee_active;
	int tx_lpi_timer;
};

extern int phyaddr;

extern int stmmac_mdio_unregister(struct net_device *ndev);
extern int stmmac_mdio_register(struct net_device *ndev);
extern void stmmac_set_ethtool_ops(struct net_device *netdev);
extern const struct stmmac_desc_ops enh_desc_ops;
extern const struct stmmac_desc_ops ndesc_ops;
int stmmac_freeze(struct net_device *ndev);
int stmmac_restore(struct net_device *ndev);
int stmmac_resume(struct net_device *ndev);
int stmmac_suspend(struct net_device *ndev);
int stmmac_dvr_remove(struct net_device *ndev);
struct stmmac_priv *stmmac_dvr_probe(struct device *device,
				     struct plat_stmmacenet_data *plat_dat,
				     void __iomem *addr);
void stmmac_disable_eee_mode(struct stmmac_priv *priv);
bool stmmac_eee_init(struct stmmac_priv *priv);

#ifdef CONFIG_STMMAC_PLATFORM
extern struct platform_driver stmmac_pltfr_driver;
static inline int stmmac_register_platform(void)
{
	int err;

	err = platform_driver_register(&stmmac_pltfr_driver);
	if (err)
		pr_err("stmmac: failed to register the platform driver\n");

	return err;
}
static inline void stmmac_unregister_platform(void)
{
	platform_driver_register(&stmmac_pltfr_driver);
}
#else
static inline int stmmac_register_platform(void)
{
	pr_debug("stmmac: do not register the platf driver\n");

	return -EINVAL;
}
static inline void stmmac_unregister_platform(void)
{
}
#endif /* CONFIG_STMMAC_PLATFORM */

#ifdef CONFIG_STMMAC_PCI
extern struct pci_driver stmmac_pci_driver;
static inline int stmmac_register_pci(void)
{
	int err;

	err = pci_register_driver(&stmmac_pci_driver);
	if (err)
		pr_err("stmmac: failed to register the PCI driver\n");

	return err;
}
static inline void stmmac_unregister_pci(void)
{
	pci_unregister_driver(&stmmac_pci_driver);
}
#else
static inline int stmmac_register_pci(void)
{
	pr_debug("stmmac: do not register the PCI driver\n");

	return -EINVAL;
}
static inline void stmmac_unregister_pci(void)
{
}
#endif /* CONFIG_STMMAC_PCI */

#endif /* __STMMAC_H__ */
