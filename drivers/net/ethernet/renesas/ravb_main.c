// SPDX-License-Identifier: GPL-2.0
/* Renesas Ethernet AVB device driver
 *
 * Copyright (C) 2014-2019 Renesas Electronics Corporation
 * Copyright (C) 2015 Renesas Solutions Corp.
 * Copyright (C) 2015-2016 Cogent Embedded, Inc. <source@cogentembedded.com>
 *
 * Based on the SuperH Ethernet driver
 */

#include <linux/cache.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/net_tstamp.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/reset.h>
#include <linux/math64.h>

#include "ravb.h"

#define RAVB_DEF_MSG_ENABLE \
		(NETIF_MSG_LINK	  | \
		 NETIF_MSG_TIMER  | \
		 NETIF_MSG_RX_ERR | \
		 NETIF_MSG_TX_ERR)

static const char *ravb_rx_irqs[NUM_RX_QUEUE] = {
	"ch0", /* RAVB_BE */
	"ch1", /* RAVB_NC */
};

static const char *ravb_tx_irqs[NUM_TX_QUEUE] = {
	"ch18", /* RAVB_BE */
	"ch19", /* RAVB_NC */
};

void ravb_modify(struct net_device *ndev, enum ravb_reg reg, u32 clear,
		 u32 set)
{
	ravb_write(ndev, (ravb_read(ndev, reg) & ~clear) | set, reg);
}

int ravb_wait(struct net_device *ndev, enum ravb_reg reg, u32 mask, u32 value)
{
	int i;

	for (i = 0; i < 10000; i++) {
		if ((ravb_read(ndev, reg) & mask) == value)
			return 0;
		udelay(10);
	}
	return -ETIMEDOUT;
}

static int ravb_config(struct net_device *ndev)
{
	int error;

	/* Set config mode */
	ravb_modify(ndev, CCC, CCC_OPC, CCC_OPC_CONFIG);
	/* Check if the operating mode is changed to the config mode */
	error = ravb_wait(ndev, CSR, CSR_OPS, CSR_OPS_CONFIG);
	if (error)
		netdev_err(ndev, "failed to switch device to config mode\n");

	return error;
}

static void ravb_set_rate_gbeth(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);

	switch (priv->speed) {
	case 10:                /* 10BASE */
		ravb_write(ndev, GBETH_GECMR_SPEED_10, GECMR);
		break;
	case 100:               /* 100BASE */
		ravb_write(ndev, GBETH_GECMR_SPEED_100, GECMR);
		break;
	case 1000:              /* 1000BASE */
		ravb_write(ndev, GBETH_GECMR_SPEED_1000, GECMR);
		break;
	}
}

static void ravb_set_rate_rcar(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);

	switch (priv->speed) {
	case 100:		/* 100BASE */
		ravb_write(ndev, GECMR_SPEED_100, GECMR);
		break;
	case 1000:		/* 1000BASE */
		ravb_write(ndev, GECMR_SPEED_1000, GECMR);
		break;
	}
}

static void ravb_set_buffer_align(struct sk_buff *skb)
{
	u32 reserve = (unsigned long)skb->data & (RAVB_ALIGN - 1);

	if (reserve)
		skb_reserve(skb, RAVB_ALIGN - reserve);
}

/* Get MAC address from the MAC address registers
 *
 * Ethernet AVB device doesn't have ROM for MAC address.
 * This function gets the MAC address that was used by a bootloader.
 */
static void ravb_read_mac_address(struct device_node *np,
				  struct net_device *ndev)
{
	int ret;

	ret = of_get_ethdev_address(np, ndev);
	if (ret) {
		u32 mahr = ravb_read(ndev, MAHR);
		u32 malr = ravb_read(ndev, MALR);
		u8 addr[ETH_ALEN];

		addr[0] = (mahr >> 24) & 0xFF;
		addr[1] = (mahr >> 16) & 0xFF;
		addr[2] = (mahr >>  8) & 0xFF;
		addr[3] = (mahr >>  0) & 0xFF;
		addr[4] = (malr >>  8) & 0xFF;
		addr[5] = (malr >>  0) & 0xFF;
		eth_hw_addr_set(ndev, addr);
	}
}

static void ravb_mdio_ctrl(struct mdiobb_ctrl *ctrl, u32 mask, int set)
{
	struct ravb_private *priv = container_of(ctrl, struct ravb_private,
						 mdiobb);

	ravb_modify(priv->ndev, PIR, mask, set ? mask : 0);
}

/* MDC pin control */
static void ravb_set_mdc(struct mdiobb_ctrl *ctrl, int level)
{
	ravb_mdio_ctrl(ctrl, PIR_MDC, level);
}

/* Data I/O pin control */
static void ravb_set_mdio_dir(struct mdiobb_ctrl *ctrl, int output)
{
	ravb_mdio_ctrl(ctrl, PIR_MMD, output);
}

/* Set data bit */
static void ravb_set_mdio_data(struct mdiobb_ctrl *ctrl, int value)
{
	ravb_mdio_ctrl(ctrl, PIR_MDO, value);
}

/* Get data bit */
static int ravb_get_mdio_data(struct mdiobb_ctrl *ctrl)
{
	struct ravb_private *priv = container_of(ctrl, struct ravb_private,
						 mdiobb);

	return (ravb_read(priv->ndev, PIR) & PIR_MDI) != 0;
}

/* MDIO bus control struct */
static const struct mdiobb_ops bb_ops = {
	.owner = THIS_MODULE,
	.set_mdc = ravb_set_mdc,
	.set_mdio_dir = ravb_set_mdio_dir,
	.set_mdio_data = ravb_set_mdio_data,
	.get_mdio_data = ravb_get_mdio_data,
};

/* Free TX skb function for AVB-IP */
static int ravb_tx_free(struct net_device *ndev, int q, bool free_txed_only)
{
	struct ravb_private *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &priv->stats[q];
	unsigned int num_tx_desc = priv->num_tx_desc;
	struct ravb_tx_desc *desc;
	unsigned int entry;
	int free_num = 0;
	u32 size;

	for (; priv->cur_tx[q] - priv->dirty_tx[q] > 0; priv->dirty_tx[q]++) {
		bool txed;

		entry = priv->dirty_tx[q] % (priv->num_tx_ring[q] *
					     num_tx_desc);
		desc = &priv->tx_ring[q][entry];
		txed = desc->die_dt == DT_FEMPTY;
		if (free_txed_only && !txed)
			break;
		/* Descriptor type must be checked before all other reads */
		dma_rmb();
		size = le16_to_cpu(desc->ds_tagl) & TX_DS;
		/* Free the original skb. */
		if (priv->tx_skb[q][entry / num_tx_desc]) {
			dma_unmap_single(ndev->dev.parent, le32_to_cpu(desc->dptr),
					 size, DMA_TO_DEVICE);
			/* Last packet descriptor? */
			if (entry % num_tx_desc == num_tx_desc - 1) {
				entry /= num_tx_desc;
				dev_kfree_skb_any(priv->tx_skb[q][entry]);
				priv->tx_skb[q][entry] = NULL;
				if (txed)
					stats->tx_packets++;
			}
			free_num++;
		}
		if (txed)
			stats->tx_bytes += size;
		desc->die_dt = DT_EEMPTY;
	}
	return free_num;
}

static void ravb_rx_ring_free_gbeth(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	unsigned int ring_size;
	unsigned int i;

	if (!priv->gbeth_rx_ring)
		return;

	for (i = 0; i < priv->num_rx_ring[q]; i++) {
		struct ravb_rx_desc *desc = &priv->gbeth_rx_ring[i];

		if (!dma_mapping_error(ndev->dev.parent,
				       le32_to_cpu(desc->dptr)))
			dma_unmap_single(ndev->dev.parent,
					 le32_to_cpu(desc->dptr),
					 GBETH_RX_BUFF_MAX,
					 DMA_FROM_DEVICE);
	}
	ring_size = sizeof(struct ravb_rx_desc) * (priv->num_rx_ring[q] + 1);
	dma_free_coherent(ndev->dev.parent, ring_size, priv->gbeth_rx_ring,
			  priv->rx_desc_dma[q]);
	priv->gbeth_rx_ring = NULL;
}

static void ravb_rx_ring_free_rcar(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	unsigned int ring_size;
	unsigned int i;

	if (!priv->rx_ring[q])
		return;

	for (i = 0; i < priv->num_rx_ring[q]; i++) {
		struct ravb_ex_rx_desc *desc = &priv->rx_ring[q][i];

		if (!dma_mapping_error(ndev->dev.parent,
				       le32_to_cpu(desc->dptr)))
			dma_unmap_single(ndev->dev.parent,
					 le32_to_cpu(desc->dptr),
					 RX_BUF_SZ,
					 DMA_FROM_DEVICE);
	}
	ring_size = sizeof(struct ravb_ex_rx_desc) *
		    (priv->num_rx_ring[q] + 1);
	dma_free_coherent(ndev->dev.parent, ring_size, priv->rx_ring[q],
			  priv->rx_desc_dma[q]);
	priv->rx_ring[q] = NULL;
}

/* Free skb's and DMA buffers for Ethernet AVB */
static void ravb_ring_free(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	unsigned int num_tx_desc = priv->num_tx_desc;
	unsigned int ring_size;
	unsigned int i;

	info->rx_ring_free(ndev, q);

	if (priv->tx_ring[q]) {
		ravb_tx_free(ndev, q, false);

		ring_size = sizeof(struct ravb_tx_desc) *
			    (priv->num_tx_ring[q] * num_tx_desc + 1);
		dma_free_coherent(ndev->dev.parent, ring_size, priv->tx_ring[q],
				  priv->tx_desc_dma[q]);
		priv->tx_ring[q] = NULL;
	}

	/* Free RX skb ringbuffer */
	if (priv->rx_skb[q]) {
		for (i = 0; i < priv->num_rx_ring[q]; i++)
			dev_kfree_skb(priv->rx_skb[q][i]);
	}
	kfree(priv->rx_skb[q]);
	priv->rx_skb[q] = NULL;

	/* Free aligned TX buffers */
	kfree(priv->tx_align[q]);
	priv->tx_align[q] = NULL;

	/* Free TX skb ringbuffer.
	 * SKBs are freed by ravb_tx_free() call above.
	 */
	kfree(priv->tx_skb[q]);
	priv->tx_skb[q] = NULL;
}

static void ravb_rx_ring_format_gbeth(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	struct ravb_rx_desc *rx_desc;
	unsigned int rx_ring_size;
	dma_addr_t dma_addr;
	unsigned int i;

	rx_ring_size = sizeof(*rx_desc) * priv->num_rx_ring[q];
	memset(priv->gbeth_rx_ring, 0, rx_ring_size);
	/* Build RX ring buffer */
	for (i = 0; i < priv->num_rx_ring[q]; i++) {
		/* RX descriptor */
		rx_desc = &priv->gbeth_rx_ring[i];
		rx_desc->ds_cc = cpu_to_le16(GBETH_RX_DESC_DATA_SIZE);
		dma_addr = dma_map_single(ndev->dev.parent, priv->rx_skb[q][i]->data,
					  GBETH_RX_BUFF_MAX,
					  DMA_FROM_DEVICE);
		/* We just set the data size to 0 for a failed mapping which
		 * should prevent DMA from happening...
		 */
		if (dma_mapping_error(ndev->dev.parent, dma_addr))
			rx_desc->ds_cc = cpu_to_le16(0);
		rx_desc->dptr = cpu_to_le32(dma_addr);
		rx_desc->die_dt = DT_FEMPTY;
	}
	rx_desc = &priv->gbeth_rx_ring[i];
	rx_desc->dptr = cpu_to_le32((u32)priv->rx_desc_dma[q]);
	rx_desc->die_dt = DT_LINKFIX; /* type */
}

static void ravb_rx_ring_format_rcar(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	struct ravb_ex_rx_desc *rx_desc;
	unsigned int rx_ring_size = sizeof(*rx_desc) * priv->num_rx_ring[q];
	dma_addr_t dma_addr;
	unsigned int i;

	memset(priv->rx_ring[q], 0, rx_ring_size);
	/* Build RX ring buffer */
	for (i = 0; i < priv->num_rx_ring[q]; i++) {
		/* RX descriptor */
		rx_desc = &priv->rx_ring[q][i];
		rx_desc->ds_cc = cpu_to_le16(RX_BUF_SZ);
		dma_addr = dma_map_single(ndev->dev.parent, priv->rx_skb[q][i]->data,
					  RX_BUF_SZ,
					  DMA_FROM_DEVICE);
		/* We just set the data size to 0 for a failed mapping which
		 * should prevent DMA from happening...
		 */
		if (dma_mapping_error(ndev->dev.parent, dma_addr))
			rx_desc->ds_cc = cpu_to_le16(0);
		rx_desc->dptr = cpu_to_le32(dma_addr);
		rx_desc->die_dt = DT_FEMPTY;
	}
	rx_desc = &priv->rx_ring[q][i];
	rx_desc->dptr = cpu_to_le32((u32)priv->rx_desc_dma[q]);
	rx_desc->die_dt = DT_LINKFIX; /* type */
}

