// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Loongson Corporation
 */

#include <linux/clk-provider.h>
#include <linux/pci.h>
#include <linux/dmi.h>
#include <linux/device.h>
#include <linux/of_irq.h>
#include "stmmac.h"
#include "dwmac_dma.h"
#include "dwmac1000.h"

#define DRIVER_NAME "dwmac-loongson-pci"

/* Normal Loongson Tx Summary */
#define DMA_INTR_ENA_NIE_TX_LOONGSON	0x00040000
/* Normal Loongson Rx Summary */
#define DMA_INTR_ENA_NIE_RX_LOONGSON	0x00020000

#define DMA_INTR_NORMAL_LOONGSON	(DMA_INTR_ENA_NIE_TX_LOONGSON | \
					 DMA_INTR_ENA_NIE_RX_LOONGSON | \
					 DMA_INTR_ENA_RIE | DMA_INTR_ENA_TIE)

/* Abnormal Loongson Tx Summary */
#define DMA_INTR_ENA_AIE_TX_LOONGSON	0x00010000
/* Abnormal Loongson Rx Summary */
#define DMA_INTR_ENA_AIE_RX_LOONGSON	0x00008000

#define DMA_INTR_ABNORMAL_LOONGSON	(DMA_INTR_ENA_AIE_TX_LOONGSON | \
					 DMA_INTR_ENA_AIE_RX_LOONGSON | \
					 DMA_INTR_ENA_FBE | DMA_INTR_ENA_UNE)

#define DMA_INTR_DEFAULT_MASK_LOONGSON	(DMA_INTR_NORMAL_LOONGSON | \
					 DMA_INTR_ABNORMAL_LOONGSON)

/* Normal Loongson Tx Interrupt Summary */
#define DMA_STATUS_NIS_TX_LOONGSON	0x00040000
/* Normal Loongson Rx Interrupt Summary */
#define DMA_STATUS_NIS_RX_LOONGSON	0x00020000

/* Abnormal Loongson Tx Interrupt Summary */
#define DMA_STATUS_AIS_TX_LOONGSON	0x00010000
/* Abnormal Loongson Rx Interrupt Summary */
#define DMA_STATUS_AIS_RX_LOONGSON	0x00008000

/* Fatal Loongson Tx Bus Error Interrupt */
#define DMA_STATUS_FBI_TX_LOONGSON	0x00002000
/* Fatal Loongson Rx Bus Error Interrupt */
#define DMA_STATUS_FBI_RX_LOONGSON	0x00001000

#define DMA_STATUS_MSK_COMMON_LOONGSON	(DMA_STATUS_NIS_TX_LOONGSON | \
					 DMA_STATUS_NIS_RX_LOONGSON | \
					 DMA_STATUS_AIS_TX_LOONGSON | \
					 DMA_STATUS_AIS_RX_LOONGSON | \
					 DMA_STATUS_FBI_TX_LOONGSON | \
					 DMA_STATUS_FBI_RX_LOONGSON)

#define DMA_STATUS_MSK_RX_LOONGSON	(DMA_STATUS_ERI | DMA_STATUS_RWT | \
					 DMA_STATUS_RPS | DMA_STATUS_RU  | \
					 DMA_STATUS_RI  | DMA_STATUS_OVF | \
					 DMA_STATUS_MSK_COMMON_LOONGSON)

#define DMA_STATUS_MSK_TX_LOONGSON	(DMA_STATUS_ETI | DMA_STATUS_UNF | \
					 DMA_STATUS_TJT | DMA_STATUS_TU  | \
					 DMA_STATUS_TPS | DMA_STATUS_TI  | \
					 DMA_STATUS_MSK_COMMON_LOONGSON)

#define PCI_DEVICE_ID_LOONGSON_GMAC1	0x7a03
#define PCI_DEVICE_ID_LOONGSON_GMAC2	0x7a23
#define PCI_DEVICE_ID_LOONGSON_GNET	0x7a13
#define DWMAC_CORE_MULTICHAN_V1	0x10	/* Loongson custom ID 0x10 */
#define DWMAC_CORE_MULTICHAN_V2	0x12	/* Loongson custom ID 0x12 */

struct loongson_data {
	u32 multichan;
	u32 loongson_id;
	struct device *dev;
};

