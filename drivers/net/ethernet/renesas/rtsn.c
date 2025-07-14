// SPDX-License-Identifier: GPL-2.0

/* Renesas Ethernet-TSN device driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 * Copyright (C) 2023 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/net_tstamp.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#include "rtsn.h"
#include "rcar_gen4_ptp.h"

struct rtsn_private {
	struct net_device *ndev;
	struct platform_device *pdev;
	void __iomem *base;
	struct rcar_gen4_ptp_private *ptp_priv;
	struct clk *clk;
	struct reset_control *reset;

	u32 num_tx_ring;
	u32 num_rx_ring;
	u32 tx_desc_bat_size;
	dma_addr_t tx_desc_bat_dma;
	struct rtsn_desc *tx_desc_bat;
	u32 rx_desc_bat_size;
	dma_addr_t rx_desc_bat_dma;
	struct rtsn_desc *rx_desc_bat;
	dma_addr_t tx_desc_dma;
	dma_addr_t rx_desc_dma;
	struct rtsn_ext_desc *tx_ring;
	struct rtsn_ext_ts_desc *rx_ring;
	struct sk_buff **tx_skb;
	struct sk_buff **rx_skb;
	spinlock_t lock;	/* Register access lock */
	u32 cur_tx;
	u32 dirty_tx;
	u32 cur_rx;
	u32 dirty_rx;
	u8 ts_tag;
	struct napi_struct napi;
	struct rtnl_link_stats64 stats;

	struct mii_bus *mii;
	phy_interface_t iface;
	int link;
	int speed;

	int tx_data_irq;
	int rx_data_irq;
};

static u32 rtsn_read(struct rtsn_private *priv, enum rtsn_reg reg)
{
	return ioread32(priv->base + reg);
}

static void rtsn_write(struct rtsn_private *priv, enum rtsn_reg reg, u32 data)
{
	iowrite32(data, priv->base + reg);
}

static void rtsn_modify(struct rtsn_private *priv, enum rtsn_reg reg,
			u32 clear, u32 set)
{
	rtsn_write(priv, reg, (rtsn_read(priv, reg) & ~clear) | set);
}

static int rtsn_reg_wait(struct rtsn_private *priv, enum rtsn_reg reg,
			 u32 mask, u32 expected)
{
	u32 val;

	return readl_poll_timeout(priv->base + reg, val,
				  (val & mask) == expected,
				  RTSN_INTERVAL_US, RTSN_TIMEOUT_US);
}

static void rtsn_ctrl_data_irq(struct rtsn_private *priv, bool enable)
{
	if (enable) {
		rtsn_write(priv, TDIE0, TDIE_TDID_TDX(TX_CHAIN_IDX));
		rtsn_write(priv, RDIE0, RDIE_RDID_RDX(RX_CHAIN_IDX));
	} else {
		rtsn_write(priv, TDID0, TDIE_TDID_TDX(TX_CHAIN_IDX));
		rtsn_write(priv, RDID0, RDIE_RDID_RDX(RX_CHAIN_IDX));
	}
}

static void rtsn_get_timestamp(struct rtsn_private *priv, struct timespec64 *ts)
{
	struct rcar_gen4_ptp_private *ptp_priv = priv->ptp_priv;

	ptp_priv->info.gettime64(&ptp_priv->info, ts);
}

static int rtsn_tx_free(struct net_device *ndev, bool free_txed_only)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	struct rtsn_ext_desc *desc;
	struct sk_buff *skb;
	int free_num = 0;
	int entry, size;

	for (; priv->cur_tx - priv->dirty_tx > 0; priv->dirty_tx++) {
		entry = priv->dirty_tx % priv->num_tx_ring;
		desc = &priv->tx_ring[entry];
		if (free_txed_only && (desc->die_dt & DT_MASK) != DT_FEMPTY)
			break;

		dma_rmb();
		size = le16_to_cpu(desc->info_ds) & TX_DS;
		skb = priv->tx_skb[entry];
		if (skb) {
			if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
				struct skb_shared_hwtstamps shhwtstamps;
				struct timespec64 ts;

				rtsn_get_timestamp(priv, &ts);
				memset(&shhwtstamps, 0, sizeof(shhwtstamps));
				shhwtstamps.hwtstamp = timespec64_to_ktime(ts);
				skb_tstamp_tx(skb, &shhwtstamps);
			}
			dma_unmap_single(ndev->dev.parent,
					 le32_to_cpu(desc->dptr),
					 size, DMA_TO_DEVICE);
			dev_kfree_skb_any(priv->tx_skb[entry]);
			free_num++;

			priv->stats.tx_packets++;
			priv->stats.tx_bytes += size;
		}

		desc->die_dt = DT_EEMPTY;
	}

	desc = &priv->tx_ring[priv->num_tx_ring];
	desc->die_dt = DT_LINK;

	return free_num;
}