/* Format skb and descriptor buffer for Ethernet AVB */
static void ravb_ring_format(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	unsigned int num_tx_desc = priv->num_tx_desc;
	struct ravb_tx_desc *tx_desc;
	struct ravb_desc *desc;
	unsigned int tx_ring_size = sizeof(*tx_desc) * priv->num_tx_ring[q] *
				    num_tx_desc;
	unsigned int i;

	priv->cur_rx[q] = 0;
	priv->cur_tx[q] = 0;
	priv->dirty_rx[q] = 0;
	priv->dirty_tx[q] = 0;

	info->rx_ring_format(ndev, q);

	memset(priv->tx_ring[q], 0, tx_ring_size);
	/* Build TX ring buffer */
	for (i = 0, tx_desc = priv->tx_ring[q]; i < priv->num_tx_ring[q];
	     i++, tx_desc++) {
		tx_desc->die_dt = DT_EEMPTY;
		if (num_tx_desc > 1) {
			tx_desc++;
			tx_desc->die_dt = DT_EEMPTY;
		}
	}
	tx_desc->dptr = cpu_to_le32((u32)priv->tx_desc_dma[q]);
	tx_desc->die_dt = DT_LINKFIX; /* type */

	/* RX descriptor base address for best effort */
	desc = &priv->desc_bat[RX_QUEUE_OFFSET + q];
	desc->die_dt = DT_LINKFIX; /* type */
	desc->dptr = cpu_to_le32((u32)priv->rx_desc_dma[q]);

	/* TX descriptor base address for best effort */
	desc = &priv->desc_bat[q];
	desc->die_dt = DT_LINKFIX; /* type */
	desc->dptr = cpu_to_le32((u32)priv->tx_desc_dma[q]);
}

static void *ravb_alloc_rx_desc_gbeth(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	unsigned int ring_size;

	ring_size = sizeof(struct ravb_rx_desc) * (priv->num_rx_ring[q] + 1);

	priv->gbeth_rx_ring = dma_alloc_coherent(ndev->dev.parent, ring_size,
						 &priv->rx_desc_dma[q],
						 GFP_KERNEL);
	return priv->gbeth_rx_ring;
}

static void *ravb_alloc_rx_desc_rcar(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	unsigned int ring_size;

	ring_size = sizeof(struct ravb_ex_rx_desc) * (priv->num_rx_ring[q] + 1);

	priv->rx_ring[q] = dma_alloc_coherent(ndev->dev.parent, ring_size,
					      &priv->rx_desc_dma[q],
					      GFP_KERNEL);
	return priv->rx_ring[q];
}

/* Init skb and descriptor buffer for Ethernet AVB */
static int ravb_ring_init(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	unsigned int num_tx_desc = priv->num_tx_desc;
	unsigned int ring_size;
	struct sk_buff *skb;
	unsigned int i;

	/* Allocate RX and TX skb rings */
	priv->rx_skb[q] = kcalloc(priv->num_rx_ring[q],
				  sizeof(*priv->rx_skb[q]), GFP_KERNEL);
	priv->tx_skb[q] = kcalloc(priv->num_tx_ring[q],
				  sizeof(*priv->tx_skb[q]), GFP_KERNEL);
	if (!priv->rx_skb[q] || !priv->tx_skb[q])
		goto error;

	for (i = 0; i < priv->num_rx_ring[q]; i++) {
		skb = __netdev_alloc_skb(ndev, info->max_rx_len, GFP_KERNEL);
		if (!skb)
			goto error;
		ravb_set_buffer_align(skb);
		priv->rx_skb[q][i] = skb;
	}

	if (num_tx_desc > 1) {
		/* Allocate rings for the aligned buffers */
		priv->tx_align[q] = kmalloc(DPTR_ALIGN * priv->num_tx_ring[q] +
					    DPTR_ALIGN - 1, GFP_KERNEL);
		if (!priv->tx_align[q])
			goto error;
	}

	/* Allocate all RX descriptors. */
	if (!info->alloc_rx_desc(ndev, q))
		goto error;

	priv->dirty_rx[q] = 0;

	/* Allocate all TX descriptors. */
	ring_size = sizeof(struct ravb_tx_desc) *
		    (priv->num_tx_ring[q] * num_tx_desc + 1);
	priv->tx_ring[q] = dma_alloc_coherent(ndev->dev.parent, ring_size,
					      &priv->tx_desc_dma[q],
					      GFP_KERNEL);
	if (!priv->tx_ring[q])
		goto error;

	return 0;

error:
	ravb_ring_free(ndev, q);

	return -ENOMEM;
}

static void ravb_emac_init_gbeth(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);

	if (priv->phy_interface == PHY_INTERFACE_MODE_MII) {
		ravb_write(ndev, (1000 << 16) | CXR35_SEL_XMII_MII, CXR35);
		ravb_modify(ndev, CXR31, CXR31_SEL_LINK0 | CXR31_SEL_LINK1, 0);
	} else {
		ravb_write(ndev, (1000 << 16) | CXR35_SEL_XMII_RGMII, CXR35);
		ravb_modify(ndev, CXR31, CXR31_SEL_LINK0 | CXR31_SEL_LINK1,
			    CXR31_SEL_LINK0);
	}

	/* Receive frame limit set register */
	ravb_write(ndev, GBETH_RX_BUFF_MAX + ETH_FCS_LEN, RFLR);

	/* EMAC Mode: PAUSE prohibition; Duplex; TX; RX; CRC Pass Through */
	ravb_write(ndev, ECMR_ZPF | ((priv->duplex > 0) ? ECMR_DM : 0) |
			 ECMR_TE | ECMR_RE | ECMR_RCPT |
			 ECMR_TXF | ECMR_RXF, ECMR);

	ravb_set_rate_gbeth(ndev);

	/* Set MAC address */
	ravb_write(ndev,
		   (ndev->dev_addr[0] << 24) | (ndev->dev_addr[1] << 16) |
		   (ndev->dev_addr[2] << 8)  | (ndev->dev_addr[3]), MAHR);
	ravb_write(ndev, (ndev->dev_addr[4] << 8)  | (ndev->dev_addr[5]), MALR);

	/* E-MAC status register clear */
	ravb_write(ndev, ECSR_ICD | ECSR_LCHNG | ECSR_PFRI, ECSR);
	ravb_write(ndev, CSR0_TPE | CSR0_RPE, CSR0);

	/* E-MAC interrupt enable register */
	ravb_write(ndev, ECSIPR_ICDIP, ECSIPR);
}

static void ravb_emac_init_rcar(struct net_device *ndev)
{
	/* Receive frame limit set register */
	ravb_write(ndev, ndev->mtu + ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN, RFLR);

	/* EMAC Mode: PAUSE prohibition; Duplex; RX Checksum; TX; RX */
	ravb_write(ndev, ECMR_ZPF | ECMR_DM |
		   (ndev->features & NETIF_F_RXCSUM ? ECMR_RCSC : 0) |
		   ECMR_TE | ECMR_RE, ECMR);

	ravb_set_rate_rcar(ndev);

	/* Set MAC address */
	ravb_write(ndev,
		   (ndev->dev_addr[0] << 24) | (ndev->dev_addr[1] << 16) |
		   (ndev->dev_addr[2] << 8)  | (ndev->dev_addr[3]), MAHR);
	ravb_write(ndev,
		   (ndev->dev_addr[4] << 8)  | (ndev->dev_addr[5]), MALR);

	/* E-MAC status register clear */
	ravb_write(ndev, ECSR_ICD | ECSR_MPD, ECSR);

	/* E-MAC interrupt enable register */
	ravb_write(ndev, ECSIPR_ICDIP | ECSIPR_MPDIP | ECSIPR_LCHNGIP, ECSIPR);
}

/* E-MAC init function */
static void ravb_emac_init(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;

	info->emac_init(ndev);
}

static int ravb_dmac_init_gbeth(struct net_device *ndev)
{
	int error;

	error = ravb_ring_init(ndev, RAVB_BE);
	if (error)
		return error;

	/* Descriptor format */
	ravb_ring_format(ndev, RAVB_BE);

	/* Set DMAC RX */
	ravb_write(ndev, 0x60000000, RCR);

	/* Set Max Frame Length (RTC) */
	ravb_write(ndev, 0x7ffc0000 | GBETH_RX_BUFF_MAX, RTC);

	/* Set FIFO size */
	ravb_write(ndev, 0x00222200, TGC);

	ravb_write(ndev, 0, TCCR);

	/* Frame receive */
	ravb_write(ndev, RIC0_FRE0, RIC0);
	/* Disable FIFO full warning */
	ravb_write(ndev, 0x0, RIC1);
	/* Receive FIFO full error, descriptor empty */
	ravb_write(ndev, RIC2_QFE0 | RIC2_RFFE, RIC2);

	ravb_write(ndev, TIC_FTE0, TIC);

	return 0;
}

static int ravb_dmac_init_rcar(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	int error;

	error = ravb_ring_init(ndev, RAVB_BE);
	if (error)
		return error;
	error = ravb_ring_init(ndev, RAVB_NC);
	if (error) {
		ravb_ring_free(ndev, RAVB_BE);
		return error;
	}

	/* Descriptor format */
	ravb_ring_format(ndev, RAVB_BE);
	ravb_ring_format(ndev, RAVB_NC);

	/* Set AVB RX */
	ravb_write(ndev,
		   RCR_EFFS | RCR_ENCF | RCR_ETS0 | RCR_ESF | 0x18000000, RCR);

	/* Set FIFO size */
	ravb_write(ndev, TGC_TQP_AVBMODE1 | 0x00112200, TGC);

	/* Timestamp enable */
	ravb_write(ndev, TCCR_TFEN, TCCR);

	/* Interrupt init: */
	if (info->multi_irqs) {
		/* Clear DIL.DPLx */
		ravb_write(ndev, 0, DIL);
		/* Set queue specific interrupt */
		ravb_write(ndev, CIE_CRIE | CIE_CTIE | CIE_CL0M, CIE);
	}
	/* Frame receive */
	ravb_write(ndev, RIC0_FRE0 | RIC0_FRE1, RIC0);
	/* Disable FIFO full warning */
	ravb_write(ndev, 0, RIC1);
	/* Receive FIFO full error, descriptor empty */
	ravb_write(ndev, RIC2_QFE0 | RIC2_QFE1 | RIC2_RFFE, RIC2);
	/* Frame transmitted, timestamp FIFO updated */
	ravb_write(ndev, TIC_FTE0 | TIC_FTE1 | TIC_TFUE, TIC);

	return 0;
}

/* Device init function for Ethernet AVB */
static int ravb_dmac_init(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	int error;

	/* Set CONFIG mode */
	error = ravb_config(ndev);
	if (error)
		return error;

	error = info->dmac_init(ndev);
	if (error)
		return error;

	/* Setting the control will start the AVB-DMAC process. */
	ravb_modify(ndev, CCC, CCC_OPC, CCC_OPC_OPERATION);

	return 0;
}

static void ravb_get_tx_tstamp(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	struct ravb_tstamp_skb *ts_skb, *ts_skb2;
	struct skb_shared_hwtstamps shhwtstamps;
	struct sk_buff *skb;
	struct timespec64 ts;
	u16 tag, tfa_tag;
	int count;
	u32 tfa2;

	count = (ravb_read(ndev, TSR) & TSR_TFFL) >> 8;
	while (count--) {
		tfa2 = ravb_read(ndev, TFA2);
		tfa_tag = (tfa2 & TFA2_TST) >> 16;
		ts.tv_nsec = (u64)ravb_read(ndev, TFA0);
		ts.tv_sec = ((u64)(tfa2 & TFA2_TSV) << 32) |
			    ravb_read(ndev, TFA1);
		memset(&shhwtstamps, 0, sizeof(shhwtstamps));
		shhwtstamps.hwtstamp = timespec64_to_ktime(ts);
		list_for_each_entry_safe(ts_skb, ts_skb2, &priv->ts_skb_list,
					 list) {
			skb = ts_skb->skb;
			tag = ts_skb->tag;
			list_del(&ts_skb->list);
			kfree(ts_skb);
			if (tag == tfa_tag) {
				skb_tstamp_tx(skb, &shhwtstamps);
				dev_consume_skb_any(skb);
				break;
			} else {
				dev_kfree_skb_any(skb);
			}
		}
		ravb_modify(ndev, TCCR, TCCR_TFR, TCCR_TFR);
	}
}