struct stmmac_pci_info {
	int (*setup)(struct pci_dev *pdev, struct plat_stmmacenet_data *plat);
};

static void loongson_default_data(struct pci_dev *pdev,
				  struct plat_stmmacenet_data *plat)
{
	struct loongson_data *ld = plat->bsp_priv;

	/* Get bus_id, this can be overwritten later */
	plat->bus_id = pci_dev_id(pdev);

	/* clk_csr_i = 20-35MHz & MDC = clk_csr_i/16 */
	plat->clk_csr = STMMAC_CSR_20_35M;
	plat->has_gmac = 1;
	plat->force_sf_dma_mode = 1;

	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = 256;

	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 1;

	/* Set the maxmtu to a default of JUMBO_LEN */
	plat->maxmtu = JUMBO_LEN;

	/* Disable Priority config by default */
	plat->tx_queues_cfg[0].use_prio = false;
	plat->rx_queues_cfg[0].use_prio = false;

	/* Disable RX queues routing by default */
	plat->rx_queues_cfg[0].pkt_route = 0x0;

	plat->clk_ref_rate = 125000000;
	plat->clk_ptp_rate = 125000000;

	/* Default to phy auto-detection */
	plat->phy_addr = -1;

	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;

	switch (ld->loongson_id) {
	case DWMAC_CORE_MULTICHAN_V1:
		ld->multichan = 1;
		plat->rx_queues_to_use = 8;
		plat->tx_queues_to_use = 8;

		/* Only channel 0 supports checksum,
		 * so turn off checksum to enable multiple channels.
		 */
		for (int i = 1; i < 8; i++)
			plat->tx_queues_cfg[i].coe_unsupported = 1;

		break;
	case DWMAC_CORE_MULTICHAN_V2:
		ld->multichan = 1;
		plat->rx_queues_to_use = 4;
		plat->tx_queues_to_use = 4;
		break;
	default:
		ld->multichan = 0;
		plat->tx_queues_to_use = 1;
		plat->rx_queues_to_use = 1;
		break;
	}
}

static int loongson_gmac_data(struct pci_dev *pdev,
			      struct plat_stmmacenet_data *plat)
{
	loongson_default_data(pdev, plat);

	plat->phy_interface = PHY_INTERFACE_MODE_RGMII_ID;

	return 0;
}

static struct stmmac_pci_info loongson_gmac_pci_info = {
	.setup = loongson_gmac_data,
};

static void loongson_gnet_fix_speed(void *priv, int speed, unsigned int mode)
{
	struct loongson_data *ld = (struct loongson_data *)priv;
	struct net_device *ndev = dev_get_drvdata(ld->dev);
	struct stmmac_priv *ptr = netdev_priv(ndev);

	/* The integrated PHY has a weird problem with switching from the low
	 * speeds to 1000Mbps mode. The speedup procedure requires the PHY-link
	 * re-negotiation.
	 */
	if (speed == SPEED_1000) {
		if (readl(ptr->ioaddr + MAC_CTRL_REG) &
		    GMAC_CONTROL_PS)
			/* Word around hardware bug, restart autoneg */
			phy_restart_aneg(ndev->phydev);
	}
}

static int loongson_gnet_data(struct pci_dev *pdev,
			      struct plat_stmmacenet_data *plat)
{
	loongson_default_data(pdev, plat);

	plat->phy_interface = PHY_INTERFACE_MODE_GMII;
	plat->mdio_bus_data->phy_mask = ~(u32)BIT(2);
	plat->fix_mac_speed = loongson_gnet_fix_speed;

	return 0;
}

static struct stmmac_pci_info loongson_gnet_pci_info = {
	.setup = loongson_gnet_data,
};