static int rtsn_rx(struct net_device *ndev, int budget)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	unsigned int ndescriptors;
	unsigned int rx_packets;
	unsigned int i;
	bool get_ts;

	get_ts = priv->ptp_priv->tstamp_rx_ctrl &
		RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT;

	ndescriptors = priv->dirty_rx + priv->num_rx_ring - priv->cur_rx;
	rx_packets = 0;
	for (i = 0; i < ndescriptors; i++) {
		const unsigned int entry = priv->cur_rx % priv->num_rx_ring;
		struct rtsn_ext_ts_desc *desc = &priv->rx_ring[entry];
		struct sk_buff *skb;
		dma_addr_t dma_addr;
		u16 pkt_len;

		/* Stop processing descriptors if budget is consumed. */
		if (rx_packets >= budget)
			break;

		/* Stop processing descriptors on first empty. */
		if ((desc->die_dt & DT_MASK) == DT_FEMPTY)
			break;

		dma_rmb();
		pkt_len = le16_to_cpu(desc->info_ds) & RX_DS;

		skb = priv->rx_skb[entry];
		priv->rx_skb[entry] = NULL;
		dma_addr = le32_to_cpu(desc->dptr);
		dma_unmap_single(ndev->dev.parent, dma_addr, PKT_BUF_SZ,
				 DMA_FROM_DEVICE);

		/* Get timestamp if enabled. */
		if (get_ts) {
			struct skb_shared_hwtstamps *shhwtstamps;
			struct timespec64 ts;

			shhwtstamps = skb_hwtstamps(skb);
			memset(shhwtstamps, 0, sizeof(*shhwtstamps));

			ts.tv_sec = (u64)le32_to_cpu(desc->ts_sec);
			ts.tv_nsec = le32_to_cpu(desc->ts_nsec & cpu_to_le32(0x3fffffff));

			shhwtstamps->hwtstamp = timespec64_to_ktime(ts);
		}

		skb_put(skb, pkt_len);
		skb->protocol = eth_type_trans(skb, ndev);
		napi_gro_receive(&priv->napi, skb);

		/* Update statistics. */
		priv->stats.rx_packets++;
		priv->stats.rx_bytes += pkt_len;

		/* Update counters. */
		priv->cur_rx++;
		rx_packets++;
	}

	/* Refill the RX ring buffers */
	for (; priv->cur_rx - priv->dirty_rx > 0; priv->dirty_rx++) {
		const unsigned int entry = priv->dirty_rx % priv->num_rx_ring;
		struct rtsn_ext_ts_desc *desc = &priv->rx_ring[entry];
		struct sk_buff *skb;
		dma_addr_t dma_addr;

		desc->info_ds = cpu_to_le16(PKT_BUF_SZ);

		if (!priv->rx_skb[entry]) {
			skb = napi_alloc_skb(&priv->napi,
					     PKT_BUF_SZ + RTSN_ALIGN - 1);
			if (!skb)
				break;
			skb_reserve(skb, NET_IP_ALIGN);
			dma_addr = dma_map_single(ndev->dev.parent, skb->data,
						  le16_to_cpu(desc->info_ds),
						  DMA_FROM_DEVICE);
			if (dma_mapping_error(ndev->dev.parent, dma_addr))
				desc->info_ds = cpu_to_le16(0);
			desc->dptr = cpu_to_le32(dma_addr);
			skb_checksum_none_assert(skb);
			priv->rx_skb[entry] = skb;
		}

		dma_wmb();
		desc->die_dt = DT_FEMPTY | D_DIE;
	}

	priv->rx_ring[priv->num_rx_ring].die_dt = DT_LINK;

	return rx_packets;
}