static void ravb_rx_csum(struct sk_buff *skb)
{
	u8 *hw_csum;

	/* The hardware checksum is contained in sizeof(__sum16) (2) bytes
	 * appended to packet data
	 */
	if (unlikely(skb->len < sizeof(__sum16)))
		return;
	hw_csum = skb_tail_pointer(skb) - sizeof(__sum16);
	skb->csum = csum_unfold((__force __sum16)get_unaligned_le16(hw_csum));
	skb->ip_summed = CHECKSUM_COMPLETE;
	skb_trim(skb, skb->len - sizeof(__sum16));
}

static struct sk_buff *ravb_get_skb_gbeth(struct net_device *ndev, int entry,
					  struct ravb_rx_desc *desc)
{
	struct ravb_private *priv = netdev_priv(ndev);
	struct sk_buff *skb;

	skb = priv->rx_skb[RAVB_BE][entry];
	priv->rx_skb[RAVB_BE][entry] = NULL;
	dma_unmap_single(ndev->dev.parent, le32_to_cpu(desc->dptr),
			 ALIGN(GBETH_RX_BUFF_MAX, 16), DMA_FROM_DEVICE);

	return skb;
}

/* Packet receive function for Gigabit Ethernet */
static bool ravb_rx_gbeth(struct net_device *ndev, int *quota, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	struct net_device_stats *stats;
	struct ravb_rx_desc *desc;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	u8  desc_status;
	int boguscnt;
	u16 pkt_len;
	u8  die_dt;
	int entry;
	int limit;

	entry = priv->cur_rx[q] % priv->num_rx_ring[q];
	boguscnt = priv->dirty_rx[q] + priv->num_rx_ring[q] - priv->cur_rx[q];
	stats = &priv->stats[q];

	boguscnt = min(boguscnt, *quota);
	limit = boguscnt;
	desc = &priv->gbeth_rx_ring[entry];
	while (desc->die_dt != DT_FEMPTY) {
		/* Descriptor type must be checked before all other reads */
		dma_rmb();
		desc_status = desc->msc;
		pkt_len = le16_to_cpu(desc->ds_cc) & RX_DS;

		if (--boguscnt < 0)
			break;

		/* We use 0-byte descriptors to mark the DMA mapping errors */
		if (!pkt_len)
			continue;

		if (desc_status & MSC_MC)
			stats->multicast++;

		if (desc_status & (MSC_CRC | MSC_RFE | MSC_RTSF | MSC_RTLF | MSC_CEEF)) {
			stats->rx_errors++;
			if (desc_status & MSC_CRC)
				stats->rx_crc_errors++;
			if (desc_status & MSC_RFE)
				stats->rx_frame_errors++;
			if (desc_status & (MSC_RTLF | MSC_RTSF))
				stats->rx_length_errors++;
			if (desc_status & MSC_CEEF)
				stats->rx_missed_errors++;
		} else {
			die_dt = desc->die_dt & 0xF0;
			switch (die_dt) {
			case DT_FSINGLE:
				skb = ravb_get_skb_gbeth(ndev, entry, desc);
				skb_put(skb, pkt_len);
				skb->protocol = eth_type_trans(skb, ndev);
				napi_gro_receive(&priv->napi[q], skb);
				stats->rx_packets++;
				stats->rx_bytes += pkt_len;
				break;
			case DT_FSTART:
				priv->rx_1st_skb = ravb_get_skb_gbeth(ndev, entry, desc);
				skb_put(priv->rx_1st_skb, pkt_len);
				break;
			case DT_FMID:
				skb = ravb_get_skb_gbeth(ndev, entry, desc);
				skb_copy_to_linear_data_offset(priv->rx_1st_skb,
							       priv->rx_1st_skb->len,
							       skb->data,
							       pkt_len);
				skb_put(priv->rx_1st_skb, pkt_len);
				dev_kfree_skb(skb);
				break;
			case DT_FEND:
				skb = ravb_get_skb_gbeth(ndev, entry, desc);
				skb_copy_to_linear_data_offset(priv->rx_1st_skb,
							       priv->rx_1st_skb->len,
							       skb->data,
							       pkt_len);
				skb_put(priv->rx_1st_skb, pkt_len);
				dev_kfree_skb(skb);
				priv->rx_1st_skb->protocol =
					eth_type_trans(priv->rx_1st_skb, ndev);
				napi_gro_receive(&priv->napi[q],
						 priv->rx_1st_skb);
				stats->rx_packets++;
				stats->rx_bytes += pkt_len;
				break;
			}
		}

		entry = (++priv->cur_rx[q]) % priv->num_rx_ring[q];
		desc = &priv->gbeth_rx_ring[entry];
	}

	/* Refill the RX ring buffers. */
	for (; priv->cur_rx[q] - priv->dirty_rx[q] > 0; priv->dirty_rx[q]++) {
		entry = priv->dirty_rx[q] % priv->num_rx_ring[q];
		desc = &priv->gbeth_rx_ring[entry];
		desc->ds_cc = cpu_to_le16(GBETH_RX_DESC_DATA_SIZE);

		if (!priv->rx_skb[q][entry]) {
			skb = netdev_alloc_skb(ndev, info->max_rx_len);
			if (!skb)
				break;
			ravb_set_buffer_align(skb);
			dma_addr = dma_map_single(ndev->dev.parent,
						  skb->data,
						  GBETH_RX_BUFF_MAX,
						  DMA_FROM_DEVICE);
			skb_checksum_none_assert(skb);
			/* We just set the data size to 0 for a failed mapping
			 * which should prevent DMA  from happening...
			 */
			if (dma_mapping_error(ndev->dev.parent, dma_addr))
				desc->ds_cc = cpu_to_le16(0);
			desc->dptr = cpu_to_le32(dma_addr);
			priv->rx_skb[q][entry] = skb;
		}
		/* Descriptor type must be set after all the above writes */
		dma_wmb();
		desc->die_dt = DT_FEMPTY;
	}

	*quota -= limit - (++boguscnt);

	return boguscnt <= 0;
}

/* Packet receive function for Ethernet AVB */
static bool ravb_rx_rcar(struct net_device *ndev, int *quota, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	int entry = priv->cur_rx[q] % priv->num_rx_ring[q];
	int boguscnt = (priv->dirty_rx[q] + priv->num_rx_ring[q]) -
			priv->cur_rx[q];
	struct net_device_stats *stats = &priv->stats[q];
	struct ravb_ex_rx_desc *desc;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	struct timespec64 ts;
	u8  desc_status;
	u16 pkt_len;
	int limit;

	boguscnt = min(boguscnt, *quota);
	limit = boguscnt;
	desc = &priv->rx_ring[q][entry];
	while (desc->die_dt != DT_FEMPTY) {
		/* Descriptor type must be checked before all other reads */
		dma_rmb();
		desc_status = desc->msc;
		pkt_len = le16_to_cpu(desc->ds_cc) & RX_DS;

		if (--boguscnt < 0)
			break;

		/* We use 0-byte descriptors to mark the DMA mapping errors */
		if (!pkt_len)
			continue;

		if (desc_status & MSC_MC)
			stats->multicast++;

		if (desc_status & (MSC_CRC | MSC_RFE | MSC_RTSF | MSC_RTLF |
				   MSC_CEEF)) {
			stats->rx_errors++;
			if (desc_status & MSC_CRC)
				stats->rx_crc_errors++;
			if (desc_status & MSC_RFE)
				stats->rx_frame_errors++;
			if (desc_status & (MSC_RTLF | MSC_RTSF))
				stats->rx_length_errors++;
			if (desc_status & MSC_CEEF)
				stats->rx_missed_errors++;
		} else {
			u32 get_ts = priv->tstamp_rx_ctrl & RAVB_RXTSTAMP_TYPE;

			skb = priv->rx_skb[q][entry];
			priv->rx_skb[q][entry] = NULL;
			dma_unmap_single(ndev->dev.parent, le32_to_cpu(desc->dptr),
					 RX_BUF_SZ,
					 DMA_FROM_DEVICE);
			get_ts &= (q == RAVB_NC) ?
					RAVB_RXTSTAMP_TYPE_V2_L2_EVENT :
					~RAVB_RXTSTAMP_TYPE_V2_L2_EVENT;
			if (get_ts) {
				struct skb_shared_hwtstamps *shhwtstamps;

				shhwtstamps = skb_hwtstamps(skb);
				memset(shhwtstamps, 0, sizeof(*shhwtstamps));
				ts.tv_sec = ((u64) le16_to_cpu(desc->ts_sh) <<
					     32) | le32_to_cpu(desc->ts_sl);
				ts.tv_nsec = le32_to_cpu(desc->ts_n);
				shhwtstamps->hwtstamp = timespec64_to_ktime(ts);
			}

			skb_put(skb, pkt_len);
			skb->protocol = eth_type_trans(skb, ndev);
			if (ndev->features & NETIF_F_RXCSUM)
				ravb_rx_csum(skb);
			napi_gro_receive(&priv->napi[q], skb);
			stats->rx_packets++;
			stats->rx_bytes += pkt_len;
		}

		entry = (++priv->cur_rx[q]) % priv->num_rx_ring[q];
		desc = &priv->rx_ring[q][entry];
	}

	/* Refill the RX ring buffers. */
	for (; priv->cur_rx[q] - priv->dirty_rx[q] > 0; priv->dirty_rx[q]++) {
		entry = priv->dirty_rx[q] % priv->num_rx_ring[q];
		desc = &priv->rx_ring[q][entry];
		desc->ds_cc = cpu_to_le16(RX_BUF_SZ);

		if (!priv->rx_skb[q][entry]) {
			skb = netdev_alloc_skb(ndev, info->max_rx_len);
			if (!skb)
				break;	/* Better luck next round. */
			ravb_set_buffer_align(skb);
			dma_addr = dma_map_single(ndev->dev.parent, skb->data,
						  le16_to_cpu(desc->ds_cc),
						  DMA_FROM_DEVICE);
			skb_checksum_none_assert(skb);
			/* We just set the data size to 0 for a failed mapping
			 * which should prevent DMA  from happening...
			 */
			if (dma_mapping_error(ndev->dev.parent, dma_addr))
				desc->ds_cc = cpu_to_le16(0);
			desc->dptr = cpu_to_le32(dma_addr);
			priv->rx_skb[q][entry] = skb;
		}
		/* Descriptor type must be set after all the above writes */
		dma_wmb();
		desc->die_dt = DT_FEMPTY;
	}

	*quota -= limit - (++boguscnt);

	return boguscnt <= 0;
}

/* Packet receive function for Ethernet AVB */
static bool ravb_rx(struct net_device *ndev, int *quota, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;

	return info->receive(ndev, quota, q);
}

static void ravb_rcv_snd_disable(struct net_device *ndev)
{
	/* Disable TX and RX */
	ravb_modify(ndev, ECMR, ECMR_RE | ECMR_TE, 0);
}

static void ravb_rcv_snd_enable(struct net_device *ndev)
{
	/* Enable TX and RX */
	ravb_modify(ndev, ECMR, ECMR_RE | ECMR_TE, ECMR_RE | ECMR_TE);
}

/* function for waiting dma process finished */
static int ravb_stop_dma(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	int error;

	/* Wait for stopping the hardware TX process */
	error = ravb_wait(ndev, TCCR, info->tccr_mask, 0);

	if (error)
		return error;

	error = ravb_wait(ndev, CSR, CSR_TPO0 | CSR_TPO1 | CSR_TPO2 | CSR_TPO3,
			  0);
	if (error)
		return error;

	/* Stop the E-MAC's RX/TX processes. */
	ravb_rcv_snd_disable(ndev);

	/* Wait for stopping the RX DMA process */
	error = ravb_wait(ndev, CSR, CSR_RPO, 0);
	if (error)
		return error;

	/* Stop AVB-DMAC process */
	return ravb_config(ndev);
}

/* E-MAC interrupt handler */
static void ravb_emac_interrupt_unlocked(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	u32 ecsr, psr;

	ecsr = ravb_read(ndev, ECSR);
	ravb_write(ndev, ecsr, ECSR);	/* clear interrupt */

	if (ecsr & ECSR_MPD)
		pm_wakeup_event(&priv->pdev->dev, 0);
	if (ecsr & ECSR_ICD)
		ndev->stats.tx_carrier_errors++;
	if (ecsr & ECSR_LCHNG) {
		/* Link changed */
		if (priv->no_avb_link)
			return;
		psr = ravb_read(ndev, PSR);
		if (priv->avb_link_active_low)
			psr ^= PSR_LMON;
		if (!(psr & PSR_LMON)) {
			/* DIsable RX and TX */
			ravb_rcv_snd_disable(ndev);
		} else {
			/* Enable RX and TX */
			ravb_rcv_snd_enable(ndev);
		}
	}
}