static void loongson_dwmac_dma_init_channel(struct stmmac_priv *priv,
					    void __iomem *ioaddr,
					    struct stmmac_dma_cfg *dma_cfg,
					    u32 chan)
{
	int txpbl = dma_cfg->txpbl ?: dma_cfg->pbl;
	int rxpbl = dma_cfg->rxpbl ?: dma_cfg->pbl;
	u32 value;

	value = readl(ioaddr + DMA_CHAN_BUS_MODE(chan));

	if (dma_cfg->pblx8)
		value |= DMA_BUS_MODE_MAXPBL;

	value |= DMA_BUS_MODE_USP;
	value &= ~(DMA_BUS_MODE_PBL_MASK | DMA_BUS_MODE_RPBL_MASK);
	value |= (txpbl << DMA_BUS_MODE_PBL_SHIFT);
	value |= (rxpbl << DMA_BUS_MODE_RPBL_SHIFT);

	/* Set the Fixed burst mode */
	if (dma_cfg->fixed_burst)
		value |= DMA_BUS_MODE_FB;

	/* Mixed Burst has no effect when fb is set */
	if (dma_cfg->mixed_burst)
		value |= DMA_BUS_MODE_MB;

	if (dma_cfg->atds)
		value |= DMA_BUS_MODE_ATDS;

	if (dma_cfg->aal)
		value |= DMA_BUS_MODE_AAL;

	writel(value, ioaddr + DMA_CHAN_BUS_MODE(chan));

	/* Mask interrupts by writing to CSR7 */
	writel(DMA_INTR_DEFAULT_MASK_LOONGSON, ioaddr +
	       DMA_CHAN_INTR_ENA(chan));
}

static int loongson_dwmac_dma_interrupt(struct stmmac_priv *priv,
					void __iomem *ioaddr,
					struct stmmac_extra_stats *x,
					u32 chan, u32 dir)
{
	struct stmmac_pcpu_stats *stats = this_cpu_ptr(priv->xstats.pcpu_stats);
	u32 abnor_intr_status;
	u32 nor_intr_status;
	u32 fb_intr_status;
	u32 intr_status;
	int ret = 0;

	/* read the status register (CSR5) */
	intr_status = readl(ioaddr + DMA_CHAN_STATUS(chan));

	if (dir == DMA_DIR_RX)
		intr_status &= DMA_STATUS_MSK_RX_LOONGSON;
	else if (dir == DMA_DIR_TX)
		intr_status &= DMA_STATUS_MSK_TX_LOONGSON;

	nor_intr_status = intr_status & (DMA_STATUS_NIS_TX_LOONGSON |
		DMA_STATUS_NIS_RX_LOONGSON);
	abnor_intr_status = intr_status & (DMA_STATUS_AIS_TX_LOONGSON |
		DMA_STATUS_AIS_RX_LOONGSON);
	fb_intr_status = intr_status & (DMA_STATUS_FBI_TX_LOONGSON |
		DMA_STATUS_FBI_RX_LOONGSON);

	/* ABNORMAL interrupts */
	if (unlikely(abnor_intr_status)) {
		if (unlikely(intr_status & DMA_STATUS_UNF)) {
			ret = tx_hard_error_bump_tc;
			x->tx_undeflow_irq++;
		}
		if (unlikely(intr_status & DMA_STATUS_TJT))
			x->tx_jabber_irq++;
		if (unlikely(intr_status & DMA_STATUS_OVF))
			x->rx_overflow_irq++;
		if (unlikely(intr_status & DMA_STATUS_RU))
			x->rx_buf_unav_irq++;
		if (unlikely(intr_status & DMA_STATUS_RPS))
			x->rx_process_stopped_irq++;
		if (unlikely(intr_status & DMA_STATUS_RWT))
			x->rx_watchdog_irq++;
		if (unlikely(intr_status & DMA_STATUS_ETI))
			x->tx_early_irq++;
		if (unlikely(intr_status & DMA_STATUS_TPS)) {
			x->tx_process_stopped_irq++;
			ret = tx_hard_error;
		}
		if (unlikely(fb_intr_status)) {
			x->fatal_bus_error_irq++;
			ret = tx_hard_error;
		}
	}
	/* TX/RX NORMAL interrupts */
	if (likely(nor_intr_status)) {
		if (likely(intr_status & DMA_STATUS_RI)) {
			u32 value = readl(ioaddr + DMA_INTR_ENA);
			/* to schedule NAPI on real RIE event. */
			if (likely(value & DMA_INTR_ENA_RIE)) {
				u64_stats_update_begin(&stats->syncp);
				u64_stats_inc(&stats->rx_normal_irq_n[chan]);
				u64_stats_update_end(&stats->syncp);
				ret |= handle_rx;
			}
		}
		if (likely(intr_status & DMA_STATUS_TI)) {
			u64_stats_update_begin(&stats->syncp);
			u64_stats_inc(&stats->tx_normal_irq_n[chan]);
			u64_stats_update_end(&stats->syncp);
			ret |= handle_tx;
		}
		if (unlikely(intr_status & DMA_STATUS_ERI))
			x->rx_early_irq++;
	}
	/* Optional hardware blocks, interrupts should be disabled */
	if (unlikely(intr_status &
		     (DMA_STATUS_GPI | DMA_STATUS_GMI | DMA_STATUS_GLI)))
		pr_warn("%s: unexpected status %08x\n", __func__, intr_status);

	/* Clear the interrupt by writing a logic 1 to the CSR5[19-0] */
	writel((intr_status & 0x7ffff), ioaddr + DMA_CHAN_STATUS(chan));

	return ret;
}