static int rtsn_poll(struct napi_struct *napi, int budget)
{
	struct rtsn_private *priv;
	struct net_device *ndev;
	unsigned long flags;
	int work_done;

	ndev = napi->dev;
	priv = netdev_priv(ndev);

	/* Processing RX Descriptor Ring */
	work_done = rtsn_rx(ndev, budget);

	/* Processing TX Descriptor Ring */
	spin_lock_irqsave(&priv->lock, flags);
	rtsn_tx_free(ndev, true);
	netif_wake_subqueue(ndev, 0);
	spin_unlock_irqrestore(&priv->lock, flags);

	/* Re-enable TX/RX interrupts */
	if (work_done < budget && napi_complete_done(napi, work_done)) {
		spin_lock_irqsave(&priv->lock, flags);
		rtsn_ctrl_data_irq(priv, true);
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	return work_done;
}

static int rtsn_desc_alloc(struct rtsn_private *priv)
{
	struct device *dev = &priv->pdev->dev;
	unsigned int i;

	priv->tx_desc_bat_size = sizeof(struct rtsn_desc) * TX_NUM_CHAINS;
	priv->tx_desc_bat = dma_alloc_coherent(dev, priv->tx_desc_bat_size,
					       &priv->tx_desc_bat_dma,
					       GFP_KERNEL);

	if (!priv->tx_desc_bat)
		return -ENOMEM;

	for (i = 0; i < TX_NUM_CHAINS; i++)
		priv->tx_desc_bat[i].die_dt = DT_EOS;

	priv->rx_desc_bat_size = sizeof(struct rtsn_desc) * RX_NUM_CHAINS;
	priv->rx_desc_bat = dma_alloc_coherent(dev, priv->rx_desc_bat_size,
					       &priv->rx_desc_bat_dma,
					       GFP_KERNEL);

	if (!priv->rx_desc_bat)
		return -ENOMEM;

	for (i = 0; i < RX_NUM_CHAINS; i++)
		priv->rx_desc_bat[i].die_dt = DT_EOS;

	return 0;
}

static void rtsn_desc_free(struct rtsn_private *priv)
{
	if (priv->tx_desc_bat)
		dma_free_coherent(&priv->pdev->dev, priv->tx_desc_bat_size,
				  priv->tx_desc_bat, priv->tx_desc_bat_dma);
	priv->tx_desc_bat = NULL;

	if (priv->rx_desc_bat)
		dma_free_coherent(&priv->pdev->dev, priv->rx_desc_bat_size,
				  priv->rx_desc_bat, priv->rx_desc_bat_dma);
	priv->rx_desc_bat = NULL;
}

static void rtsn_chain_free(struct rtsn_private *priv)
{
	struct device *dev = &priv->pdev->dev;

	dma_free_coherent(dev,
			  sizeof(struct rtsn_ext_desc) * (priv->num_tx_ring + 1),
			  priv->tx_ring, priv->tx_desc_dma);
	priv->tx_ring = NULL;

	dma_free_coherent(dev,
			  sizeof(struct rtsn_ext_ts_desc) * (priv->num_rx_ring + 1),
			  priv->rx_ring, priv->rx_desc_dma);
	priv->rx_ring = NULL;

	kfree(priv->tx_skb);
	priv->tx_skb = NULL;

	kfree(priv->rx_skb);
	priv->rx_skb = NULL;
}

static int rtsn_chain_init(struct rtsn_private *priv, int tx_size, int rx_size)
{
	struct net_device *ndev = priv->ndev;
	struct sk_buff *skb;
	int i;

	priv->num_tx_ring = tx_size;
	priv->num_rx_ring = rx_size;

	priv->tx_skb = kcalloc(tx_size, sizeof(*priv->tx_skb), GFP_KERNEL);
	priv->rx_skb = kcalloc(rx_size, sizeof(*priv->rx_skb), GFP_KERNEL);

	if (!priv->rx_skb || !priv->tx_skb)
		goto error;

	for (i = 0; i < rx_size; i++) {
		skb = netdev_alloc_skb(ndev, PKT_BUF_SZ + RTSN_ALIGN - 1);
		if (!skb)
			goto error;
		skb_reserve(skb, NET_IP_ALIGN);
		priv->rx_skb[i] = skb;
	}

	/* Allocate TX, RX descriptors */
	priv->tx_ring = dma_alloc_coherent(ndev->dev.parent,
					   sizeof(struct rtsn_ext_desc) * (tx_size + 1),
					   &priv->tx_desc_dma, GFP_KERNEL);
	priv->rx_ring = dma_alloc_coherent(ndev->dev.parent,
					   sizeof(struct rtsn_ext_ts_desc) * (rx_size + 1),
					   &priv->rx_desc_dma, GFP_KERNEL);

	if (!priv->tx_ring || !priv->rx_ring)
		goto error;

	return 0;
error:
	rtsn_chain_free(priv);

	return -ENOMEM;
}

static void rtsn_chain_format(struct rtsn_private *priv)
{
	struct net_device *ndev = priv->ndev;
	struct rtsn_ext_ts_desc *rx_desc;
	struct rtsn_ext_desc *tx_desc;
	struct rtsn_desc *bat_desc;
	dma_addr_t dma_addr;
	unsigned int i;

	priv->cur_tx = 0;
	priv->cur_rx = 0;
	priv->dirty_rx = 0;
	priv->dirty_tx = 0;

	/* TX */
	memset(priv->tx_ring, 0, sizeof(*tx_desc) * priv->num_tx_ring);
	for (i = 0, tx_desc = priv->tx_ring; i < priv->num_tx_ring; i++, tx_desc++)
		tx_desc->die_dt = DT_EEMPTY | D_DIE;

	tx_desc->dptr = cpu_to_le32((u32)priv->tx_desc_dma);
	tx_desc->die_dt = DT_LINK;

	bat_desc = &priv->tx_desc_bat[TX_CHAIN_IDX];
	bat_desc->die_dt = DT_LINK;
	bat_desc->dptr = cpu_to_le32((u32)priv->tx_desc_dma);

	/* RX */
	memset(priv->rx_ring, 0, sizeof(*rx_desc) * priv->num_rx_ring);
	for (i = 0, rx_desc = priv->rx_ring; i < priv->num_rx_ring; i++, rx_desc++) {
		dma_addr = dma_map_single(ndev->dev.parent,
					  priv->rx_skb[i]->data, PKT_BUF_SZ,
					  DMA_FROM_DEVICE);
		if (!dma_mapping_error(ndev->dev.parent, dma_addr))
			rx_desc->info_ds = cpu_to_le16(PKT_BUF_SZ);
		rx_desc->dptr = cpu_to_le32((u32)dma_addr);
		rx_desc->die_dt = DT_FEMPTY | D_DIE;
	}
	rx_desc->dptr = cpu_to_le32((u32)priv->rx_desc_dma);
	rx_desc->die_dt = DT_LINK;

	bat_desc = &priv->rx_desc_bat[RX_CHAIN_IDX];
	bat_desc->die_dt = DT_LINK;
	bat_desc->dptr = cpu_to_le32((u32)priv->rx_desc_dma);
}

static int rtsn_dmac_init(struct rtsn_private *priv)
{
	int ret;

	ret = rtsn_chain_init(priv, TX_CHAIN_SIZE, RX_CHAIN_SIZE);
	if (ret)
		return ret;

	rtsn_chain_format(priv);

	return 0;
}

static enum rtsn_mode rtsn_read_mode(struct rtsn_private *priv)
{
	return (rtsn_read(priv, OSR) & OSR_OPS) >> 1;
}

static int rtsn_wait_mode(struct rtsn_private *priv, enum rtsn_mode mode)
{
	unsigned int i;

	/* Need to busy loop as mode changes can happen in atomic context. */
	for (i = 0; i < RTSN_TIMEOUT_US / RTSN_INTERVAL_US; i++) {
		if (rtsn_read_mode(priv) == mode)
			return 0;

		udelay(RTSN_INTERVAL_US);
	}

	return -ETIMEDOUT;
}

static int rtsn_change_mode(struct rtsn_private *priv, enum rtsn_mode mode)
{
	int ret;

	rtsn_write(priv, OCR, mode);
	ret = rtsn_wait_mode(priv, mode);
	if (ret)
		netdev_err(priv->ndev, "Failed to switch operation mode\n");
	return ret;
}

static int rtsn_get_data_irq_status(struct rtsn_private *priv)
{
	u32 val;

	val = rtsn_read(priv, TDIS0) | TDIS_TDS(TX_CHAIN_IDX);
	val |= rtsn_read(priv, RDIS0) | RDIS_RDS(RX_CHAIN_IDX);

	return val;
}

static irqreturn_t rtsn_irq(int irq, void *dev_id)
{
	struct rtsn_private *priv = dev_id;
	int ret = IRQ_NONE;

	spin_lock(&priv->lock);

	if (rtsn_get_data_irq_status(priv)) {
		/* Clear TX/RX irq status */
		rtsn_write(priv, TDIS0, TDIS_TDS(TX_CHAIN_IDX));
		rtsn_write(priv, RDIS0, RDIS_RDS(RX_CHAIN_IDX));

		if (napi_schedule_prep(&priv->napi)) {
			/* Disable TX/RX interrupts */
			rtsn_ctrl_data_irq(priv, false);

			__napi_schedule(&priv->napi);
		}

		ret = IRQ_HANDLED;
	}

	spin_unlock(&priv->lock);

	return ret;
}

static int rtsn_request_irq(unsigned int irq, irq_handler_t handler,
			    unsigned long flags, struct rtsn_private *priv,
			    const char *ch)
{
	char *name;
	int ret;

	name = devm_kasprintf(&priv->pdev->dev, GFP_KERNEL, "%s:%s",
			      priv->ndev->name, ch);
	if (!name)
		return -ENOMEM;

	ret = request_irq(irq, handler, flags, name, priv);
	if (ret)
		netdev_err(priv->ndev, "Cannot request IRQ %s\n", name);

	return ret;
}

static void rtsn_free_irqs(struct rtsn_private *priv)
{
	free_irq(priv->tx_data_irq, priv);
	free_irq(priv->rx_data_irq, priv);
}

static int rtsn_request_irqs(struct rtsn_private *priv)
{
	int ret;

	priv->rx_data_irq = platform_get_irq_byname(priv->pdev, "rx");
	if (priv->rx_data_irq < 0)
		return priv->rx_data_irq;

	priv->tx_data_irq = platform_get_irq_byname(priv->pdev, "tx");
	if (priv->tx_data_irq < 0)
		return priv->tx_data_irq;

	ret = rtsn_request_irq(priv->tx_data_irq, rtsn_irq, 0, priv, "tx");
	if (ret)
		return ret;

	ret = rtsn_request_irq(priv->rx_data_irq, rtsn_irq, 0, priv, "rx");
	if (ret) {
		free_irq(priv->tx_data_irq, priv);
		return ret;
	}

	return 0;
}

static int rtsn_reset(struct rtsn_private *priv)
{
	reset_control_reset(priv->reset);
	mdelay(1);

	return rtsn_wait_mode(priv, OCR_OPC_DISABLE);
}

static int rtsn_axibmi_init(struct rtsn_private *priv)
{
	int ret;

	ret = rtsn_reg_wait(priv, RR, RR_RST, RR_RST_COMPLETE);
	if (ret)
		return ret;

	/* Set AXIWC */
	rtsn_write(priv, AXIWC, AXIWC_DEFAULT);

	/* Set AXIRC */
	rtsn_write(priv, AXIRC, AXIRC_DEFAULT);

	/* TX Descriptor chain setting */
	rtsn_write(priv, TATLS0, TATLS0_TEDE | TATLS0_TATEN(TX_CHAIN_IDX));
	rtsn_write(priv, TATLS1, priv->tx_desc_bat_dma + TX_CHAIN_ADDR_OFFSET);
	rtsn_write(priv, TATLR, TATLR_TATL);

	ret = rtsn_reg_wait(priv, TATLR, TATLR_TATL, 0);
	if (ret)
		return ret;

	/* RX Descriptor chain setting */
	rtsn_write(priv, RATLS0,
		   RATLS0_RETS | RATLS0_REDE | RATLS0_RATEN(RX_CHAIN_IDX));
	rtsn_write(priv, RATLS1, priv->rx_desc_bat_dma + RX_CHAIN_ADDR_OFFSET);
	rtsn_write(priv, RATLR, RATLR_RATL);

	ret = rtsn_reg_wait(priv, RATLR, RATLR_RATL, 0);
	if (ret)
		return ret;

	/* Enable TX/RX interrupts */
	rtsn_ctrl_data_irq(priv, true);

	return 0;
}

static void rtsn_mhd_init(struct rtsn_private *priv)
{
	/* TX General setting */
	rtsn_write(priv, TGC1, TGC1_STTV_DEFAULT | TGC1_TQTM_SFM);
	rtsn_write(priv, TMS0, TMS_MFS_MAX);

	/* RX Filter IP */
	rtsn_write(priv, CFCR0, CFCR_SDID(RX_CHAIN_IDX));
	rtsn_write(priv, FMSCR, FMSCR_FMSIE(RX_CHAIN_IDX));
}

static int rtsn_get_phy_params(struct rtsn_private *priv)
{
	int ret;

	ret = of_get_phy_mode(priv->pdev->dev.of_node, &priv->iface);
	if (ret)
		return ret;

	switch (priv->iface) {
	case PHY_INTERFACE_MODE_MII:
		priv->speed = 100;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		priv->speed = 1000;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static void rtsn_set_phy_interface(struct rtsn_private *priv)
{
	u32 val;

	switch (priv->iface) {
	case PHY_INTERFACE_MODE_MII:
		val = MPIC_PIS_MII;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = MPIC_PIS_GMII;
		break;
	default:
		return;
	}

	rtsn_modify(priv, MPIC, MPIC_PIS_MASK, val);
}

static void rtsn_set_rate(struct rtsn_private *priv)
{
	u32 val;

	switch (priv->speed) {
	case 10:
		val = MPIC_LSC_10M;
		break;
	case 100:
		val = MPIC_LSC_100M;
		break;
	case 1000:
		val = MPIC_LSC_1G;
		break;
	default:
		return;
	}

	rtsn_modify(priv, MPIC, MPIC_LSC_MASK, val);
}

static int rtsn_rmac_init(struct rtsn_private *priv)
{
	const u8 *mac_addr = priv->ndev->dev_addr;
	int ret;

	/* Set MAC address */
	rtsn_write(priv, MRMAC0, (mac_addr[0] << 8) | mac_addr[1]);
	rtsn_write(priv, MRMAC1, (mac_addr[2] << 24) | (mac_addr[3] << 16) |
		   (mac_addr[4] << 8) | mac_addr[5]);

	/* Set xMII type */
	rtsn_set_phy_interface(priv);
	rtsn_set_rate(priv);

	/* Enable MII */
	rtsn_modify(priv, MPIC, MPIC_PSMCS_MASK | MPIC_PSMHT_MASK,
		    MPIC_PSMCS_DEFAULT | MPIC_PSMHT_DEFAULT);

	/* Link verification */
	rtsn_modify(priv, MLVC, MLVC_PLV, MLVC_PLV);
	ret = rtsn_reg_wait(priv, MLVC, MLVC_PLV, 0);
	if (ret)
		return ret;

	return ret;
}

static int rtsn_hw_init(struct rtsn_private *priv)
{
	int ret;

	ret = rtsn_reset(priv);
	if (ret)
		return ret;

	/* Change to CONFIG mode */
	ret = rtsn_change_mode(priv, OCR_OPC_CONFIG);
	if (ret)
		return ret;

	ret = rtsn_axibmi_init(priv);
	if (ret)
		return ret;

	rtsn_mhd_init(priv);

	ret = rtsn_rmac_init(priv);
	if (ret)
		return ret;

	ret = rtsn_change_mode(priv, OCR_OPC_DISABLE);
	if (ret)
		return ret;

	/* Change to OPERATION mode */
	ret = rtsn_change_mode(priv, OCR_OPC_OPERATION);

	return ret;
}

static int rtsn_mii_access(struct mii_bus *bus, bool read, int phyad,
			   int regad, u16 data)
{
	struct rtsn_private *priv = bus->priv;
	u32 val;
	int ret;

	val = MPSM_PDA(phyad) | MPSM_PRA(regad) | MPSM_PSME;

	if (!read)
		val |= MPSM_PSMAD | MPSM_PRD_SET(data);

	rtsn_write(priv, MPSM, val);

	ret = rtsn_reg_wait(priv, MPSM, MPSM_PSME, 0);
	if (ret)
		return ret;

	if (read)
		ret = MPSM_PRD_GET(rtsn_read(priv, MPSM));

	return ret;
}

static int rtsn_mii_read(struct mii_bus *bus, int addr, int regnum)
{
	return rtsn_mii_access(bus, true, addr, regnum, 0);
}

static int rtsn_mii_write(struct mii_bus *bus, int addr, int regnum, u16 val)
{
	return rtsn_mii_access(bus, false, addr, regnum, val);
}

static int rtsn_mdio_alloc(struct rtsn_private *priv)
{
	struct platform_device *pdev = priv->pdev;
	struct device *dev = &pdev->dev;
	struct device_node *mdio_node;
	struct mii_bus *mii;
	int ret;

	mii = mdiobus_alloc();
	if (!mii)
		return -ENOMEM;

	mdio_node = of_get_child_by_name(dev->of_node, "mdio");
	if (!mdio_node) {
		ret = -ENODEV;
		goto out_free_bus;
	}

	/* Enter config mode before registering the MDIO bus */
	ret = rtsn_reset(priv);
	if (ret)
		goto out_free_bus;

	ret = rtsn_change_mode(priv, OCR_OPC_CONFIG);
	if (ret)
		goto out_free_bus;

	rtsn_modify(priv, MPIC, MPIC_PSMCS_MASK | MPIC_PSMHT_MASK,
		    MPIC_PSMCS_DEFAULT | MPIC_PSMHT_DEFAULT);

	/* Register the MDIO bus */
	mii->name = "rtsn_mii";
	snprintf(mii->id, MII_BUS_ID_SIZE, "%s-%x",
		 pdev->name, pdev->id);
	mii->priv = priv;
	mii->read = rtsn_mii_read;
	mii->write = rtsn_mii_write;
	mii->parent = dev;

	ret = of_mdiobus_register(mii, mdio_node);
	of_node_put(mdio_node);
	if (ret)
		goto out_free_bus;

	priv->mii = mii;

	return 0;

out_free_bus:
	mdiobus_free(mii);
	return ret;
}

static void rtsn_mdio_free(struct rtsn_private *priv)
{
	mdiobus_unregister(priv->mii);
	mdiobus_free(priv->mii);
	priv->mii = NULL;
}

static void rtsn_adjust_link(struct net_device *ndev)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	bool new_state = false;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	if (phydev->link) {
		if (phydev->speed != priv->speed) {
			new_state = true;
			priv->speed = phydev->speed;
		}

		if (!priv->link) {
			new_state = true;
			priv->link = phydev->link;
		}
	} else if (priv->link) {
		new_state = true;
		priv->link = 0;
		priv->speed = 0;
	}

	if (new_state) {
		/* Need to transition to CONFIG mode before reconfiguring and
		 * then back to the original mode. Any state change to/from
		 * CONFIG or OPERATION must go over DISABLED to stop Rx/Tx.
		 */
		enum rtsn_mode orgmode = rtsn_read_mode(priv);

		/* Transit to CONFIG */
		if (orgmode != OCR_OPC_CONFIG) {
			if (orgmode != OCR_OPC_DISABLE &&
			    rtsn_change_mode(priv, OCR_OPC_DISABLE))
				goto out;
			if (rtsn_change_mode(priv, OCR_OPC_CONFIG))
				goto out;
		}

		rtsn_set_rate(priv);

		/* Transition to original mode */
		if (orgmode != OCR_OPC_CONFIG) {
			if (rtsn_change_mode(priv, OCR_OPC_DISABLE))
				goto out;
			if (orgmode != OCR_OPC_DISABLE &&
			    rtsn_change_mode(priv, orgmode))
				goto out;
		}
	}
out:
	spin_unlock_irqrestore(&priv->lock, flags);

	if (new_state)
		phy_print_status(phydev);
}

static int rtsn_phy_init(struct rtsn_private *priv)
{
	struct device_node *np = priv->ndev->dev.parent->of_node;
	struct phy_device *phydev;
	struct device_node *phy;

	priv->link = 0;

	phy = of_parse_phandle(np, "phy-handle", 0);
	if (!phy)
		return -ENOENT;

	phydev = of_phy_connect(priv->ndev, phy, rtsn_adjust_link, 0,
				priv->iface);
	of_node_put(phy);
	if (!phydev)
		return -ENOENT;

	/* Only support full-duplex mode */
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);

	phy_attached_info(phydev);

	return 0;
}

static void rtsn_phy_deinit(struct rtsn_private *priv)
{
	phy_disconnect(priv->ndev->phydev);
	priv->ndev->phydev = NULL;
}

static int rtsn_init(struct rtsn_private *priv)
{
	int ret;

	ret = rtsn_desc_alloc(priv);
	if (ret)
		return ret;

	ret = rtsn_dmac_init(priv);
	if (ret)
		goto error_free_desc;

	ret = rtsn_hw_init(priv);
	if (ret)
		goto error_free_chain;

	ret = rtsn_phy_init(priv);
	if (ret)
		goto error_free_chain;

	ret = rtsn_request_irqs(priv);
	if (ret)
		goto error_free_phy;

	return 0;
error_free_phy:
	rtsn_phy_deinit(priv);
error_free_chain:
	rtsn_chain_free(priv);
error_free_desc:
	rtsn_desc_free(priv);
	return ret;
}

static void rtsn_deinit(struct rtsn_private *priv)
{
	rtsn_free_irqs(priv);
	rtsn_phy_deinit(priv);
	rtsn_chain_free(priv);
	rtsn_desc_free(priv);
}

static void rtsn_parse_mac_address(struct device_node *np,
				   struct net_device *ndev)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	u8 addr[ETH_ALEN];
	u32 mrmac0;
	u32 mrmac1;

	/* Try to read address from Device Tree. */
	if (!of_get_mac_address(np, addr)) {
		eth_hw_addr_set(ndev, addr);
		return;
	}

	/* Try to read address from device. */
	mrmac0 = rtsn_read(priv, MRMAC0);
	mrmac1 = rtsn_read(priv, MRMAC1);

	addr[0] = (mrmac0 >>  8) & 0xff;
	addr[1] = (mrmac0 >>  0) & 0xff;
	addr[2] = (mrmac1 >> 24) & 0xff;
	addr[3] = (mrmac1 >> 16) & 0xff;
	addr[4] = (mrmac1 >>  8) & 0xff;
	addr[5] = (mrmac1 >>  0) & 0xff;

	if (is_valid_ether_addr(addr)) {
		eth_hw_addr_set(ndev, addr);
		return;
	}

	/* Fallback to a random address */
	eth_hw_addr_random(ndev);
}