static irqreturn_t ravb_emac_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct ravb_private *priv = netdev_priv(ndev);

	spin_lock(&priv->lock);
	ravb_emac_interrupt_unlocked(ndev);
	spin_unlock(&priv->lock);
	return IRQ_HANDLED;
}

/* Error interrupt handler */
static void ravb_error_interrupt(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	u32 eis, ris2;

	eis = ravb_read(ndev, EIS);
	ravb_write(ndev, ~(EIS_QFS | EIS_RESERVED), EIS);
	if (eis & EIS_QFS) {
		ris2 = ravb_read(ndev, RIS2);
		ravb_write(ndev, ~(RIS2_QFF0 | RIS2_QFF1 | RIS2_RFFF | RIS2_RESERVED),
			   RIS2);

		/* Receive Descriptor Empty int */
		if (ris2 & RIS2_QFF0)
			priv->stats[RAVB_BE].rx_over_errors++;

		/* Receive Descriptor Empty int */
		if (ris2 & RIS2_QFF1)
			priv->stats[RAVB_NC].rx_over_errors++;

		/* Receive FIFO Overflow int */
		if (ris2 & RIS2_RFFF)
			priv->rx_fifo_errors++;
	}
}

static bool ravb_queue_interrupt(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	u32 ris0 = ravb_read(ndev, RIS0);
	u32 ric0 = ravb_read(ndev, RIC0);
	u32 tis  = ravb_read(ndev, TIS);
	u32 tic  = ravb_read(ndev, TIC);

	if (((ris0 & ric0) & BIT(q)) || ((tis  & tic)  & BIT(q))) {
		if (napi_schedule_prep(&priv->napi[q])) {
			/* Mask RX and TX interrupts */
			if (!info->irq_en_dis) {
				ravb_write(ndev, ric0 & ~BIT(q), RIC0);
				ravb_write(ndev, tic & ~BIT(q), TIC);
			} else {
				ravb_write(ndev, BIT(q), RID0);
				ravb_write(ndev, BIT(q), TID);
			}
			__napi_schedule(&priv->napi[q]);
		} else {
			netdev_warn(ndev,
				    "ignoring interrupt, rx status 0x%08x, rx mask 0x%08x,\n",
				    ris0, ric0);
			netdev_warn(ndev,
				    "                    tx status 0x%08x, tx mask 0x%08x.\n",
				    tis, tic);
		}
		return true;
	}
	return false;
}

static bool ravb_timestamp_interrupt(struct net_device *ndev)
{
	u32 tis = ravb_read(ndev, TIS);

	if (tis & TIS_TFUF) {
		ravb_write(ndev, ~(TIS_TFUF | TIS_RESERVED), TIS);
		ravb_get_tx_tstamp(ndev);
		return true;
	}
	return false;
}

static irqreturn_t ravb_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	irqreturn_t result = IRQ_NONE;
	u32 iss;

	spin_lock(&priv->lock);
	/* Get interrupt status */
	iss = ravb_read(ndev, ISS);

	/* Received and transmitted interrupts */
	if (iss & (ISS_FRS | ISS_FTS | ISS_TFUS)) {
		int q;

		/* Timestamp updated */
		if (ravb_timestamp_interrupt(ndev))
			result = IRQ_HANDLED;

		/* Network control and best effort queue RX/TX */
		if (info->nc_queues) {
			for (q = RAVB_NC; q >= RAVB_BE; q--) {
				if (ravb_queue_interrupt(ndev, q))
					result = IRQ_HANDLED;
			}
		} else {
			if (ravb_queue_interrupt(ndev, RAVB_BE))
				result = IRQ_HANDLED;
		}
	}

	/* E-MAC status summary */
	if (iss & ISS_MS) {
		ravb_emac_interrupt_unlocked(ndev);
		result = IRQ_HANDLED;
	}

	/* Error status summary */
	if (iss & ISS_ES) {
		ravb_error_interrupt(ndev);
		result = IRQ_HANDLED;
	}

	/* gPTP interrupt status summary */
	if (iss & ISS_CGIS) {
		ravb_ptp_interrupt(ndev);
		result = IRQ_HANDLED;
	}

	spin_unlock(&priv->lock);
	return result;
}

/* Timestamp/Error/gPTP interrupt handler */
static irqreturn_t ravb_multi_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct ravb_private *priv = netdev_priv(ndev);
	irqreturn_t result = IRQ_NONE;
	u32 iss;

	spin_lock(&priv->lock);
	/* Get interrupt status */
	iss = ravb_read(ndev, ISS);

	/* Timestamp updated */
	if ((iss & ISS_TFUS) && ravb_timestamp_interrupt(ndev))
		result = IRQ_HANDLED;

	/* Error status summary */
	if (iss & ISS_ES) {
		ravb_error_interrupt(ndev);
		result = IRQ_HANDLED;
	}

	/* gPTP interrupt status summary */
	if (iss & ISS_CGIS) {
		ravb_ptp_interrupt(ndev);
		result = IRQ_HANDLED;
	}

	spin_unlock(&priv->lock);
	return result;
}

static irqreturn_t ravb_dma_interrupt(int irq, void *dev_id, int q)
{
	struct net_device *ndev = dev_id;
	struct ravb_private *priv = netdev_priv(ndev);
	irqreturn_t result = IRQ_NONE;

	spin_lock(&priv->lock);

	/* Network control/Best effort queue RX/TX */
	if (ravb_queue_interrupt(ndev, q))
		result = IRQ_HANDLED;

	spin_unlock(&priv->lock);
	return result;
}

static irqreturn_t ravb_be_interrupt(int irq, void *dev_id)
{
	return ravb_dma_interrupt(irq, dev_id, RAVB_BE);
}

static irqreturn_t ravb_nc_interrupt(int irq, void *dev_id)
{
	return ravb_dma_interrupt(irq, dev_id, RAVB_NC);
}

static int ravb_poll(struct napi_struct *napi, int budget)
{
	struct net_device *ndev = napi->dev;
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	bool gptp = info->gptp || info->ccc_gac;
	struct ravb_rx_desc *desc;
	unsigned long flags;
	int q = napi - priv->napi;
	int mask = BIT(q);
	int quota = budget;
	unsigned int entry;

	if (!gptp) {
		entry = priv->cur_rx[q] % priv->num_rx_ring[q];
		desc = &priv->gbeth_rx_ring[entry];
	}
	/* Processing RX Descriptor Ring */
	/* Clear RX interrupt */
	ravb_write(ndev, ~(mask | RIS0_RESERVED), RIS0);
	if (gptp || desc->die_dt != DT_FEMPTY) {
		if (ravb_rx(ndev, &quota, q))
			goto out;
	}

	/* Processing TX Descriptor Ring */
	spin_lock_irqsave(&priv->lock, flags);
	/* Clear TX interrupt */
	ravb_write(ndev, ~(mask | TIS_RESERVED), TIS);
	ravb_tx_free(ndev, q, true);
	netif_wake_subqueue(ndev, q);
	spin_unlock_irqrestore(&priv->lock, flags);

	napi_complete(napi);

	/* Re-enable RX/TX interrupts */
	spin_lock_irqsave(&priv->lock, flags);
	if (!info->irq_en_dis) {
		ravb_modify(ndev, RIC0, mask, mask);
		ravb_modify(ndev, TIC,  mask, mask);
	} else {
		ravb_write(ndev, mask, RIE0);
		ravb_write(ndev, mask, TIE);
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	/* Receive error message handling */
	priv->rx_over_errors =  priv->stats[RAVB_BE].rx_over_errors;
	if (info->nc_queues)
		priv->rx_over_errors += priv->stats[RAVB_NC].rx_over_errors;
	if (priv->rx_over_errors != ndev->stats.rx_over_errors)
		ndev->stats.rx_over_errors = priv->rx_over_errors;
	if (priv->rx_fifo_errors != ndev->stats.rx_fifo_errors)
		ndev->stats.rx_fifo_errors = priv->rx_fifo_errors;
out:
	return budget - quota;
}

static void ravb_set_duplex_gbeth(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);

	ravb_modify(ndev, ECMR, ECMR_DM, priv->duplex > 0 ? ECMR_DM : 0);
}

/* PHY state control function */
static void ravb_adjust_link(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	struct phy_device *phydev = ndev->phydev;
	bool new_state = false;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	/* Disable TX and RX right over here, if E-MAC change is ignored */
	if (priv->no_avb_link)
		ravb_rcv_snd_disable(ndev);

	if (phydev->link) {
		if (info->half_duplex && phydev->duplex != priv->duplex) {
			new_state = true;
			priv->duplex = phydev->duplex;
			ravb_set_duplex_gbeth(ndev);
		}

		if (phydev->speed != priv->speed) {
			new_state = true;
			priv->speed = phydev->speed;
			info->set_rate(ndev);
		}
		if (!priv->link) {
			ravb_modify(ndev, ECMR, ECMR_TXF, 0);
			new_state = true;
			priv->link = phydev->link;
		}
	} else if (priv->link) {
		new_state = true;
		priv->link = 0;
		priv->speed = 0;
		if (info->half_duplex)
			priv->duplex = -1;
	}

	/* Enable TX and RX right over here, if E-MAC change is ignored */
	if (priv->no_avb_link && phydev->link)
		ravb_rcv_snd_enable(ndev);

	spin_unlock_irqrestore(&priv->lock, flags);

	if (new_state && netif_msg_link(priv))
		phy_print_status(phydev);
}

/* PHY init function */
static int ravb_phy_init(struct net_device *ndev)
{
	struct device_node *np = ndev->dev.parent->of_node;
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	struct phy_device *phydev;
	struct device_node *pn;
	phy_interface_t iface;
	int err;

	priv->link = 0;
	priv->speed = 0;
	priv->duplex = -1;

	/* Try connecting to PHY */
	pn = of_parse_phandle(np, "phy-handle", 0);
	if (!pn) {
		/* In the case of a fixed PHY, the DT node associated
		 * to the PHY is the Ethernet MAC DT node.
		 */
		if (of_phy_is_fixed_link(np)) {
			err = of_phy_register_fixed_link(np);
			if (err)
				return err;
		}
		pn = of_node_get(np);
	}

	iface = priv->rgmii_override ? PHY_INTERFACE_MODE_RGMII
				     : priv->phy_interface;
	phydev = of_phy_connect(ndev, pn, ravb_adjust_link, 0, iface);
	of_node_put(pn);
	if (!phydev) {
		netdev_err(ndev, "failed to connect PHY\n");
		err = -ENOENT;
		goto err_deregister_fixed_link;
	}

	if (!info->half_duplex) {
		/* 10BASE, Pause and Asym Pause is not supported */
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_Pause_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_Asym_Pause_BIT);

		/* Half Duplex is not supported */
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);
	}

	phy_attached_info(phydev);

	return 0;

err_deregister_fixed_link:
	if (of_phy_is_fixed_link(np))
		of_phy_deregister_fixed_link(np);

	return err;
}

/* PHY control start function */
static int ravb_phy_start(struct net_device *ndev)
{
	int error;

	error = ravb_phy_init(ndev);
	if (error)
		return error;

	phy_start(ndev->phydev);

	return 0;
}

static u32 ravb_get_msglevel(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);

	return priv->msg_enable;
}

static void ravb_set_msglevel(struct net_device *ndev, u32 value)
{
	struct ravb_private *priv = netdev_priv(ndev);

	priv->msg_enable = value;
}

static const char ravb_gstrings_stats_gbeth[][ETH_GSTRING_LEN] = {
	"rx_queue_0_current",
	"tx_queue_0_current",
	"rx_queue_0_dirty",
	"tx_queue_0_dirty",
	"rx_queue_0_packets",
	"tx_queue_0_packets",
	"rx_queue_0_bytes",
	"tx_queue_0_bytes",
	"rx_queue_0_mcast_packets",
	"rx_queue_0_errors",
	"rx_queue_0_crc_errors",
	"rx_queue_0_frame_errors",
	"rx_queue_0_length_errors",
	"rx_queue_0_csum_offload_errors",
	"rx_queue_0_over_errors",
};

static const char ravb_gstrings_stats[][ETH_GSTRING_LEN] = {
	"rx_queue_0_current",
	"tx_queue_0_current",
	"rx_queue_0_dirty",
	"tx_queue_0_dirty",
	"rx_queue_0_packets",
	"tx_queue_0_packets",
	"rx_queue_0_bytes",
	"tx_queue_0_bytes",
	"rx_queue_0_mcast_packets",
	"rx_queue_0_errors",
	"rx_queue_0_crc_errors",
	"rx_queue_0_frame_errors",
	"rx_queue_0_length_errors",
	"rx_queue_0_missed_errors",
	"rx_queue_0_over_errors",

	"rx_queue_1_current",
	"tx_queue_1_current",
	"rx_queue_1_dirty",
	"tx_queue_1_dirty",
	"rx_queue_1_packets",
	"tx_queue_1_packets",
	"rx_queue_1_bytes",
	"tx_queue_1_bytes",
	"rx_queue_1_mcast_packets",
	"rx_queue_1_errors",
	"rx_queue_1_crc_errors",
	"rx_queue_1_frame_errors",
	"rx_queue_1_length_errors",
	"rx_queue_1_missed_errors",
	"rx_queue_1_over_errors",
};