static struct mac_device_info *loongson_dwmac_setup(void *apriv)
{
	struct stmmac_priv *priv = apriv;
	struct mac_device_info *mac;
	struct stmmac_dma_ops *dma;
	struct loongson_data *ld;
	struct pci_dev *pdev;

	ld = priv->plat->bsp_priv;
	pdev = to_pci_dev(priv->device);

	mac = devm_kzalloc(priv->device, sizeof(*mac), GFP_KERNEL);
	if (!mac)
		return NULL;

	dma = devm_kzalloc(priv->device, sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return NULL;

	/* The Loongson GMAC and GNET devices are based on the DW GMAC
	 * v3.50a and v3.73a IP-cores. But the HW designers have changed
	 * the GMAC_VERSION.SNPSVER field to the custom 0x10/0x12 value
	 * on the network controllers with the multi-channels feature
	 * available to emphasize the differences: multiple DMA-channels,
	 * AV feature and GMAC_INT_STATUS CSR flags layout. Get back the
	 * original value so the correct HW-interface would be selected.
	 */
	if (ld->multichan) {
		priv->synopsys_id = DWMAC_CORE_3_70;
		*dma = dwmac1000_dma_ops;
		dma->init_chan = loongson_dwmac_dma_init_channel;
		dma->dma_interrupt = loongson_dwmac_dma_interrupt;
		mac->dma = dma;
	}

	priv->dev->priv_flags |= IFF_UNICAST_FLT;

	/* Pre-initialize the respective "mac" fields as it's done in
	 * dwmac1000_setup()
	 */
	mac->pcsr = priv->ioaddr;
	mac->multicast_filter_bins = priv->plat->multicast_filter_bins;
	mac->unicast_filter_entries = priv->plat->unicast_filter_entries;
	mac->mcast_bits_log2 = 0;

	if (mac->multicast_filter_bins)
		mac->mcast_bits_log2 = ilog2(mac->multicast_filter_bins);

	/* Loongson GMAC doesn't support the flow control. Loongson GNET
	 * without multi-channel doesn't support the half-duplex link mode.
	 */
	if (pdev->device != PCI_DEVICE_ID_LOONGSON_GNET) {
		mac->link.caps = MAC_10 | MAC_100 | MAC_1000;
	} else {
		if (ld->multichan)
			mac->link.caps = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
					 MAC_10 | MAC_100 | MAC_1000;
		else
			mac->link.caps = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
					 MAC_10FD | MAC_100FD | MAC_1000FD;
	}

	mac->link.duplex = GMAC_CONTROL_DM;
	mac->link.speed10 = GMAC_CONTROL_PS;
	mac->link.speed100 = GMAC_CONTROL_PS | GMAC_CONTROL_FES;
	mac->link.speed1000 = 0;
	mac->link.speed_mask = GMAC_CONTROL_PS | GMAC_CONTROL_FES;
	mac->mii.addr = GMAC_MII_ADDR;
	mac->mii.data = GMAC_MII_DATA;
	mac->mii.addr_shift = 11;
	mac->mii.addr_mask = 0x0000F800;
	mac->mii.reg_shift = 6;
	mac->mii.reg_mask = 0x000007C0;
	mac->mii.clk_csr_shift = 2;
	mac->mii.clk_csr_mask = GENMASK(5, 2);

	return mac;
}

static int loongson_dwmac_msi_config(struct pci_dev *pdev,
				     struct plat_stmmacenet_data *plat,
				     struct stmmac_resources *res)
{
	int i, ch_num, ret, vecs;

	ch_num = min(plat->tx_queues_to_use, plat->rx_queues_to_use);