static int rtsn_open(struct net_device *ndev)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	int ret;

	napi_enable(&priv->napi);

	ret = rtsn_init(priv);
	if (ret) {
		napi_disable(&priv->napi);
		return ret;
	}

	phy_start(ndev->phydev);

	netif_start_queue(ndev);

	return 0;
}

static int rtsn_stop(struct net_device *ndev)
{
	struct rtsn_private *priv = netdev_priv(ndev);

	phy_stop(priv->ndev->phydev);
	napi_disable(&priv->napi);
	rtsn_change_mode(priv, OCR_OPC_DISABLE);
	rtsn_deinit(priv);

	return 0;
}

static netdev_tx_t rtsn_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	struct rtsn_ext_desc *desc;
	int ret = NETDEV_TX_OK;
	unsigned long flags;
	dma_addr_t dma_addr;
	int entry;

	spin_lock_irqsave(&priv->lock, flags);

	/* Drop packet if it won't fit in a single descriptor. */
	if (skb->len >= TX_DS) {
		priv->stats.tx_dropped++;
		priv->stats.tx_errors++;
		dev_kfree_skb_any(skb);
		goto out;
	}

	if (priv->cur_tx - priv->dirty_tx > priv->num_tx_ring) {
		netif_stop_subqueue(ndev, 0);
		ret = NETDEV_TX_BUSY;
		goto out;
	}

	if (skb_put_padto(skb, ETH_ZLEN))
		goto out;

	dma_addr = dma_map_single(ndev->dev.parent, skb->data, skb->len,
				  DMA_TO_DEVICE);
	if (dma_mapping_error(ndev->dev.parent, dma_addr)) {
		dev_kfree_skb_any(skb);
		goto out;
	}

	entry = priv->cur_tx % priv->num_tx_ring;
	priv->tx_skb[entry] = skb;
	desc = &priv->tx_ring[entry];
	desc->dptr = cpu_to_le32(dma_addr);
	desc->info_ds = cpu_to_le16(skb->len);
	desc->info1 = cpu_to_le64(skb->len);

	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		priv->ts_tag++;
		desc->info_ds |= cpu_to_le16(TXC);
		desc->info = priv->ts_tag;
	}

	skb_tx_timestamp(skb);
	dma_wmb();

	desc->die_dt = DT_FSINGLE | D_DIE;
	priv->cur_tx++;

	/* Start xmit */
	rtsn_write(priv, TRCR0, BIT(TX_CHAIN_IDX));