static int ravb_get_sset_count(struct net_device *netdev, int sset)
{
	struct ravb_private *priv = netdev_priv(netdev);
	const struct ravb_hw_info *info = priv->info;

	switch (sset) {
	case ETH_SS_STATS:
		return info->stats_len;
	default:
		return -EOPNOTSUPP;
	}
}

static void ravb_get_ethtool_stats(struct net_device *ndev,
				   struct ethtool_stats *estats, u64 *data)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	int num_rx_q;
	int i = 0;
	int q;

	num_rx_q = info->nc_queues ? NUM_RX_QUEUE : 1;
	/* Device-specific stats */
	for (q = RAVB_BE; q < num_rx_q; q++) {
		struct net_device_stats *stats = &priv->stats[q];

		data[i++] = priv->cur_rx[q];
		data[i++] = priv->cur_tx[q];
		data[i++] = priv->dirty_rx[q];
		data[i++] = priv->dirty_tx[q];
		data[i++] = stats->rx_packets;
		data[i++] = stats->tx_packets;
		data[i++] = stats->rx_bytes;
		data[i++] = stats->tx_bytes;
		data[i++] = stats->multicast;
		data[i++] = stats->rx_errors;
		data[i++] = stats->rx_crc_errors;
		data[i++] = stats->rx_frame_errors;
		data[i++] = stats->rx_length_errors;
		data[i++] = stats->rx_missed_errors;
		data[i++] = stats->rx_over_errors;
	}
}

static void ravb_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;

	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, info->gstrings_stats, info->gstrings_size);
		break;
	}
}

static void ravb_get_ringparam(struct net_device *ndev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
{
	struct ravb_private *priv = netdev_priv(ndev);

	ring->rx_max_pending = BE_RX_RING_MAX;
	ring->tx_max_pending = BE_TX_RING_MAX;
	ring->rx_pending = priv->num_rx_ring[RAVB_BE];
	ring->tx_pending = priv->num_tx_ring[RAVB_BE];
}

static int ravb_set_ringparam(struct net_device *ndev,
			      struct ethtool_ringparam *ring,
			      struct kernel_ethtool_ringparam *kernel_ring,
			      struct netlink_ext_ack *extack)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	int error;

	if (ring->tx_pending > BE_TX_RING_MAX ||
	    ring->rx_pending > BE_RX_RING_MAX ||
	    ring->tx_pending < BE_TX_RING_MIN ||
	    ring->rx_pending < BE_RX_RING_MIN)
		return -EINVAL;
	if (ring->rx_mini_pending || ring->rx_jumbo_pending)
		return -EINVAL;

	if (netif_running(ndev)) {
		netif_device_detach(ndev);
		/* Stop PTP Clock driver */
		if (info->gptp)
			ravb_ptp_stop(ndev);
		/* Wait for DMA stopping */
		error = ravb_stop_dma(ndev);
		if (error) {
			netdev_err(ndev,
				   "cannot set ringparam! Any AVB processes are still running?\n");
			return error;
		}
		synchronize_irq(ndev->irq);

		/* Free all the skb's in the RX queue and the DMA buffers. */
		ravb_ring_free(ndev, RAVB_BE);
		if (info->nc_queues)
			ravb_ring_free(ndev, RAVB_NC);
	}

	/* Set new parameters */
	priv->num_rx_ring[RAVB_BE] = ring->rx_pending;
	priv->num_tx_ring[RAVB_BE] = ring->tx_pending;

	if (netif_running(ndev)) {
		error = ravb_dmac_init(ndev);
		if (error) {
			netdev_err(ndev,
				   "%s: ravb_dmac_init() failed, error %d\n",
				   __func__, error);
			return error;
		}

		ravb_emac_init(ndev);

		/* Initialise PTP Clock driver */
		if (info->gptp)
			ravb_ptp_init(ndev, priv->pdev);

		netif_device_attach(ndev);
	}

	return 0;
}

static int ravb_get_ts_info(struct net_device *ndev,
			    struct ethtool_ts_info *info)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *hw_info = priv->info;

	info->so_timestamping =
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_SOFTWARE |
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);
	info->rx_filters =
		(1 << HWTSTAMP_FILTER_NONE) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
		(1 << HWTSTAMP_FILTER_ALL);
	if (hw_info->gptp || hw_info->ccc_gac)
		info->phc_index = ptp_clock_index(priv->ptp.clock);

	return 0;
}

static void ravb_get_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct ravb_private *priv = netdev_priv(ndev);

	wol->supported = WAKE_MAGIC;
	wol->wolopts = priv->wol_enabled ? WAKE_MAGIC : 0;
}

static int ravb_set_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;

	if (!info->magic_pkt || (wol->wolopts & ~WAKE_MAGIC))
		return -EOPNOTSUPP;

	priv->wol_enabled = !!(wol->wolopts & WAKE_MAGIC);

	device_set_wakeup_enable(&priv->pdev->dev, priv->wol_enabled);

	return 0;
}

static const struct ethtool_ops ravb_ethtool_ops = {
	.nway_reset		= phy_ethtool_nway_reset,
	.get_msglevel		= ravb_get_msglevel,
	.set_msglevel		= ravb_set_msglevel,
	.get_link		= ethtool_op_get_link,
	.get_strings		= ravb_get_strings,
	.get_ethtool_stats	= ravb_get_ethtool_stats,
	.get_sset_count		= ravb_get_sset_count,
	.get_ringparam		= ravb_get_ringparam,
	.set_ringparam		= ravb_set_ringparam,
	.get_ts_info		= ravb_get_ts_info,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.get_wol		= ravb_get_wol,
	.set_wol		= ravb_set_wol,
};

static inline int ravb_hook_irq(unsigned int irq, irq_handler_t handler,
				struct net_device *ndev, struct device *dev,
				const char *ch)
{
	char *name;
	int error;

	name = devm_kasprintf(dev, GFP_KERNEL, "%s:%s", ndev->name, ch);
	if (!name)
		return -ENOMEM;
	error = request_irq(irq, handler, 0, name, ndev);
	if (error)
		netdev_err(ndev, "cannot request IRQ %s\n", name);

	return error;
}

/* Network device open function for Ethernet AVB */
static int ravb_open(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	struct platform_device *pdev = priv->pdev;
	struct device *dev = &pdev->dev;
	int error;

	napi_enable(&priv->napi[RAVB_BE]);
	if (info->nc_queues)
		napi_enable(&priv->napi[RAVB_NC]);

	if (!info->multi_irqs) {
		error = request_irq(ndev->irq, ravb_interrupt, IRQF_SHARED,
				    ndev->name, ndev);
		if (error) {
			netdev_err(ndev, "cannot request IRQ\n");
			goto out_napi_off;
		}
	} else {
		error = ravb_hook_irq(ndev->irq, ravb_multi_interrupt, ndev,
				      dev, "ch22:multi");
		if (error)
			goto out_napi_off;
		error = ravb_hook_irq(priv->emac_irq, ravb_emac_interrupt, ndev,
				      dev, "ch24:emac");
		if (error)
			goto out_free_irq;
		error = ravb_hook_irq(priv->rx_irqs[RAVB_BE], ravb_be_interrupt,
				      ndev, dev, "ch0:rx_be");
		if (error)
			goto out_free_irq_emac;
		error = ravb_hook_irq(priv->tx_irqs[RAVB_BE], ravb_be_interrupt,
				      ndev, dev, "ch18:tx_be");
		if (error)
			goto out_free_irq_be_rx;
		error = ravb_hook_irq(priv->rx_irqs[RAVB_NC], ravb_nc_interrupt,
				      ndev, dev, "ch1:rx_nc");
		if (error)
			goto out_free_irq_be_tx;
		error = ravb_hook_irq(priv->tx_irqs[RAVB_NC], ravb_nc_interrupt,
				      ndev, dev, "ch19:tx_nc");
		if (error)
			goto out_free_irq_nc_rx;

		if (info->err_mgmt_irqs) {
			error = ravb_hook_irq(priv->erra_irq, ravb_multi_interrupt,
					      ndev, dev, "err_a");
			if (error)
				goto out_free_irq_nc_tx;
			error = ravb_hook_irq(priv->mgmta_irq, ravb_multi_interrupt,
					      ndev, dev, "mgmt_a");
			if (error)
				goto out_free_irq_erra;
		}
	}

	/* Device init */
	error = ravb_dmac_init(ndev);
	if (error)
		goto out_free_irq_mgmta;
	ravb_emac_init(ndev);

	/* Initialise PTP Clock driver */
	if (info->gptp)
		ravb_ptp_init(ndev, priv->pdev);

	/* PHY control start */
	error = ravb_phy_start(ndev);
	if (error)
		goto out_ptp_stop;

	netif_tx_start_all_queues(ndev);

	return 0;

out_ptp_stop:
	/* Stop PTP Clock driver */
	if (info->gptp)
		ravb_ptp_stop(ndev);
	ravb_stop_dma(ndev);
out_free_irq_mgmta:
	if (!info->multi_irqs)
		goto out_free_irq;
	if (info->err_mgmt_irqs)
		free_irq(priv->mgmta_irq, ndev);
out_free_irq_erra:
	if (info->err_mgmt_irqs)
		free_irq(priv->erra_irq, ndev);
out_free_irq_nc_tx:
	free_irq(priv->tx_irqs[RAVB_NC], ndev);
out_free_irq_nc_rx:
	free_irq(priv->rx_irqs[RAVB_NC], ndev);
out_free_irq_be_tx:
	free_irq(priv->tx_irqs[RAVB_BE], ndev);
out_free_irq_be_rx:
	free_irq(priv->rx_irqs[RAVB_BE], ndev);
out_free_irq_emac:
	free_irq(priv->emac_irq, ndev);
out_free_irq:
	free_irq(ndev->irq, ndev);
out_napi_off:
	if (info->nc_queues)
		napi_disable(&priv->napi[RAVB_NC]);
	napi_disable(&priv->napi[RAVB_BE]);
	return error;
}

/* Timeout function for Ethernet AVB */
static void ravb_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct ravb_private *priv = netdev_priv(ndev);

	netif_err(priv, tx_err, ndev,
		  "transmit timed out, status %08x, resetting...\n",
		  ravb_read(ndev, ISS));

	/* tx_errors count up */
	ndev->stats.tx_errors++;

	schedule_work(&priv->work);
}

static void ravb_tx_timeout_work(struct work_struct *work)
{
	struct ravb_private *priv = container_of(work, struct ravb_private,
						 work);
	const struct ravb_hw_info *info = priv->info;
	struct net_device *ndev = priv->ndev;
	int error;

	if (!rtnl_trylock()) {
		usleep_range(1000, 2000);
		schedule_work(&priv->work);
		return;
	}

	netif_tx_stop_all_queues(ndev);

	/* Stop PTP Clock driver */
	if (info->gptp)
		ravb_ptp_stop(ndev);

	/* Wait for DMA stopping */
	if (ravb_stop_dma(ndev)) {
		/* If ravb_stop_dma() fails, the hardware is still operating
		 * for TX and/or RX. So, this should not call the following
		 * functions because ravb_dmac_init() is possible to fail too.
		 * Also, this should not retry ravb_stop_dma() again and again
		 * here because it's possible to wait forever. So, this just
		 * re-enables the TX and RX and skip the following
		 * re-initialization procedure.
		 */
		ravb_rcv_snd_enable(ndev);
		goto out;
	}

	ravb_ring_free(ndev, RAVB_BE);
	if (info->nc_queues)
		ravb_ring_free(ndev, RAVB_NC);

	/* Device init */
	error = ravb_dmac_init(ndev);
	if (error) {
		/* If ravb_dmac_init() fails, descriptors are freed. So, this
		 * should return here to avoid re-enabling the TX and RX in
		 * ravb_emac_init().
		 */
		netdev_err(ndev, "%s: ravb_dmac_init() failed, error %d\n",
			   __func__, error);
		goto out_unlock;
	}
	ravb_emac_init(ndev);

out:
	/* Initialise PTP Clock driver */
	if (info->gptp)
		ravb_ptp_init(ndev, priv->pdev);

	netif_tx_start_all_queues(ndev);

out_unlock:
	rtnl_unlock();
}