	vecs = roundup_pow_of_two(ch_num * 2 + 1);
	ret = pci_alloc_irq_vectors(pdev, vecs, vecs, PCI_IRQ_MSI);
	if (ret < 0) {
		dev_warn(&pdev->dev, "Failed to allocate MSI IRQs\n");
		return ret;
	}

	res->irq = pci_irq_vector(pdev, 0);

	for (i = 0; i < ch_num; i++) {
		res->rx_irq[ch_num - 1 - i] = pci_irq_vector(pdev, 1 + i * 2);
	}

	for (i = 0; i < ch_num; i++) {
		res->tx_irq[ch_num - 1 - i] = pci_irq_vector(pdev, 2 + i * 2);
	}

	plat->flags |= STMMAC_FLAG_MULTI_MSI_EN;

	return 0;
}

static void loongson_dwmac_msi_clear(struct pci_dev *pdev)
{
	pci_free_irq_vectors(pdev);
}

static int loongson_dwmac_dt_config(struct pci_dev *pdev,
				    struct plat_stmmacenet_data *plat,
				    struct stmmac_resources *res)
{
	struct device_node *np = dev_of_node(&pdev->dev);
	int ret;

	plat->mdio_node = of_get_child_by_name(np, "mdio");
	if (plat->mdio_node) {
		dev_info(&pdev->dev, "Found MDIO subnode\n");
		plat->mdio_bus_data->needs_reset = true;
	}

	ret = of_alias_get_id(np, "ethernet");
	if (ret >= 0)
		plat->bus_id = ret;

	res->irq = of_irq_get_byname(np, "macirq");
	if (res->irq < 0) {
		dev_err(&pdev->dev, "IRQ macirq not found\n");
		ret = -ENODEV;
		goto err_put_node;
	}

	res->wol_irq = of_irq_get_byname(np, "eth_wake_irq");
	if (res->wol_irq < 0) {
		dev_info(&pdev->dev,
			 "IRQ eth_wake_irq not found, using macirq\n");
		res->wol_irq = res->irq;
	}

	res->lpi_irq = of_irq_get_byname(np, "eth_lpi");
	if (res->lpi_irq < 0) {
		dev_err(&pdev->dev, "IRQ eth_lpi not found\n");
		ret = -ENODEV;
		goto err_put_node;
	}

	ret = device_get_phy_mode(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "phy_mode not found\n");
		ret = -ENODEV;
		goto err_put_node;
	}

	plat->phy_interface = ret;

	return 0;

err_put_node:
	of_node_put(plat->mdio_node);

	return ret;
}

static void loongson_dwmac_dt_clear(struct pci_dev *pdev,
				    struct plat_stmmacenet_data *plat)
{
	of_node_put(plat->mdio_node);
}

static int loongson_dwmac_acpi_config(struct pci_dev *pdev,
				      struct plat_stmmacenet_data *plat,
				      struct stmmac_resources *res)
{
	if (!pdev->irq)
		return -EINVAL;

	res->irq = pdev->irq;

	return 0;
}

/* Loongson's DWMAC device may take nearly two seconds to complete DMA reset */
static int loongson_dwmac_fix_reset(struct stmmac_priv *priv, void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + DMA_BUS_MODE);

	if (value & DMA_BUS_MODE_SFT_RESET) {
		netdev_err(priv->dev, "the PHY clock is missing\n");
		return -EINVAL;
	}

	value |= DMA_BUS_MODE_SFT_RESET;
	writel(value, ioaddr + DMA_BUS_MODE);

	return readl_poll_timeout(ioaddr + DMA_BUS_MODE, value,
				  !(value & DMA_BUS_MODE_SFT_RESET),
				  10000, 2000000);
}

static int loongson_dwmac_suspend(struct device *dev, void *bsp_priv)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	ret = pci_save_state(pdev);
	if (ret)
		return ret;

	pci_disable_device(pdev);
	pci_wake_from_d3(pdev, true);
	return 0;
}

static int loongson_dwmac_resume(struct device *dev, void *bsp_priv)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	pci_restore_state(pdev);
	pci_set_power_state(pdev, PCI_D0);

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_master(pdev);

	return 0;
}