out:
	spin_unlock_irqrestore(&priv->lock, flags);
	return ret;
}

static void rtsn_get_stats64(struct net_device *ndev,
			     struct rtnl_link_stats64 *storage)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	*storage = priv->stats;
}

static int rtsn_do_ioctl(struct net_device *ndev, struct ifreq *ifr, int cmd)
{
	if (!netif_running(ndev))
		return -ENODEV;

	return phy_do_ioctl_running(ndev, ifr, cmd);
}

static int rtsn_hwtstamp_get(struct net_device *ndev,
			     struct kernel_hwtstamp_config *config)
{
	struct rcar_gen4_ptp_private *ptp_priv;
	struct rtsn_private *priv;

	if (!netif_running(ndev))
		return -ENODEV;

	priv = netdev_priv(ndev);
	ptp_priv = priv->ptp_priv;

	config->flags = 0;

	config->tx_type =
		ptp_priv->tstamp_tx_ctrl ? HWTSTAMP_TX_ON : HWTSTAMP_TX_OFF;

	switch (ptp_priv->tstamp_rx_ctrl & RCAR_GEN4_RXTSTAMP_TYPE) {
	case RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		break;
	case RCAR_GEN4_RXTSTAMP_TYPE_ALL:
		config->rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		config->rx_filter = HWTSTAMP_FILTER_NONE;
		break;
	}