/* Packet transmit function for Ethernet AVB */
static netdev_tx_t ravb_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	unsigned int num_tx_desc = priv->num_tx_desc;
	u16 q = skb_get_queue_mapping(skb);
	struct ravb_tstamp_skb *ts_skb;
	struct ravb_tx_desc *desc;
	unsigned long flags;
	u32 dma_addr;
	void *buffer;
	u32 entry;
	u32 len;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->cur_tx[q] - priv->dirty_tx[q] > (priv->num_tx_ring[q] - 1) *
	    num_tx_desc) {
		netif_err(priv, tx_queued, ndev,
			  "still transmitting with the full ring!\n");
		netif_stop_subqueue(ndev, q);
		spin_unlock_irqrestore(&priv->lock, flags);
		return NETDEV_TX_BUSY;
	}

	if (skb_put_padto(skb, ETH_ZLEN))
		goto exit;

	entry = priv->cur_tx[q] % (priv->num_tx_ring[q] * num_tx_desc);
	priv->tx_skb[q][entry / num_tx_desc] = skb;

	if (num_tx_desc > 1) {
		buffer = PTR_ALIGN(priv->tx_align[q], DPTR_ALIGN) +
			 entry / num_tx_desc * DPTR_ALIGN;
		len = PTR_ALIGN(skb->data, DPTR_ALIGN) - skb->data;

		/* Zero length DMA descriptors are problematic as they seem
		 * to terminate DMA transfers. Avoid them by simply using a
		 * length of DPTR_ALIGN (4) when skb data is aligned to
		 * DPTR_ALIGN.
		 *
		 * As skb is guaranteed to have at least ETH_ZLEN (60)
		 * bytes of data by the call to skb_put_padto() above this
		 * is safe with respect to both the length of the first DMA
		 * descriptor (len) overflowing the available data and the
		 * length of the second DMA descriptor (skb->len - len)
		 * being negative.
		 */
		if (len == 0)
			len = DPTR_ALIGN;

		memcpy(buffer, skb->data, len);
		dma_addr = dma_map_single(ndev->dev.parent, buffer, len,
					  DMA_TO_DEVICE);
		if (dma_mapping_error(ndev->dev.parent, dma_addr))
			goto drop;

		desc = &priv->tx_ring[q][entry];
		desc->ds_tagl = cpu_to_le16(len);
		desc->dptr = cpu_to_le32(dma_addr);

		buffer = skb->data + len;
		len = skb->len - len;
		dma_addr = dma_map_single(ndev->dev.parent, buffer, len,
					  DMA_TO_DEVICE);
		if (dma_mapping_error(ndev->dev.parent, dma_addr))
			goto unmap;

		desc++;
	} else {
		desc = &priv->tx_ring[q][entry];
		len = skb->len;
		dma_addr = dma_map_single(ndev->dev.parent, skb->data, skb->len,
					  DMA_TO_DEVICE);
		if (dma_mapping_error(ndev->dev.parent, dma_addr))
			goto drop;
	}
	desc->ds_tagl = cpu_to_le16(len);
	desc->dptr = cpu_to_le32(dma_addr);

	/* TX timestamp required */
	if (info->gptp || info->ccc_gac) {
		if (q == RAVB_NC) {
			ts_skb = kmalloc(sizeof(*ts_skb), GFP_ATOMIC);
			if (!ts_skb) {
				if (num_tx_desc > 1) {
					desc--;
					dma_unmap_single(ndev->dev.parent, dma_addr,
							 len, DMA_TO_DEVICE);
				}
				goto unmap;
			}
			ts_skb->skb = skb_get(skb);
			ts_skb->tag = priv->ts_skb_tag++;
			priv->ts_skb_tag &= 0x3ff;
			list_add_tail(&ts_skb->list, &priv->ts_skb_list);

			/* TAG and timestamp required flag */
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
			desc->tagh_tsr = (ts_skb->tag >> 4) | TX_TSR;
			desc->ds_tagl |= cpu_to_le16(ts_skb->tag << 12);
		}

		skb_tx_timestamp(skb);
	}
	/* Descriptor type must be set after all the above writes */
	dma_wmb();
	if (num_tx_desc > 1) {
		desc->die_dt = DT_FEND;
		desc--;
		desc->die_dt = DT_FSTART;
	} else {
		desc->die_dt = DT_FSINGLE;
	}
	ravb_modify(ndev, TCCR, TCCR_TSRQ0 << q, TCCR_TSRQ0 << q);

	priv->cur_tx[q] += num_tx_desc;
	if (priv->cur_tx[q] - priv->dirty_tx[q] >
	    (priv->num_tx_ring[q] - 1) * num_tx_desc &&
	    !ravb_tx_free(ndev, q, true))
		netif_stop_subqueue(ndev, q);

exit:
	spin_unlock_irqrestore(&priv->lock, flags);
	return NETDEV_TX_OK;

unmap:
	dma_unmap_single(ndev->dev.parent, le32_to_cpu(desc->dptr),
			 le16_to_cpu(desc->ds_tagl), DMA_TO_DEVICE);
drop:
	dev_kfree_skb_any(skb);
	priv->tx_skb[q][entry / num_tx_desc] = NULL;
	goto exit;
}

static u16 ravb_select_queue(struct net_device *ndev, struct sk_buff *skb,
			     struct net_device *sb_dev)
{
	/* If skb needs TX timestamp, it is handled in network control queue */
	return (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) ? RAVB_NC :
							       RAVB_BE;

}

static struct net_device_stats *ravb_get_stats(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	struct net_device_stats *nstats, *stats0, *stats1;

	nstats = &ndev->stats;
	stats0 = &priv->stats[RAVB_BE];

	if (info->tx_counters) {
		nstats->tx_dropped += ravb_read(ndev, TROCR);
		ravb_write(ndev, 0, TROCR);	/* (write clear) */
	}

	if (info->carrier_counters) {
		nstats->collisions += ravb_read(ndev, CXR41);
		ravb_write(ndev, 0, CXR41);	/* (write clear) */
		nstats->tx_carrier_errors += ravb_read(ndev, CXR42);
		ravb_write(ndev, 0, CXR42);	/* (write clear) */
	}

	nstats->rx_packets = stats0->rx_packets;
	nstats->tx_packets = stats0->tx_packets;
	nstats->rx_bytes = stats0->rx_bytes;
	nstats->tx_bytes = stats0->tx_bytes;
	nstats->multicast = stats0->multicast;
	nstats->rx_errors = stats0->rx_errors;
	nstats->rx_crc_errors = stats0->rx_crc_errors;
	nstats->rx_frame_errors = stats0->rx_frame_errors;
	nstats->rx_length_errors = stats0->rx_length_errors;
	nstats->rx_missed_errors = stats0->rx_missed_errors;
	nstats->rx_over_errors = stats0->rx_over_errors;
	if (info->nc_queues) {
		stats1 = &priv->stats[RAVB_NC];

		nstats->rx_packets += stats1->rx_packets;
		nstats->tx_packets += stats1->tx_packets;
		nstats->rx_bytes += stats1->rx_bytes;
		nstats->tx_bytes += stats1->tx_bytes;
		nstats->multicast += stats1->multicast;
		nstats->rx_errors += stats1->rx_errors;
		nstats->rx_crc_errors += stats1->rx_crc_errors;
		nstats->rx_frame_errors += stats1->rx_frame_errors;
		nstats->rx_length_errors += stats1->rx_length_errors;
		nstats->rx_missed_errors += stats1->rx_missed_errors;
		nstats->rx_over_errors += stats1->rx_over_errors;
	}

	return nstats;
}

/* Update promiscuous bit */
static void ravb_set_rx_mode(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	ravb_modify(ndev, ECMR, ECMR_PRM,
		    ndev->flags & IFF_PROMISC ? ECMR_PRM : 0);
	spin_unlock_irqrestore(&priv->lock, flags);
}

/* Device close function for Ethernet AVB */
static int ravb_close(struct net_device *ndev)
{
	struct device_node *np = ndev->dev.parent->of_node;
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	struct ravb_tstamp_skb *ts_skb, *ts_skb2;

	netif_tx_stop_all_queues(ndev);

	/* Disable interrupts by clearing the interrupt masks. */
	ravb_write(ndev, 0, RIC0);
	ravb_write(ndev, 0, RIC2);
	ravb_write(ndev, 0, TIC);

	/* Stop PTP Clock driver */
	if (info->gptp)
		ravb_ptp_stop(ndev);

	/* Set the config mode to stop the AVB-DMAC's processes */
	if (ravb_stop_dma(ndev) < 0)
		netdev_err(ndev,
			   "device will be stopped after h/w processes are done.\n");

	/* Clear the timestamp list */
	if (info->gptp || info->ccc_gac) {
		list_for_each_entry_safe(ts_skb, ts_skb2, &priv->ts_skb_list, list) {
			list_del(&ts_skb->list);
			kfree_skb(ts_skb->skb);
			kfree(ts_skb);
		}
	}

	/* PHY disconnect */
	if (ndev->phydev) {
		phy_stop(ndev->phydev);
		phy_disconnect(ndev->phydev);
		if (of_phy_is_fixed_link(np))
			of_phy_deregister_fixed_link(np);
	}

	cancel_work_sync(&priv->work);

	if (info->multi_irqs) {
		free_irq(priv->tx_irqs[RAVB_NC], ndev);
		free_irq(priv->rx_irqs[RAVB_NC], ndev);
		free_irq(priv->tx_irqs[RAVB_BE], ndev);
		free_irq(priv->rx_irqs[RAVB_BE], ndev);
		free_irq(priv->emac_irq, ndev);
		if (info->err_mgmt_irqs) {
			free_irq(priv->erra_irq, ndev);
			free_irq(priv->mgmta_irq, ndev);
		}
	}
	free_irq(ndev->irq, ndev);

	if (info->nc_queues)
		napi_disable(&priv->napi[RAVB_NC]);
	napi_disable(&priv->napi[RAVB_BE]);

	/* Free all the skb's in the RX queue and the DMA buffers. */
	ravb_ring_free(ndev, RAVB_BE);
	if (info->nc_queues)
		ravb_ring_free(ndev, RAVB_NC);

	return 0;
}