static int loongson_dwmac_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct plat_stmmacenet_data *plat;
	struct stmmac_resources res = {};
	struct stmmac_pci_info *info;
	struct loongson_data *ld;
	int ret;

	plat = devm_kzalloc(&pdev->dev, sizeof(*plat), GFP_KERNEL);
	if (!plat)
		return -ENOMEM;

	plat->mdio_bus_data = devm_kzalloc(&pdev->dev,
					   sizeof(*plat->mdio_bus_data),
					   GFP_KERNEL);
	if (!plat->mdio_bus_data)
		return -ENOMEM;

	plat->dma_cfg = devm_kzalloc(&pdev->dev, sizeof(*plat->dma_cfg), GFP_KERNEL);
	if (!plat->dma_cfg)
		return -ENOMEM;

	ld = devm_kzalloc(&pdev->dev, sizeof(*ld), GFP_KERNEL);
	if (!ld)
		return -ENOMEM;

	/* Enable pci device */
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s: ERROR: failed to enable device\n", __func__);
		return ret;
	}

	pci_set_master(pdev);

	/* Get the base address of device */
	res.addr = pcim_iomap_region(pdev, 0, DRIVER_NAME);
	ret = PTR_ERR_OR_ZERO(res.addr);
	if (ret)
		goto err_disable_device;

	plat->bsp_priv = ld;
	plat->setup = loongson_dwmac_setup;
	plat->fix_soc_reset = loongson_dwmac_fix_reset;
	plat->suspend = loongson_dwmac_suspend;
	plat->resume = loongson_dwmac_resume;
	ld->dev = &pdev->dev;
	ld->loongson_id = readl(res.addr + GMAC_VERSION) & 0xff;

	info = (struct stmmac_pci_info *)id->driver_data;
	ret = info->setup(pdev, plat);
	if (ret)
		goto err_disable_device;

	plat->tx_fifo_size = SZ_16K * plat->tx_queues_to_use;
	plat->rx_fifo_size = SZ_16K * plat->rx_queues_to_use;

	if (dev_of_node(&pdev->dev))
		ret = loongson_dwmac_dt_config(pdev, plat, &res);
	else
		ret = loongson_dwmac_acpi_config(pdev, plat, &res);
	if (ret)
		goto err_disable_device;

	/* Use the common MAC IRQ if per-channel MSIs allocation failed */
	if (ld->multichan)
		loongson_dwmac_msi_config(pdev, plat, &res);

	ret = stmmac_dvr_probe(&pdev->dev, plat, &res);
	if (ret)
		goto err_plat_clear;

	return 0;

err_plat_clear:
	if (dev_of_node(&pdev->dev))
		loongson_dwmac_dt_clear(pdev, plat);
	if (ld->multichan)
		loongson_dwmac_msi_clear(pdev);
err_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void loongson_dwmac_remove(struct pci_dev *pdev)
{
	struct net_device *ndev = dev_get_drvdata(&pdev->dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct loongson_data *ld;

	ld = priv->plat->bsp_priv;
	stmmac_dvr_remove(&pdev->dev);

	if (dev_of_node(&pdev->dev))
		loongson_dwmac_dt_clear(pdev, priv->plat);

	if (ld->multichan)
		loongson_dwmac_msi_clear(pdev);

	pci_disable_device(pdev);
}

static const struct pci_device_id loongson_dwmac_id_table[] = {
	{ PCI_DEVICE_DATA(LOONGSON, GMAC1, &loongson_gmac_pci_info) },
	{ PCI_DEVICE_DATA(LOONGSON, GMAC2, &loongson_gmac_pci_info) },
	{ PCI_DEVICE_DATA(LOONGSON, GNET, &loongson_gnet_pci_info) },
	{}
};
MODULE_DEVICE_TABLE(pci, loongson_dwmac_id_table);

static struct pci_driver loongson_dwmac_driver = {
	.name = DRIVER_NAME,
	.id_table = loongson_dwmac_id_table,
	.probe = loongson_dwmac_probe,
	.remove = loongson_dwmac_remove,
	.driver = {
		.pm = &stmmac_simple_pm_ops,
	},
};

module_pci_driver(loongson_dwmac_driver);

MODULE_DESCRIPTION("Loongson DWMAC PCI driver");
MODULE_AUTHOR("Qing Zhang <zhangqing@loongson.cn>");
MODULE_AUTHOR("Yanteng Si <siyanteng@loongson.cn>");
MODULE_LICENSE("GPL v2");