	return 0;
}

static int rtsn_hwtstamp_set(struct net_device *ndev,
			     struct kernel_hwtstamp_config *config,
			     struct netlink_ext_ack *extack)
{
	struct rcar_gen4_ptp_private *ptp_priv;
	struct rtsn_private *priv;
	u32 tstamp_rx_ctrl;
	u32 tstamp_tx_ctrl;

	if (!netif_running(ndev))
		return -ENODEV;

	priv = netdev_priv(ndev);
	ptp_priv = priv->ptp_priv;

	if (config->flags)
		return -EINVAL;

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		tstamp_tx_ctrl = 0;
		break;
	case HWTSTAMP_TX_ON:
		tstamp_tx_ctrl = RCAR_GEN4_TXTSTAMP_ENABLED;
		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		tstamp_rx_ctrl = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		tstamp_rx_ctrl = RCAR_GEN4_RXTSTAMP_ENABLED |
			RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT;
		break;
	default:
		config->rx_filter = HWTSTAMP_FILTER_ALL;
		tstamp_rx_ctrl = RCAR_GEN4_RXTSTAMP_ENABLED |
			RCAR_GEN4_RXTSTAMP_TYPE_ALL;
		break;
	}

	ptp_priv->tstamp_tx_ctrl = tstamp_tx_ctrl;
	ptp_priv->tstamp_rx_ctrl = tstamp_rx_ctrl;

	return 0;
}