static int ravb_hwtstamp_get(struct net_device *ndev, struct ifreq *req)
{
	struct ravb_private *priv = netdev_priv(ndev);
	struct hwtstamp_config config;

	config.flags = 0;
	config.tx_type = priv->tstamp_tx_ctrl ? HWTSTAMP_TX_ON :
						HWTSTAMP_TX_OFF;
	switch (priv->tstamp_rx_ctrl & RAVB_RXTSTAMP_TYPE) {
	case RAVB_RXTSTAMP_TYPE_V2_L2_EVENT:
		config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		break;
	case RAVB_RXTSTAMP_TYPE_ALL:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		config.rx_filter = HWTSTAMP_FILTER_NONE;
	}

	return copy_to_user(req->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

/* Control hardware time stamping */
static int ravb_hwtstamp_set(struct net_device *ndev, struct ifreq *req)
{
	struct ravb_private *priv = netdev_priv(ndev);
	struct hwtstamp_config config;
	u32 tstamp_rx_ctrl = RAVB_RXTSTAMP_ENABLED;
	u32 tstamp_tx_ctrl;

	if (copy_from_user(&config, req->ifr_data, sizeof(config)))
		return -EFAULT;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		tstamp_tx_ctrl = 0;
		break;
	case HWTSTAMP_TX_ON:
		tstamp_tx_ctrl = RAVB_TXTSTAMP_ENABLED;
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		tstamp_rx_ctrl = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		tstamp_rx_ctrl |= RAVB_RXTSTAMP_TYPE_V2_L2_EVENT;
		break;
	default:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		tstamp_rx_ctrl |= RAVB_RXTSTAMP_TYPE_ALL;
	}

	priv->tstamp_tx_ctrl = tstamp_tx_ctrl;
	priv->tstamp_rx_ctrl = tstamp_rx_ctrl;

	return copy_to_user(req->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

/* ioctl to device function */
static int ravb_do_ioctl(struct net_device *ndev, struct ifreq *req, int cmd)
{
	struct phy_device *phydev = ndev->phydev;

	if (!netif_running(ndev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	switch (cmd) {
	case SIOCGHWTSTAMP:
		return ravb_hwtstamp_get(ndev, req);
	case SIOCSHWTSTAMP:
		return ravb_hwtstamp_set(ndev, req);
	}

	return phy_mii_ioctl(phydev, req, cmd);
}

static int ravb_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct ravb_private *priv = netdev_priv(ndev);

	ndev->mtu = new_mtu;

	if (netif_running(ndev)) {
		synchronize_irq(priv->emac_irq);
		ravb_emac_init(ndev);
	}

	netdev_update_features(ndev);

	return 0;
}

static void ravb_set_rx_csum(struct net_device *ndev, bool enable)
{
	struct ravb_private *priv = netdev_priv(ndev);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	/* Disable TX and RX */
	ravb_rcv_snd_disable(ndev);

	/* Modify RX Checksum setting */
	ravb_modify(ndev, ECMR, ECMR_RCSC, enable ? ECMR_RCSC : 0);

	/* Enable TX and RX */
	ravb_rcv_snd_enable(ndev);

	spin_unlock_irqrestore(&priv->lock, flags);
}

static int ravb_set_features_gbeth(struct net_device *ndev,
				   netdev_features_t features)
{
	/* Place holder */
	return 0;
}

static int ravb_set_features_rcar(struct net_device *ndev,
				  netdev_features_t features)
{
	netdev_features_t changed = ndev->features ^ features;

	if (changed & NETIF_F_RXCSUM)
		ravb_set_rx_csum(ndev, features & NETIF_F_RXCSUM);

	ndev->features = features;

	return 0;
}

static int ravb_set_features(struct net_device *ndev,
			     netdev_features_t features)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;

	return info->set_feature(ndev, features);
}

static const struct net_device_ops ravb_netdev_ops = {
	.ndo_open		= ravb_open,
	.ndo_stop		= ravb_close,
	.ndo_start_xmit		= ravb_start_xmit,
	.ndo_select_queue	= ravb_select_queue,
	.ndo_get_stats		= ravb_get_stats,
	.ndo_set_rx_mode	= ravb_set_rx_mode,
	.ndo_tx_timeout		= ravb_tx_timeout,
	.ndo_eth_ioctl		= ravb_do_ioctl,
	.ndo_change_mtu		= ravb_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_set_features	= ravb_set_features,
};

/* MDIO bus init function */
static int ravb_mdio_init(struct ravb_private *priv)
{
	struct platform_device *pdev = priv->pdev;
	struct device *dev = &pdev->dev;
	struct phy_device *phydev;
	struct device_node *pn;
	int error;

	/* Bitbang init */
	priv->mdiobb.ops = &bb_ops;

	/* MII controller setting */
	priv->mii_bus = alloc_mdio_bitbang(&priv->mdiobb);
	if (!priv->mii_bus)
		return -ENOMEM;

	/* Hook up MII support for ethtool */
	priv->mii_bus->name = "ravb_mii";
	priv->mii_bus->parent = dev;
	snprintf(priv->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		 pdev->name, pdev->id);

	/* Register MDIO bus */
	error = of_mdiobus_register(priv->mii_bus, dev->of_node);
	if (error)
		goto out_free_bus;

	pn = of_parse_phandle(dev->of_node, "phy-handle", 0);
	phydev = of_phy_find_device(pn);
	if (phydev) {
		phydev->mac_managed_pm = true;
		put_device(&phydev->mdio.dev);
	}
	of_node_put(pn);

	return 0;

out_free_bus:
	free_mdio_bitbang(priv->mii_bus);
	return error;
}

/* MDIO bus release function */
static int ravb_mdio_release(struct ravb_private *priv)
{
	/* Unregister mdio bus */
	mdiobus_unregister(priv->mii_bus);

	/* Free bitbang info */
	free_mdio_bitbang(priv->mii_bus);

	return 0;
}

static const struct ravb_hw_info ravb_gen3_hw_info = {
	.rx_ring_free = ravb_rx_ring_free_rcar,
	.rx_ring_format = ravb_rx_ring_format_rcar,
	.alloc_rx_desc = ravb_alloc_rx_desc_rcar,
	.receive = ravb_rx_rcar,
	.set_rate = ravb_set_rate_rcar,
	.set_feature = ravb_set_features_rcar,
	.dmac_init = ravb_dmac_init_rcar,
	.emac_init = ravb_emac_init_rcar,
	.gstrings_stats = ravb_gstrings_stats,
	.gstrings_size = sizeof(ravb_gstrings_stats),
	.net_hw_features = NETIF_F_RXCSUM,
	.net_features = NETIF_F_RXCSUM,
	.stats_len = ARRAY_SIZE(ravb_gstrings_stats),
	.max_rx_len = RX_BUF_SZ + RAVB_ALIGN - 1,
	.tccr_mask = TCCR_TSRQ0 | TCCR_TSRQ1 | TCCR_TSRQ2 | TCCR_TSRQ3,
	.rx_max_buf_size = SZ_2K,
	.internal_delay = 1,
	.tx_counters = 1,
	.multi_irqs = 1,
	.irq_en_dis = 1,
	.ccc_gac = 1,
	.nc_queues = 1,
	.magic_pkt = 1,
};

static const struct ravb_hw_info ravb_gen2_hw_info = {
	.rx_ring_free = ravb_rx_ring_free_rcar,
	.rx_ring_format = ravb_rx_ring_format_rcar,
	.alloc_rx_desc = ravb_alloc_rx_desc_rcar,
	.receive = ravb_rx_rcar,
	.set_rate = ravb_set_rate_rcar,
	.set_feature = ravb_set_features_rcar,
	.dmac_init = ravb_dmac_init_rcar,
	.emac_init = ravb_emac_init_rcar,
	.gstrings_stats = ravb_gstrings_stats,
	.gstrings_size = sizeof(ravb_gstrings_stats),
	.net_hw_features = NETIF_F_RXCSUM,
	.net_features = NETIF_F_RXCSUM,
	.stats_len = ARRAY_SIZE(ravb_gstrings_stats),
	.max_rx_len = RX_BUF_SZ + RAVB_ALIGN - 1,
	.tccr_mask = TCCR_TSRQ0 | TCCR_TSRQ1 | TCCR_TSRQ2 | TCCR_TSRQ3,
	.rx_max_buf_size = SZ_2K,
	.aligned_tx = 1,
	.gptp = 1,
	.nc_queues = 1,
	.magic_pkt = 1,
};

static const struct ravb_hw_info ravb_rzv2m_hw_info = {
	.rx_ring_free = ravb_rx_ring_free_rcar,
	.rx_ring_format = ravb_rx_ring_format_rcar,
	.alloc_rx_desc = ravb_alloc_rx_desc_rcar,
	.receive = ravb_rx_rcar,
	.set_rate = ravb_set_rate_rcar,
	.set_feature = ravb_set_features_rcar,
	.dmac_init = ravb_dmac_init_rcar,
	.emac_init = ravb_emac_init_rcar,
	.gstrings_stats = ravb_gstrings_stats,
	.gstrings_size = sizeof(ravb_gstrings_stats),
	.net_hw_features = NETIF_F_RXCSUM,
	.net_features = NETIF_F_RXCSUM,
	.stats_len = ARRAY_SIZE(ravb_gstrings_stats),
	.max_rx_len = RX_BUF_SZ + RAVB_ALIGN - 1,
	.tccr_mask = TCCR_TSRQ0 | TCCR_TSRQ1 | TCCR_TSRQ2 | TCCR_TSRQ3,
	.rx_max_buf_size = SZ_2K,
	.multi_irqs = 1,
	.err_mgmt_irqs = 1,
	.gptp = 1,
	.gptp_ref_clk = 1,
	.nc_queues = 1,
	.magic_pkt = 1,
};

static const struct ravb_hw_info gbeth_hw_info = {
	.rx_ring_free = ravb_rx_ring_free_gbeth,
	.rx_ring_format = ravb_rx_ring_format_gbeth,
	.alloc_rx_desc = ravb_alloc_rx_desc_gbeth,
	.receive = ravb_rx_gbeth,
	.set_rate = ravb_set_rate_gbeth,
	.set_feature = ravb_set_features_gbeth,
	.dmac_init = ravb_dmac_init_gbeth,
	.emac_init = ravb_emac_init_gbeth,
	.gstrings_stats = ravb_gstrings_stats_gbeth,
	.gstrings_size = sizeof(ravb_gstrings_stats_gbeth),
	.stats_len = ARRAY_SIZE(ravb_gstrings_stats_gbeth),
	.max_rx_len = ALIGN(GBETH_RX_BUFF_MAX, RAVB_ALIGN),
	.tccr_mask = TCCR_TSRQ0,
	.rx_max_buf_size = SZ_8K,
	.aligned_tx = 1,
	.tx_counters = 1,
	.carrier_counters = 1,
	.half_duplex = 1,
};

static const struct of_device_id ravb_match_table[] = {
	{ .compatible = "renesas,etheravb-r8a7790", .data = &ravb_gen2_hw_info },
	{ .compatible = "renesas,etheravb-r8a7794", .data = &ravb_gen2_hw_info },
	{ .compatible = "renesas,etheravb-rcar-gen2", .data = &ravb_gen2_hw_info },
	{ .compatible = "renesas,etheravb-r8a7795", .data = &ravb_gen3_hw_info },
	{ .compatible = "renesas,etheravb-rcar-gen3", .data = &ravb_gen3_hw_info },
	{ .compatible = "renesas,etheravb-rcar-gen4", .data = &ravb_gen3_hw_info },
	{ .compatible = "renesas,etheravb-rzv2m", .data = &ravb_rzv2m_hw_info },
	{ .compatible = "renesas,rzg2l-gbeth", .data = &gbeth_hw_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ravb_match_table);

static int ravb_set_gti(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	struct device *dev = ndev->dev.parent;
	unsigned long rate;
	uint64_t inc;

	if (info->gptp_ref_clk)
		rate = clk_get_rate(priv->gptp_clk);
	else
		rate = clk_get_rate(priv->clk);
	if (!rate)
		return -EINVAL;

	inc = div64_ul(1000000000ULL << 20, rate);

	if (inc < GTI_TIV_MIN || inc > GTI_TIV_MAX) {
		dev_err(dev, "gti.tiv increment 0x%llx is outside the range 0x%x - 0x%x\n",
			inc, GTI_TIV_MIN, GTI_TIV_MAX);
		return -EINVAL;
	}

	ravb_write(ndev, inc, GTI);

	return 0;
}

static void ravb_set_config_mode(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;

	if (info->gptp) {
		ravb_modify(ndev, CCC, CCC_OPC, CCC_OPC_CONFIG);
		/* Set CSEL value */
		ravb_modify(ndev, CCC, CCC_CSEL, CCC_CSEL_HPB);
	} else if (info->ccc_gac) {
		ravb_modify(ndev, CCC, CCC_OPC, CCC_OPC_CONFIG |
			    CCC_GAC | CCC_CSEL_HPB);
	} else {
		ravb_modify(ndev, CCC, CCC_OPC, CCC_OPC_CONFIG);
	}
}

/* Set tx and rx clock internal delay modes */
static void ravb_parse_delay_mode(struct device_node *np, struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	bool explicit_delay = false;
	u32 delay;

	if (!of_property_read_u32(np, "rx-internal-delay-ps", &delay)) {
		/* Valid values are 0 and 1800, according to DT bindings */
		priv->rxcidm = !!delay;
		explicit_delay = true;
	}
	if (!of_property_read_u32(np, "tx-internal-delay-ps", &delay)) {
		/* Valid values are 0 and 2000, according to DT bindings */
		priv->txcidm = !!delay;
		explicit_delay = true;
	}

	if (explicit_delay)
		return;

	/* Fall back to legacy rgmii-*id behavior */
	if (priv->phy_interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    priv->phy_interface == PHY_INTERFACE_MODE_RGMII_RXID) {
		priv->rxcidm = 1;
		priv->rgmii_override = 1;
	}

	if (priv->phy_interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    priv->phy_interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		priv->txcidm = 1;
		priv->rgmii_override = 1;
	}
}

static void ravb_set_delay_mode(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	u32 set = 0;

	if (priv->rxcidm)
		set |= APSR_RDM;
	if (priv->txcidm)
		set |= APSR_TDM;
	ravb_modify(ndev, APSR, APSR_RDM | APSR_TDM, set);
}

static int ravb_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct ravb_hw_info *info;
	struct reset_control *rstc;
	struct ravb_private *priv;
	struct net_device *ndev;
	int error, irq, q;
	struct resource *res;
	int i;

	if (!np) {
		dev_err(&pdev->dev,
			"this driver is required to be instantiated from device tree\n");
		return -EINVAL;
	}

	rstc = devm_reset_control_get_optional_exclusive(&pdev->dev, NULL);
	if (IS_ERR(rstc))
		return dev_err_probe(&pdev->dev, PTR_ERR(rstc),
				     "failed to get cpg reset\n");

	ndev = alloc_etherdev_mqs(sizeof(struct ravb_private),
				  NUM_TX_QUEUE, NUM_RX_QUEUE);
	if (!ndev)
		return -ENOMEM;

	info = of_device_get_match_data(&pdev->dev);

	ndev->features = info->net_features;
	ndev->hw_features = info->net_hw_features;

	error = reset_control_deassert(rstc);
	if (error)
		goto out_free_netdev;

	pm_runtime_enable(&pdev->dev);
	error = pm_runtime_resume_and_get(&pdev->dev);
	if (error < 0)
		goto out_rpm_disable;

	if (info->multi_irqs) {
		if (info->err_mgmt_irqs)
			irq = platform_get_irq_byname(pdev, "dia");
		else
			irq = platform_get_irq_byname(pdev, "ch22");
	} else {
		irq = platform_get_irq(pdev, 0);
	}
	if (irq < 0) {
		error = irq;
		goto out_release;
	}
	ndev->irq = irq;

	SET_NETDEV_DEV(ndev, &pdev->dev);

	priv = netdev_priv(ndev);
	priv->info = info;
	priv->rstc = rstc;
	priv->ndev = ndev;
	priv->pdev = pdev;
	priv->num_tx_ring[RAVB_BE] = BE_TX_RING_SIZE;
	priv->num_rx_ring[RAVB_BE] = BE_RX_RING_SIZE;
	if (info->nc_queues) {
		priv->num_tx_ring[RAVB_NC] = NC_TX_RING_SIZE;
		priv->num_rx_ring[RAVB_NC] = NC_RX_RING_SIZE;
	}

	priv->addr = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv->addr)) {
		error = PTR_ERR(priv->addr);
		goto out_release;
	}

	/* The Ether-specific entries in the device structure. */
	ndev->base_addr = res->start;

	spin_lock_init(&priv->lock);
	INIT_WORK(&priv->work, ravb_tx_timeout_work);

	error = of_get_phy_mode(np, &priv->phy_interface);
	if (error && error != -ENODEV)
		goto out_release;

	priv->no_avb_link = of_property_read_bool(np, "renesas,no-ether-link");
	priv->avb_link_active_low =
		of_property_read_bool(np, "renesas,ether-link-active-low");

	if (info->multi_irqs) {
		if (info->err_mgmt_irqs)
			irq = platform_get_irq_byname(pdev, "line3");
		else
			irq = platform_get_irq_byname(pdev, "ch24");
		if (irq < 0) {
			error = irq;
			goto out_release;
		}
		priv->emac_irq = irq;
		for (i = 0; i < NUM_RX_QUEUE; i++) {
			irq = platform_get_irq_byname(pdev, ravb_rx_irqs[i]);
			if (irq < 0) {
				error = irq;
				goto out_release;
			}
			priv->rx_irqs[i] = irq;
		}
		for (i = 0; i < NUM_TX_QUEUE; i++) {
			irq = platform_get_irq_byname(pdev, ravb_tx_irqs[i]);
			if (irq < 0) {
				error = irq;
				goto out_release;
			}
			priv->tx_irqs[i] = irq;
		}

		if (info->err_mgmt_irqs) {
			irq = platform_get_irq_byname(pdev, "err_a");
			if (irq < 0) {
				error = irq;
				goto out_release;
			}
			priv->erra_irq = irq;

			irq = platform_get_irq_byname(pdev, "mgmt_a");
			if (irq < 0) {
				error = irq;
				goto out_release;
			}
			priv->mgmta_irq = irq;
		}
	}

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		error = PTR_ERR(priv->clk);
		goto out_release;
	}

	priv->refclk = devm_clk_get_optional(&pdev->dev, "refclk");
	if (IS_ERR(priv->refclk)) {
		error = PTR_ERR(priv->refclk);
		goto out_release;
	}
	clk_prepare_enable(priv->refclk);

	if (info->gptp_ref_clk) {
		priv->gptp_clk = devm_clk_get(&pdev->dev, "gptp");
		if (IS_ERR(priv->gptp_clk)) {
			error = PTR_ERR(priv->gptp_clk);
			goto out_disable_refclk;
		}
		clk_prepare_enable(priv->gptp_clk);
	}

	ndev->max_mtu = info->rx_max_buf_size - (ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN);
	ndev->min_mtu = ETH_MIN_MTU;

	/* FIXME: R-Car Gen2 has 4byte alignment restriction for tx buffer
	 * Use two descriptor to handle such situation. First descriptor to
	 * handle aligned data buffer and second descriptor to handle the
	 * overflow data because of alignment.
	 */
	priv->num_tx_desc = info->aligned_tx ? 2 : 1;

	/* Set function */
	ndev->netdev_ops = &ravb_netdev_ops;
	ndev->ethtool_ops = &ravb_ethtool_ops;

	/* Set AVB config mode */
	ravb_set_config_mode(ndev);

	if (info->gptp || info->ccc_gac) {
		/* Set GTI value */
		error = ravb_set_gti(ndev);
		if (error)
			goto out_disable_gptp_clk;

		/* Request GTI loading */
		ravb_modify(ndev, GCCR, GCCR_LTI, GCCR_LTI);
	}

	if (info->internal_delay) {
		ravb_parse_delay_mode(np, ndev);
		ravb_set_delay_mode(ndev);
	}

	/* Allocate descriptor base address table */
	priv->desc_bat_size = sizeof(struct ravb_desc) * DBAT_ENTRY_NUM;
	priv->desc_bat = dma_alloc_coherent(ndev->dev.parent, priv->desc_bat_size,
					    &priv->desc_bat_dma, GFP_KERNEL);
	if (!priv->desc_bat) {
		dev_err(&pdev->dev,
			"Cannot allocate desc base address table (size %d bytes)\n",
			priv->desc_bat_size);
		error = -ENOMEM;
		goto out_disable_gptp_clk;
	}
	for (q = RAVB_BE; q < DBAT_ENTRY_NUM; q++)
		priv->desc_bat[q].die_dt = DT_EOS;
	ravb_write(ndev, priv->desc_bat_dma, DBAT);

	/* Initialise HW timestamp list */
	INIT_LIST_HEAD(&priv->ts_skb_list);

	/* Initialise PTP Clock driver */
	if (info->ccc_gac)
		ravb_ptp_init(ndev, pdev);

	/* Debug message level */
	priv->msg_enable = RAVB_DEF_MSG_ENABLE;

	/* Read and set MAC address */
	ravb_read_mac_address(np, ndev);
	if (!is_valid_ether_addr(ndev->dev_addr)) {
		dev_warn(&pdev->dev,
			 "no valid MAC address supplied, using a random one\n");
		eth_hw_addr_random(ndev);
	}

	/* MDIO bus init */
	error = ravb_mdio_init(priv);
	if (error) {
		dev_err(&pdev->dev, "failed to initialize MDIO\n");
		goto out_dma_free;
	}

	netif_napi_add(ndev, &priv->napi[RAVB_BE], ravb_poll);
	if (info->nc_queues)
		netif_napi_add(ndev, &priv->napi[RAVB_NC], ravb_poll);

	/* Network device register */
	error = register_netdev(ndev);
	if (error)
		goto out_napi_del;

	device_set_wakeup_capable(&pdev->dev, 1);

	/* Print device information */
	netdev_info(ndev, "Base address at %#x, %pM, IRQ %d.\n",
		    (u32)ndev->base_addr, ndev->dev_addr, ndev->irq);

	platform_set_drvdata(pdev, ndev);

	return 0;

out_napi_del:
	if (info->nc_queues)
		netif_napi_del(&priv->napi[RAVB_NC]);

	netif_napi_del(&priv->napi[RAVB_BE]);
	ravb_mdio_release(priv);
out_dma_free:
	dma_free_coherent(ndev->dev.parent, priv->desc_bat_size, priv->desc_bat,
			  priv->desc_bat_dma);

	/* Stop PTP Clock driver */
	if (info->ccc_gac)
		ravb_ptp_stop(ndev);
out_disable_gptp_clk:
	clk_disable_unprepare(priv->gptp_clk);
out_disable_refclk:
	clk_disable_unprepare(priv->refclk);
out_release:
	pm_runtime_put(&pdev->dev);
out_rpm_disable:
	pm_runtime_disable(&pdev->dev);
	reset_control_assert(rstc);
out_free_netdev:
	free_netdev(ndev);
	return error;
}

