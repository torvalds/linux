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
#define DRV_MODULE_VERSION	"March_2013"

#include <linux/clk.h>
#include <linux/stmmac.h>
#include <linux/phy.h>
#include <linux/pci.h>
#include "common.h"
#include <linux/ptp_clock_kernel.h>

struct stmmac_priv {
	/* Frequently used values are kept adjacent for cache effect */
	struct dma_extended_desc *dma_etx ____cacheline_aligned_in_smp;
	struct dma_desc *dma_tx;
	struct sk_buff **tx_skbuff;
	unsigned int cur_tx;
	unsigned int dirty_tx;
	unsigned int dma_tx_size;
	u32 tx_count_frames;
	u32 tx_coal_frames;
	u32 tx_coal_timer;
	dma_addr_t *tx_skbuff_dma;
	dma_addr_t dma_tx_phy;
	int tx_coalesce;
	int hwts_tx_en;
	spinlock_t tx_lock;
	bool tx_path_in_lpi_mode;
	struct timer_list txtimer;

	struct dma_desc *dma_rx	____cacheline_aligned_in_smp;
	struct dma_extended_desc *dma_erx;
	struct sk_buff **rx_skbuff;
	unsigned int cur_rx;
	unsigned int dirty_rx;
	unsigned int dma_rx_size;
	unsigned int dma_buf_sz;
	u32 rx_riwt;
	int hwts_rx_en;
	dma_addr_t *rx_skbuff_dma;
	dma_addr_t dma_rx_phy;

	struct napi_struct napi ____cacheline_aligned_in_smp;

	void __iomem *ioaddr;
	struct net_device *dev;
	struct device *device;
	struct mac_device_info *hw;
	int no_csum_insertion;
	spinlock_t lock;

	struct phy_device *phydev ____cacheline_aligned_in_smp;
	int oldlink;
	int speed;
	int oldduplex;
	unsigned int flow_ctrl;
	unsigned int pause;
	struct mii_bus *mii;
	int mii_irq[PHY_MAX_ADDR];

	struct stmmac_extra_stats xstats ____cacheline_aligned_in_smp;
	struct plat_stmmacenet_data *plat;
	struct dma_features dma_cap;
	struct stmmac_counters mmc;
	int hw_cap_support;
	int synopsys_id;
	u32 msg_enable;
	int wolopts;
	int wol_irq;
	struct clk *stmmac_clk;
	int clk_csr;
	struct timer_list eee_ctrl_timer;
	int lpi_irq;
	int eee_enabled;
	int eee_active;
	int tx_lpi_timer;
	int pcs;
	unsigned int mode;
	int extend_desc;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_ops;
	unsigned int default_addend;
	u32 adv_ts;
	int use_riwt;
	spinlock_t ptp_lock;
	bool mdio_registered;
};

enum {
    RK3288_GMAC,
    RK312X_GMAC
};

struct bsp_priv {
	bool power_ctrl_by_pmu;
	char pmu_regulator[32];
	int pmu_enable_level;
	int power_io;
	int power_io_level;
	int reset_io;
	int reset_io_level;
	int phyirq_io;
	int phyirq_io_level;
	int phy_iface;
	bool clock_input;
	int chip;
	int tx_delay;
	int rx_delay;

	struct clk *clk_mac;
	struct clk *clk_mac_pll;
	struct clk *gmac_clkin;
	struct clk *mac_clk_rx;
	struct clk *mac_clk_tx;
	struct clk *clk_mac_ref;
	struct clk *clk_mac_refout;
	struct clk *aclk_mac;
	struct clk *pclk_mac;
	bool clk_enable;

	int (*phy_power_on)(bool enable);
	int (*gmac_clk_enable)(bool enable);
};

extern int phyaddr;

extern int stmmac_mdio_unregister(struct net_device *ndev);
extern int stmmac_mdio_register(struct net_device *ndev);
extern void stmmac_set_ethtool_ops(struct net_device *netdev);
extern const struct stmmac_desc_ops enh_desc_ops;
extern const struct stmmac_desc_ops ndesc_ops;
extern const struct stmmac_hwtimestamp stmmac_ptp;
extern int stmmac_ptp_register(struct stmmac_priv *priv);
extern void stmmac_ptp_unregister(struct stmmac_priv *priv);
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
	platform_driver_unregister(&stmmac_pltfr_driver);
}

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

	return 0;
}

static inline void stmmac_unregister_pci(void)
{
}
#endif /* CONFIG_STMMAC_PCI */

#endif /* __STMMAC_H__ */