static const struct net_device_ops rtsn_netdev_ops = {
	.ndo_open		= rtsn_open,
	.ndo_stop		= rtsn_stop,
	.ndo_start_xmit		= rtsn_start_xmit,
	.ndo_get_stats64	= rtsn_get_stats64,
	.ndo_eth_ioctl		= rtsn_do_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_hwtstamp_set	= rtsn_hwtstamp_set,
	.ndo_hwtstamp_get	= rtsn_hwtstamp_get,
};

static int rtsn_get_ts_info(struct net_device *ndev,
			    struct kernel_ethtool_ts_info *info)
{
	struct rtsn_private *priv = netdev_priv(ndev);

	info->phc_index = ptp_clock_index(priv->ptp_priv->clock);
	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) | BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}

static const struct ethtool_ops rtsn_ethtool_ops = {
	.nway_reset		= phy_ethtool_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_ts_info		= rtsn_get_ts_info,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
};

static const struct of_device_id rtsn_match_table[] = {
	{ .compatible = "renesas,r8a779g0-ethertsn", },
	{ /* Sentinel */ }
};

MODULE_DEVICE_TABLE(of, rtsn_match_table);

static int rtsn_probe(struct platform_device *pdev)
{
	struct rtsn_private *priv;
	struct net_device *ndev;
	struct resource *res;
	int ret;

	ndev = alloc_etherdev_mqs(sizeof(struct rtsn_private), TX_NUM_CHAINS,
				  RX_NUM_CHAINS);
	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);
	priv->pdev = pdev;
	priv->ndev = ndev;

	priv->ptp_priv = rcar_gen4_ptp_alloc(pdev);
	if (!priv->ptp_priv) {
		ret = -ENOMEM;
		goto error_free;
	}

	spin_lock_init(&priv->lock);
	platform_set_drvdata(pdev, priv);

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		ret = PTR_ERR(priv->clk);
		goto error_free;
	}

	priv->reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(priv->reset)) {
		ret = PTR_ERR(priv->reset);
		goto error_free;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "tsnes");
	if (!res) {
		dev_err(&pdev->dev, "Can't find tsnes resource\n");
		ret = -EINVAL;
		goto error_free;
	}

	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base)) {
		ret = PTR_ERR(priv->base);
		goto error_free;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);

	ndev->features = NETIF_F_RXCSUM;
	ndev->hw_features = NETIF_F_RXCSUM;
	ndev->base_addr = res->start;
	ndev->netdev_ops = &rtsn_netdev_ops;
	ndev->ethtool_ops = &rtsn_ethtool_ops;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gptp");
	if (!res) {
		dev_err(&pdev->dev, "Can't find gptp resource\n");
		ret = -EINVAL;
		goto error_free;
	}

	priv->ptp_priv->addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->ptp_priv->addr)) {
		ret = PTR_ERR(priv->ptp_priv->addr);
		goto error_free;
	}

	ret = rtsn_get_phy_params(priv);
	if (ret)
		goto error_free;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	netif_napi_add(ndev, &priv->napi, rtsn_poll);

	rtsn_parse_mac_address(pdev->dev.of_node, ndev);

	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));

	device_set_wakeup_capable(&pdev->dev, 1);

	ret = rcar_gen4_ptp_register(priv->ptp_priv, RCAR_GEN4_PTP_REG_LAYOUT,
				     clk_get_rate(priv->clk));
	if (ret)
		goto error_pm;

	ret = rtsn_mdio_alloc(priv);
	if (ret)
		goto error_ptp;

	ret = register_netdev(ndev);
	if (ret)
		goto error_mdio;

	netdev_info(ndev, "MAC address %pM\n", ndev->dev_addr);

	return 0;

error_mdio:
	rtsn_mdio_free(priv);
error_ptp:
	rcar_gen4_ptp_unregister(priv->ptp_priv);
error_pm:
	netif_napi_del(&priv->napi);
	rtsn_change_mode(priv, OCR_OPC_DISABLE);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
error_free:
	free_netdev(ndev);

	return ret;
}

static void rtsn_remove(struct platform_device *pdev)
{
	struct rtsn_private *priv = platform_get_drvdata(pdev);

	unregister_netdev(priv->ndev);
	rtsn_mdio_free(priv);
	rcar_gen4_ptp_unregister(priv->ptp_priv);
	rtsn_change_mode(priv, OCR_OPC_DISABLE);
	netif_napi_del(&priv->napi);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	free_netdev(priv->ndev);
}

static struct platform_driver rtsn_driver = {
	.probe		= rtsn_probe,
	.remove		= rtsn_remove,
	.driver	= {
		.name	= "rtsn",
		.of_match_table	= rtsn_match_table,
	}
};
module_platform_driver(rtsn_driver);

MODULE_AUTHOR("Phong Hoang, Niklas Söderlund");
MODULE_DESCRIPTION("Renesas Ethernet-TSN device driver");
MODULE_LICENSE("GPL");