static void ravb_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;

	unregister_netdev(ndev);
	if (info->nc_queues)
		netif_napi_del(&priv->napi[RAVB_NC]);
	netif_napi_del(&priv->napi[RAVB_BE]);

	ravb_mdio_release(priv);

	/* Stop PTP Clock driver */
	if (info->ccc_gac)
		ravb_ptp_stop(ndev);

	dma_free_coherent(ndev->dev.parent, priv->desc_bat_size, priv->desc_bat,
			  priv->desc_bat_dma);

	/* Set reset mode */
	ravb_write(ndev, CCC_OPC_RESET, CCC);

	clk_disable_unprepare(priv->gptp_clk);
	clk_disable_unprepare(priv->refclk);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	reset_control_assert(priv->rstc);
	free_netdev(ndev);
	platform_set_drvdata(pdev, NULL);
}

static int ravb_wol_setup(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;

	/* Disable interrupts by clearing the interrupt masks. */
	ravb_write(ndev, 0, RIC0);
	ravb_write(ndev, 0, RIC2);
	ravb_write(ndev, 0, TIC);

	/* Only allow ECI interrupts */
	synchronize_irq(priv->emac_irq);
	if (info->nc_queues)
		napi_disable(&priv->napi[RAVB_NC]);
	napi_disable(&priv->napi[RAVB_BE]);
	ravb_write(ndev, ECSIPR_MPDIP, ECSIPR);

	/* Enable MagicPacket */
	ravb_modify(ndev, ECMR, ECMR_MPDE, ECMR_MPDE);

	return enable_irq_wake(priv->emac_irq);
}

static int ravb_wol_restore(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;

	if (info->nc_queues)
		napi_enable(&priv->napi[RAVB_NC]);
	napi_enable(&priv->napi[RAVB_BE]);

	/* Disable MagicPacket */
	ravb_modify(ndev, ECMR, ECMR_MPDE, 0);

	ravb_close(ndev);

	return disable_irq_wake(priv->emac_irq);
}

static int __maybe_unused ravb_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct ravb_private *priv = netdev_priv(ndev);
	int ret;

	if (!netif_running(ndev))
		return 0;

	netif_device_detach(ndev);

	if (priv->wol_enabled)
		ret = ravb_wol_setup(ndev);
	else
		ret = ravb_close(ndev);

	if (priv->info->ccc_gac)
		ravb_ptp_stop(ndev);

	return ret;
}

static int __maybe_unused ravb_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct ravb_private *priv = netdev_priv(ndev);
	const struct ravb_hw_info *info = priv->info;
	int ret = 0;

	/* If WoL is enabled set reset mode to rearm the WoL logic */
	if (priv->wol_enabled)
		ravb_write(ndev, CCC_OPC_RESET, CCC);

	/* All register have been reset to default values.
	 * Restore all registers which where setup at probe time and
	 * reopen device if it was running before system suspended.
	 */

	/* Set AVB config mode */
	ravb_set_config_mode(ndev);

	if (info->gptp || info->ccc_gac) {
		/* Set GTI value */
		ret = ravb_set_gti(ndev);
		if (ret)
			return ret;

		/* Request GTI loading */
		ravb_modify(ndev, GCCR, GCCR_LTI, GCCR_LTI);
	}

	if (info->internal_delay)
		ravb_set_delay_mode(ndev);

	/* Restore descriptor base address table */
	ravb_write(ndev, priv->desc_bat_dma, DBAT);

	if (priv->info->ccc_gac)
		ravb_ptp_init(ndev, priv->pdev);

	if (netif_running(ndev)) {
		if (priv->wol_enabled) {
			ret = ravb_wol_restore(ndev);
			if (ret)
				return ret;
		}
		ret = ravb_open(ndev);
		if (ret < 0)
			return ret;
		ravb_set_rx_mode(ndev);
		netif_device_attach(ndev);
	}

	return ret;
}

static int __maybe_unused ravb_runtime_nop(struct device *dev)
{
	/* Runtime PM callback shared between ->runtime_suspend()
	 * and ->runtime_resume(). Simply returns success.
	 *
	 * This driver re-initializes all registers after
	 * pm_runtime_get_sync() anyway so there is no need
	 * to save and restore registers here.
	 */
	return 0;
}

static const struct dev_pm_ops ravb_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ravb_suspend, ravb_resume)
	SET_RUNTIME_PM_OPS(ravb_runtime_nop, ravb_runtime_nop, NULL)
};

static struct platform_driver ravb_driver = {
	.probe		= ravb_probe,
	.remove_new	= ravb_remove,
	.driver = {
		.name	= "ravb",
		.pm	= &ravb_dev_pm_ops,
		.of_match_table = ravb_match_table,
	},
};

module_platform_driver(ravb_driver);

MODULE_AUTHOR("Mitsuhiro Kimura, Masaru Nagai");
MODULE_DESCRIPTION("Renesas Ethernet AVB driver");
MODULE_LICENSE("GPL v2");
