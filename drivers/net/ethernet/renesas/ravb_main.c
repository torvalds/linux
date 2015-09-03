/* Renesas Ethernet AVB device driver
 *
 * Copyright (C) 2014-2015 Renesas Electronics Corporation
 * Copyright (C) 2015 Renesas Solutions Corp.
 * Copyright (C) 2015 Cogent Embedded, Inc. <source@cogentembedded.com>
 *
 * Based on the SuperH Ethernet driver
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
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
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "ravb.h"

#define RAVB_DEF_MSG_ENABLE \
		(NETIF_MSG_LINK	  | \
		 NETIF_MSG_TIMER  | \
		 NETIF_MSG_RX_ERR | \
		 NETIF_MSG_TX_ERR)

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
	ravb_write(ndev, (ravb_read(ndev, CCC) & ~CCC_OPC) | CCC_OPC_CONFIG,
		   CCC);
	/* Check if the operating mode is changed to the config mode */
	error = ravb_wait(ndev, CSR, CSR_OPS, CSR_OPS_CONFIG);
	if (error)
		netdev_err(ndev, "failed to switch device to config mode\n");

	return error;
}

static void ravb_set_duplex(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	u32 ecmr = ravb_read(ndev, ECMR);

	if (priv->duplex)	/* Full */
		ecmr |=  ECMR_DM;
	else			/* Half */
		ecmr &= ~ECMR_DM;
	ravb_write(ndev, ecmr, ECMR);
}

static void ravb_set_rate(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);

	switch (priv->speed) {
	case 100:		/* 100BASE */
		ravb_write(ndev, GECMR_SPEED_100, GECMR);
		break;
	case 1000:		/* 1000BASE */
		ravb_write(ndev, GECMR_SPEED_1000, GECMR);
		break;
	default:
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
static void ravb_read_mac_address(struct net_device *ndev, const u8 *mac)
{
	if (mac) {
		ether_addr_copy(ndev->dev_addr, mac);
	} else {
		ndev->dev_addr[0] = (ravb_read(ndev, MAHR) >> 24);
		ndev->dev_addr[1] = (ravb_read(ndev, MAHR) >> 16) & 0xFF;
		ndev->dev_addr[2] = (ravb_read(ndev, MAHR) >> 8) & 0xFF;
		ndev->dev_addr[3] = (ravb_read(ndev, MAHR) >> 0) & 0xFF;
		ndev->dev_addr[4] = (ravb_read(ndev, MALR) >> 8) & 0xFF;
		ndev->dev_addr[5] = (ravb_read(ndev, MALR) >> 0) & 0xFF;
	}
}

static void ravb_mdio_ctrl(struct mdiobb_ctrl *ctrl, u32 mask, int set)
{
	struct ravb_private *priv = container_of(ctrl, struct ravb_private,
						 mdiobb);
	u32 pir = ravb_read(priv->ndev, PIR);

	if (set)
		pir |=  mask;
	else
		pir &= ~mask;
	ravb_write(priv->ndev, pir, PIR);
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
static struct mdiobb_ops bb_ops = {
	.owner = THIS_MODULE,
	.set_mdc = ravb_set_mdc,
	.set_mdio_dir = ravb_set_mdio_dir,
	.set_mdio_data = ravb_set_mdio_data,
	.get_mdio_data = ravb_get_mdio_data,
};

/* Free skb's and DMA buffers for Ethernet AVB */
static void ravb_ring_free(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	int ring_size;
	int i;

	/* Free RX skb ringbuffer */
	if (priv->rx_skb[q]) {
		for (i = 0; i < priv->num_rx_ring[q]; i++)
			dev_kfree_skb(priv->rx_skb[q][i]);
	}
	kfree(priv->rx_skb[q]);
	priv->rx_skb[q] = NULL;

	/* Free TX skb ringbuffer */
	if (priv->tx_skb[q]) {
		for (i = 0; i < priv->num_tx_ring[q]; i++)
			dev_kfree_skb(priv->tx_skb[q][i]);
	}
	kfree(priv->tx_skb[q]);
	priv->tx_skb[q] = NULL;

	/* Free aligned TX buffers */
	kfree(priv->tx_align[q]);
	priv->tx_align[q] = NULL;

	if (priv->rx_ring[q]) {
		ring_size = sizeof(struct ravb_ex_rx_desc) *
			    (priv->num_rx_ring[q] + 1);
		dma_free_coherent(NULL, ring_size, priv->rx_ring[q],
				  priv->rx_desc_dma[q]);
		priv->rx_ring[q] = NULL;
	}

	if (priv->tx_ring[q]) {
		ring_size = sizeof(struct ravb_tx_desc) *
			    (priv->num_tx_ring[q] * NUM_TX_DESC + 1);
		dma_free_coherent(NULL, ring_size, priv->tx_ring[q],
				  priv->tx_desc_dma[q]);
		priv->tx_ring[q] = NULL;
	}
}

/* Format skb and descriptor buffer for Ethernet AVB */
static void ravb_ring_format(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	struct ravb_ex_rx_desc *rx_desc;
	struct ravb_tx_desc *tx_desc;
	struct ravb_desc *desc;
	int rx_ring_size = sizeof(*rx_desc) * priv->num_rx_ring[q];
	int tx_ring_size = sizeof(*tx_desc) * priv->num_tx_ring[q] *
			   NUM_TX_DESC;
	dma_addr_t dma_addr;
	int i;

	priv->cur_rx[q] = 0;
	priv->cur_tx[q] = 0;
	priv->dirty_rx[q] = 0;
	priv->dirty_tx[q] = 0;

	memset(priv->rx_ring[q], 0, rx_ring_size);
	/* Build RX ring buffer */
	for (i = 0; i < priv->num_rx_ring[q]; i++) {
		/* RX descriptor */
		rx_desc = &priv->rx_ring[q][i];
		/* The size of the buffer should be on 16-byte boundary. */
		rx_desc->ds_cc = cpu_to_le16(ALIGN(PKT_BUF_SZ, 16));
		dma_addr = dma_map_single(&ndev->dev, priv->rx_skb[q][i]->data,
					  ALIGN(PKT_BUF_SZ, 16),
					  DMA_FROM_DEVICE);
		/* We just set the data size to 0 for a failed mapping which
		 * should prevent DMA from happening...
		 */
		if (dma_mapping_error(&ndev->dev, dma_addr))
			rx_desc->ds_cc = cpu_to_le16(0);
		rx_desc->dptr = cpu_to_le32(dma_addr);
		rx_desc->die_dt = DT_FEMPTY;
	}
	rx_desc = &priv->rx_ring[q][i];
	rx_desc->dptr = cpu_to_le32((u32)priv->rx_desc_dma[q]);
	rx_desc->die_dt = DT_LINKFIX; /* type */

	memset(priv->tx_ring[q], 0, tx_ring_size);
	/* Build TX ring buffer */
	for (i = 0, tx_desc = priv->tx_ring[q]; i < priv->num_tx_ring[q];
	     i++, tx_desc++) {
		tx_desc->die_dt = DT_EEMPTY;
		tx_desc++;
		tx_desc->die_dt = DT_EEMPTY;
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

/* Init skb and descriptor buffer for Ethernet AVB */
static int ravb_ring_init(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	struct sk_buff *skb;
	int ring_size;
	int i;

	/* Allocate RX and TX skb rings */
	priv->rx_skb[q] = kcalloc(priv->num_rx_ring[q],
				  sizeof(*priv->rx_skb[q]), GFP_KERNEL);
	priv->tx_skb[q] = kcalloc(priv->num_tx_ring[q],
				  sizeof(*priv->tx_skb[q]), GFP_KERNEL);
	if (!priv->rx_skb[q] || !priv->tx_skb[q])
		goto error;

	for (i = 0; i < priv->num_rx_ring[q]; i++) {
		skb = netdev_alloc_skb(ndev, PKT_BUF_SZ + RAVB_ALIGN - 1);
		if (!skb)
			goto error;
		ravb_set_buffer_align(skb);
		priv->rx_skb[q][i] = skb;
	}

	/* Allocate rings for the aligned buffers */
	priv->tx_align[q] = kmalloc(DPTR_ALIGN * priv->num_tx_ring[q] +
				    DPTR_ALIGN - 1, GFP_KERNEL);
	if (!priv->tx_align[q])
		goto error;

	/* Allocate all RX descriptors. */
	ring_size = sizeof(struct ravb_ex_rx_desc) * (priv->num_rx_ring[q] + 1);
	priv->rx_ring[q] = dma_alloc_coherent(NULL, ring_size,
					      &priv->rx_desc_dma[q],
					      GFP_KERNEL);
	if (!priv->rx_ring[q])
		goto error;

	priv->dirty_rx[q] = 0;

	/* Allocate all TX descriptors. */
	ring_size = sizeof(struct ravb_tx_desc) *
		    (priv->num_tx_ring[q] * NUM_TX_DESC + 1);
	priv->tx_ring[q] = dma_alloc_coherent(NULL, ring_size,
					      &priv->tx_desc_dma[q],
					      GFP_KERNEL);
	if (!priv->tx_ring[q])
		goto error;

	return 0;

error:
	ravb_ring_free(ndev, q);

	return -ENOMEM;
}

/* E-MAC init function */
static void ravb_emac_init(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	u32 ecmr;

	/* Receive frame limit set register */
	ravb_write(ndev, ndev->mtu + ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN, RFLR);

	/* PAUSE prohibition */
	ecmr =  ravb_read(ndev, ECMR);
	ecmr &= ECMR_DM;
	ecmr |= ECMR_ZPF | (priv->duplex ? ECMR_DM : 0) | ECMR_TE | ECMR_RE;
	ravb_write(ndev, ecmr, ECMR);

	ravb_set_rate(ndev);

	/* Set MAC address */
	ravb_write(ndev,
		   (ndev->dev_addr[0] << 24) | (ndev->dev_addr[1] << 16) |
		   (ndev->dev_addr[2] << 8)  | (ndev->dev_addr[3]), MAHR);
	ravb_write(ndev,
		   (ndev->dev_addr[4] << 8)  | (ndev->dev_addr[5]), MALR);

	ravb_write(ndev, 1, MPR);

	/* E-MAC status register clear */
	ravb_write(ndev, ECSR_ICD | ECSR_MPD, ECSR);

	/* E-MAC interrupt enable register */
	ravb_write(ndev, ECSIPR_ICDIP | ECSIPR_MPDIP | ECSIPR_LCHNGIP, ECSIPR);
}

/* Device init function for Ethernet AVB */
static int ravb_dmac_init(struct net_device *ndev)
{
	int error;

	/* Set CONFIG mode */
	error = ravb_config(ndev);
	if (error)
		return error;

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

#if defined(__LITTLE_ENDIAN)
	ravb_write(ndev, ravb_read(ndev, CCC) & ~CCC_BOC, CCC);
#else
	ravb_write(ndev, ravb_read(ndev, CCC) | CCC_BOC, CCC);
#endif

	/* Set AVB RX */
	ravb_write(ndev, RCR_EFFS | RCR_ENCF | RCR_ETS0 | 0x18000000, RCR);

	/* Set FIFO size */
	ravb_write(ndev, TGC_TQP_AVBMODE1 | 0x00222200, TGC);

	/* Timestamp enable */
	ravb_write(ndev, TCCR_TFEN, TCCR);

	/* Interrupt enable: */
	/* Frame receive */
	ravb_write(ndev, RIC0_FRE0 | RIC0_FRE1, RIC0);
	/* Receive FIFO full warning */
	ravb_write(ndev, RIC1_RFWE, RIC1);
	/* Receive FIFO full error, descriptor empty */
	ravb_write(ndev, RIC2_QFE0 | RIC2_QFE1 | RIC2_RFFE, RIC2);
	/* Frame transmitted, timestamp FIFO updated */
	ravb_write(ndev, TIC_FTE0 | TIC_FTE1 | TIC_TFUE, TIC);

	/* Setting the control will start the AVB-DMAC process. */
	ravb_write(ndev, (ravb_read(ndev, CCC) & ~CCC_OPC) | CCC_OPC_OPERATION,
		   CCC);

	return 0;
}

/* Free TX skb function for AVB-IP */
static int ravb_tx_free(struct net_device *ndev, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &priv->stats[q];
	struct ravb_tx_desc *desc;
	int free_num = 0;
	int entry;
	u32 size;

	for (; priv->cur_tx[q] - priv->dirty_tx[q] > 0; priv->dirty_tx[q]++) {
		entry = priv->dirty_tx[q] % (priv->num_tx_ring[q] *
					     NUM_TX_DESC);
		desc = &priv->tx_ring[q][entry];
		if (desc->die_dt != DT_FEMPTY)
			break;
		/* Descriptor type must be checked before all other reads */
		dma_rmb();
		size = le16_to_cpu(desc->ds_tagl) & TX_DS;
		/* Free the original skb. */
		if (priv->tx_skb[q][entry / NUM_TX_DESC]) {
			dma_unmap_single(&ndev->dev, le32_to_cpu(desc->dptr),
					 size, DMA_TO_DEVICE);
			/* Last packet descriptor? */
			if (entry % NUM_TX_DESC == NUM_TX_DESC - 1) {
				entry /= NUM_TX_DESC;
				dev_kfree_skb_any(priv->tx_skb[q][entry]);
				priv->tx_skb[q][entry] = NULL;
				stats->tx_packets++;
			}
			free_num++;
		}
		stats->tx_bytes += size;
		desc->die_dt = DT_EEMPTY;
	}
	return free_num;
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
				break;
			}
		}
		ravb_write(ndev, ravb_read(ndev, TCCR) | TCCR_TFR, TCCR);
	}
}

/* Packet receive function for Ethernet AVB */
static bool ravb_rx(struct net_device *ndev, int *quota, int q)
{
	struct ravb_private *priv = netdev_priv(ndev);
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
			dma_unmap_single(&ndev->dev, le32_to_cpu(desc->dptr),
					 ALIGN(PKT_BUF_SZ, 16),
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
		/* The size of the buffer should be on 16-byte boundary. */
		desc->ds_cc = cpu_to_le16(ALIGN(PKT_BUF_SZ, 16));

		if (!priv->rx_skb[q][entry]) {
			skb = netdev_alloc_skb(ndev,
					       PKT_BUF_SZ + RAVB_ALIGN - 1);
			if (!skb)
				break;	/* Better luck next round. */
			ravb_set_buffer_align(skb);
			dma_addr = dma_map_single(&ndev->dev, skb->data,
						  le16_to_cpu(desc->ds_cc),
						  DMA_FROM_DEVICE);
			skb_checksum_none_assert(skb);
			/* We just set the data size to 0 for a failed mapping
			 * which should prevent DMA  from happening...
			 */
			if (dma_mapping_error(&ndev->dev, dma_addr))
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

static void ravb_rcv_snd_disable(struct net_device *ndev)
{
	/* Disable TX and RX */
	ravb_write(ndev, ravb_read(ndev, ECMR) & ~(ECMR_RE | ECMR_TE), ECMR);
}

static void ravb_rcv_snd_enable(struct net_device *ndev)
{
	/* Enable TX and RX */
	ravb_write(ndev, ravb_read(ndev, ECMR) | ECMR_RE | ECMR_TE, ECMR);
}

/* function for waiting dma process finished */
static int ravb_stop_dma(struct net_device *ndev)
{
	int error;

	/* Wait for stopping the hardware TX process */
	error = ravb_wait(ndev, TCCR,
			  TCCR_TSRQ0 | TCCR_TSRQ1 | TCCR_TSRQ2 | TCCR_TSRQ3, 0);
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
static void ravb_emac_interrupt(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	u32 ecsr, psr;

	ecsr = ravb_read(ndev, ECSR);
	ravb_write(ndev, ecsr, ECSR);	/* clear interrupt */
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

/* Error interrupt handler */
static void ravb_error_interrupt(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	u32 eis, ris2;

	eis = ravb_read(ndev, EIS);
	ravb_write(ndev, ~EIS_QFS, EIS);
	if (eis & EIS_QFS) {
		ris2 = ravb_read(ndev, RIS2);
		ravb_write(ndev, ~(RIS2_QFF0 | RIS2_RFFF), RIS2);

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

static irqreturn_t ravb_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct ravb_private *priv = netdev_priv(ndev);
	irqreturn_t result = IRQ_NONE;
	u32 iss;

	spin_lock(&priv->lock);
	/* Get interrupt status */
	iss = ravb_read(ndev, ISS);

	/* Received and transmitted interrupts */
	if (iss & (ISS_FRS | ISS_FTS | ISS_TFUS)) {
		u32 ris0 = ravb_read(ndev, RIS0);
		u32 ric0 = ravb_read(ndev, RIC0);
		u32 tis  = ravb_read(ndev, TIS);
		u32 tic  = ravb_read(ndev, TIC);
		int q;

		/* Timestamp updated */
		if (tis & TIS_TFUF) {
			ravb_write(ndev, ~TIS_TFUF, TIS);
			ravb_get_tx_tstamp(ndev);
			result = IRQ_HANDLED;
		}

		/* Network control and best effort queue RX/TX */
		for (q = RAVB_NC; q >= RAVB_BE; q--) {
			if (((ris0 & ric0) & BIT(q)) ||
			    ((tis  & tic)  & BIT(q))) {
				if (napi_schedule_prep(&priv->napi[q])) {
					/* Mask RX and TX interrupts */
					ravb_write(ndev, ric0 & ~BIT(q), RIC0);
					ravb_write(ndev, tic  & ~BIT(q), TIC);
					__napi_schedule(&priv->napi[q]);
				} else {
					netdev_warn(ndev,
						    "ignoring interrupt, rx status 0x%08x, rx mask 0x%08x,\n",
						    ris0, ric0);
					netdev_warn(ndev,
						    "                    tx status 0x%08x, tx mask 0x%08x.\n",
						    tis, tic);
				}
				result = IRQ_HANDLED;
			}
		}
	}

	/* E-MAC status summary */
	if (iss & ISS_MS) {
		ravb_emac_interrupt(ndev);
		result = IRQ_HANDLED;
	}

	/* Error status summary */
	if (iss & ISS_ES) {
		ravb_error_interrupt(ndev);
		result = IRQ_HANDLED;
	}

	if (iss & ISS_CGIS)
		result = ravb_ptp_interrupt(ndev);

	mmiowb();
	spin_unlock(&priv->lock);
	return result;
}

static int ravb_poll(struct napi_struct *napi, int budget)
{
	struct net_device *ndev = napi->dev;
	struct ravb_private *priv = netdev_priv(ndev);
	unsigned long flags;
	int q = napi - priv->napi;
	int mask = BIT(q);
	int quota = budget;
	u32 ris0, tis;

	for (;;) {
		tis = ravb_read(ndev, TIS);
		ris0 = ravb_read(ndev, RIS0);
		if (!((ris0 & mask) || (tis & mask)))
			break;

		/* Processing RX Descriptor Ring */
		if (ris0 & mask) {
			/* Clear RX interrupt */
			ravb_write(ndev, ~mask, RIS0);
			if (ravb_rx(ndev, &quota, q))
				goto out;
		}
		/* Processing TX Descriptor Ring */
		if (tis & mask) {
			spin_lock_irqsave(&priv->lock, flags);
			/* Clear TX interrupt */
			ravb_write(ndev, ~mask, TIS);
			ravb_tx_free(ndev, q);
			netif_wake_subqueue(ndev, q);
			mmiowb();
			spin_unlock_irqrestore(&priv->lock, flags);
		}
	}

	napi_complete(napi);

	/* Re-enable RX/TX interrupts */
	spin_lock_irqsave(&priv->lock, flags);
	ravb_write(ndev, ravb_read(ndev, RIC0) | mask, RIC0);
	ravb_write(ndev, ravb_read(ndev, TIC)  | mask,  TIC);
	mmiowb();
	spin_unlock_irqrestore(&priv->lock, flags);

	/* Receive error message handling */
	priv->rx_over_errors =  priv->stats[RAVB_BE].rx_over_errors;
	priv->rx_over_errors += priv->stats[RAVB_NC].rx_over_errors;
	if (priv->rx_over_errors != ndev->stats.rx_over_errors) {
		ndev->stats.rx_over_errors = priv->rx_over_errors;
		netif_err(priv, rx_err, ndev, "Receive Descriptor Empty\n");
	}
	if (priv->rx_fifo_errors != ndev->stats.rx_fifo_errors) {
		ndev->stats.rx_fifo_errors = priv->rx_fifo_errors;
		netif_err(priv, rx_err, ndev, "Receive FIFO Overflow\n");
	}
out:
	return budget - quota;
}

/* PHY state control function */
static void ravb_adjust_link(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	struct phy_device *phydev = priv->phydev;
	bool new_state = false;

	if (phydev->link) {
		if (phydev->duplex != priv->duplex) {
			new_state = true;
			priv->duplex = phydev->duplex;
			ravb_set_duplex(ndev);
		}

		if (phydev->speed != priv->speed) {
			new_state = true;
			priv->speed = phydev->speed;
			ravb_set_rate(ndev);
		}
		if (!priv->link) {
			ravb_write(ndev, ravb_read(ndev, ECMR) & ~ECMR_TXF,
				   ECMR);
			new_state = true;
			priv->link = phydev->link;
			if (priv->no_avb_link)
				ravb_rcv_snd_enable(ndev);
		}
	} else if (priv->link) {
		new_state = true;
		priv->link = 0;
		priv->speed = 0;
		priv->duplex = -1;
		if (priv->no_avb_link)
			ravb_rcv_snd_disable(ndev);
	}

	if (new_state && netif_msg_link(priv))
		phy_print_status(phydev);
}

/* PHY init function */
static int ravb_phy_init(struct net_device *ndev)
{
	struct device_node *np = ndev->dev.parent->of_node;
	struct ravb_private *priv = netdev_priv(ndev);
	struct phy_device *phydev;
	struct device_node *pn;

	priv->link = 0;
	priv->speed = 0;
	priv->duplex = -1;

	/* Try connecting to PHY */
	pn = of_parse_phandle(np, "phy-handle", 0);
	phydev = of_phy_connect(ndev, pn, ravb_adjust_link, 0,
				priv->phy_interface);
	if (!phydev) {
		netdev_err(ndev, "failed to connect PHY\n");
		return -ENOENT;
	}

	netdev_info(ndev, "attached PHY %d (IRQ %d) to driver %s\n",
		    phydev->addr, phydev->irq, phydev->drv->name);

	priv->phydev = phydev;

	return 0;
}

/* PHY control start function */
static int ravb_phy_start(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	int error;

	error = ravb_phy_init(ndev);
	if (error)
		return error;

	phy_start(priv->phydev);

	return 0;
}

static int ravb_get_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct ravb_private *priv = netdev_priv(ndev);
	int error = -ENODEV;
	unsigned long flags;

	if (priv->phydev) {
		spin_lock_irqsave(&priv->lock, flags);
		error = phy_ethtool_gset(priv->phydev, ecmd);
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	return error;
}

static int ravb_set_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct ravb_private *priv = netdev_priv(ndev);
	unsigned long flags;
	int error;

	if (!priv->phydev)
		return -ENODEV;

	spin_lock_irqsave(&priv->lock, flags);

	/* Disable TX and RX */
	ravb_rcv_snd_disable(ndev);

	error = phy_ethtool_sset(priv->phydev, ecmd);
	if (error)
		goto error_exit;

	if (ecmd->duplex == DUPLEX_FULL)
		priv->duplex = 1;
	else
		priv->duplex = 0;

	ravb_set_duplex(ndev);

error_exit:
	mdelay(1);

	/* Enable TX and RX */
	ravb_rcv_snd_enable(ndev);

	mmiowb();
	spin_unlock_irqrestore(&priv->lock, flags);

	return error;
}

static int ravb_nway_reset(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	int error = -ENODEV;
	unsigned long flags;

	if (priv->phydev) {
		spin_lock_irqsave(&priv->lock, flags);
		error = phy_start_aneg(priv->phydev);
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	return error;
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
	"rx_queue_1_frame_errors_",
	"rx_queue_1_length_errors",
	"rx_queue_1_missed_errors",
	"rx_queue_1_over_errors",
};

#define RAVB_STATS_LEN	ARRAY_SIZE(ravb_gstrings_stats)

static int ravb_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return RAVB_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void ravb_get_ethtool_stats(struct net_device *ndev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct ravb_private *priv = netdev_priv(ndev);
	int i = 0;
	int q;

	/* Device-specific stats */
	for (q = RAVB_BE; q < NUM_RX_QUEUE; q++) {
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
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, *ravb_gstrings_stats, sizeof(ravb_gstrings_stats));
		break;
	}
}

static void ravb_get_ringparam(struct net_device *ndev,
			       struct ethtool_ringparam *ring)
{
	struct ravb_private *priv = netdev_priv(ndev);

	ring->rx_max_pending = BE_RX_RING_MAX;
	ring->tx_max_pending = BE_TX_RING_MAX;
	ring->rx_pending = priv->num_rx_ring[RAVB_BE];
	ring->tx_pending = priv->num_tx_ring[RAVB_BE];
}

static int ravb_set_ringparam(struct net_device *ndev,
			      struct ethtool_ringparam *ring)
{
	struct ravb_private *priv = netdev_priv(ndev);
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
		ravb_ptp_init(ndev, priv->pdev);

		netif_device_attach(ndev);
	}

	return 0;
}

static int ravb_get_ts_info(struct net_device *ndev,
			    struct ethtool_ts_info *info)
{
	struct ravb_private *priv = netdev_priv(ndev);

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
	info->phc_index = ptp_clock_index(priv->ptp.clock);

	return 0;
}

static const struct ethtool_ops ravb_ethtool_ops = {
	.get_settings		= ravb_get_settings,
	.set_settings		= ravb_set_settings,
	.nway_reset		= ravb_nway_reset,
	.get_msglevel		= ravb_get_msglevel,
	.set_msglevel		= ravb_set_msglevel,
	.get_link		= ethtool_op_get_link,
	.get_strings		= ravb_get_strings,
	.get_ethtool_stats	= ravb_get_ethtool_stats,
	.get_sset_count		= ravb_get_sset_count,
	.get_ringparam		= ravb_get_ringparam,
	.set_ringparam		= ravb_set_ringparam,
	.get_ts_info		= ravb_get_ts_info,
};

/* Network device open function for Ethernet AVB */
static int ravb_open(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	int error;

	napi_enable(&priv->napi[RAVB_BE]);
	napi_enable(&priv->napi[RAVB_NC]);

	error = request_irq(ndev->irq, ravb_interrupt, IRQF_SHARED, ndev->name,
			    ndev);
	if (error) {
		netdev_err(ndev, "cannot request IRQ\n");
		goto out_napi_off;
	}

	/* Device init */
	error = ravb_dmac_init(ndev);
	if (error)
		goto out_free_irq;
	ravb_emac_init(ndev);

	/* Initialise PTP Clock driver */
	ravb_ptp_init(ndev, priv->pdev);

	netif_tx_start_all_queues(ndev);

	/* PHY control start */
	error = ravb_phy_start(ndev);
	if (error)
		goto out_ptp_stop;

	return 0;

out_ptp_stop:
	/* Stop PTP Clock driver */
	ravb_ptp_stop(ndev);
out_free_irq:
	free_irq(ndev->irq, ndev);
out_napi_off:
	napi_disable(&priv->napi[RAVB_NC]);
	napi_disable(&priv->napi[RAVB_BE]);
	return error;
}

/* Timeout function for Ethernet AVB */
static void ravb_tx_timeout(struct net_device *ndev)
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
	struct net_device *ndev = priv->ndev;

	netif_tx_stop_all_queues(ndev);

	/* Stop PTP Clock driver */
	ravb_ptp_stop(ndev);

	/* Wait for DMA stopping */
	ravb_stop_dma(ndev);

	ravb_ring_free(ndev, RAVB_BE);
	ravb_ring_free(ndev, RAVB_NC);

	/* Device init */
	ravb_dmac_init(ndev);
	ravb_emac_init(ndev);

	/* Initialise PTP Clock driver */
	ravb_ptp_init(ndev, priv->pdev);

	netif_tx_start_all_queues(ndev);
}

/* Packet transmit function for Ethernet AVB */
static netdev_tx_t ravb_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
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
	    NUM_TX_DESC) {
		netif_err(priv, tx_queued, ndev,
			  "still transmitting with the full ring!\n");
		netif_stop_subqueue(ndev, q);
		spin_unlock_irqrestore(&priv->lock, flags);
		return NETDEV_TX_BUSY;
	}
	entry = priv->cur_tx[q] % (priv->num_tx_ring[q] * NUM_TX_DESC);
	priv->tx_skb[q][entry / NUM_TX_DESC] = skb;

	if (skb_put_padto(skb, ETH_ZLEN))
		goto drop;

	buffer = PTR_ALIGN(priv->tx_align[q], DPTR_ALIGN) +
		 entry / NUM_TX_DESC * DPTR_ALIGN;
	len = PTR_ALIGN(skb->data, DPTR_ALIGN) - skb->data;
	memcpy(buffer, skb->data, len);
	dma_addr = dma_map_single(&ndev->dev, buffer, len, DMA_TO_DEVICE);
	if (dma_mapping_error(&ndev->dev, dma_addr))
		goto drop;

	desc = &priv->tx_ring[q][entry];
	desc->ds_tagl = cpu_to_le16(len);
	desc->dptr = cpu_to_le32(dma_addr);

	buffer = skb->data + len;
	len = skb->len - len;
	dma_addr = dma_map_single(&ndev->dev, buffer, len, DMA_TO_DEVICE);
	if (dma_mapping_error(&ndev->dev, dma_addr))
		goto unmap;

	desc++;
	desc->ds_tagl = cpu_to_le16(len);
	desc->dptr = cpu_to_le32(dma_addr);

	/* TX timestamp required */
	if (q == RAVB_NC) {
		ts_skb = kmalloc(sizeof(*ts_skb), GFP_ATOMIC);
		if (!ts_skb) {
			desc--;
			dma_unmap_single(&ndev->dev, dma_addr, len,
					 DMA_TO_DEVICE);
			goto unmap;
		}
		ts_skb->skb = skb;
		ts_skb->tag = priv->ts_skb_tag++;
		priv->ts_skb_tag &= 0x3ff;
		list_add_tail(&ts_skb->list, &priv->ts_skb_list);

		/* TAG and timestamp required flag */
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		skb_tx_timestamp(skb);
		desc->tagh_tsr = (ts_skb->tag >> 4) | TX_TSR;
		desc->ds_tagl |= le16_to_cpu(ts_skb->tag << 12);
	}

	/* Descriptor type must be set after all the above writes */
	dma_wmb();
	desc->die_dt = DT_FEND;
	desc--;
	desc->die_dt = DT_FSTART;

	ravb_write(ndev, ravb_read(ndev, TCCR) | (TCCR_TSRQ0 << q), TCCR);

	priv->cur_tx[q] += NUM_TX_DESC;
	if (priv->cur_tx[q] - priv->dirty_tx[q] >
	    (priv->num_tx_ring[q] - 1) * NUM_TX_DESC && !ravb_tx_free(ndev, q))
		netif_stop_subqueue(ndev, q);

exit:
	mmiowb();
	spin_unlock_irqrestore(&priv->lock, flags);
	return NETDEV_TX_OK;

unmap:
	dma_unmap_single(&ndev->dev, le32_to_cpu(desc->dptr),
			 le16_to_cpu(desc->ds_tagl), DMA_TO_DEVICE);
drop:
	dev_kfree_skb_any(skb);
	priv->tx_skb[q][entry / NUM_TX_DESC] = NULL;
	goto exit;
}

static u16 ravb_select_queue(struct net_device *ndev, struct sk_buff *skb,
			     void *accel_priv, select_queue_fallback_t fallback)
{
	/* If skb needs TX timestamp, it is handled in network control queue */
	return (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) ? RAVB_NC :
							       RAVB_BE;

}

static struct net_device_stats *ravb_get_stats(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	struct net_device_stats *nstats, *stats0, *stats1;

	nstats = &ndev->stats;
	stats0 = &priv->stats[RAVB_BE];
	stats1 = &priv->stats[RAVB_NC];

	nstats->tx_dropped += ravb_read(ndev, TROCR);
	ravb_write(ndev, 0, TROCR);	/* (write clear) */
	nstats->collisions += ravb_read(ndev, CDCR);
	ravb_write(ndev, 0, CDCR);	/* (write clear) */
	nstats->tx_carrier_errors += ravb_read(ndev, LCCR);
	ravb_write(ndev, 0, LCCR);	/* (write clear) */

	nstats->tx_carrier_errors += ravb_read(ndev, CERCR);
	ravb_write(ndev, 0, CERCR);	/* (write clear) */
	nstats->tx_carrier_errors += ravb_read(ndev, CEECR);
	ravb_write(ndev, 0, CEECR);	/* (write clear) */

	nstats->rx_packets = stats0->rx_packets + stats1->rx_packets;
	nstats->tx_packets = stats0->tx_packets + stats1->tx_packets;
	nstats->rx_bytes = stats0->rx_bytes + stats1->rx_bytes;
	nstats->tx_bytes = stats0->tx_bytes + stats1->tx_bytes;
	nstats->multicast = stats0->multicast + stats1->multicast;
	nstats->rx_errors = stats0->rx_errors + stats1->rx_errors;
	nstats->rx_crc_errors = stats0->rx_crc_errors + stats1->rx_crc_errors;
	nstats->rx_frame_errors =
		stats0->rx_frame_errors + stats1->rx_frame_errors;
	nstats->rx_length_errors =
		stats0->rx_length_errors + stats1->rx_length_errors;
	nstats->rx_missed_errors =
		stats0->rx_missed_errors + stats1->rx_missed_errors;
	nstats->rx_over_errors =
		stats0->rx_over_errors + stats1->rx_over_errors;

	return nstats;
}

/* Update promiscuous bit */
static void ravb_set_rx_mode(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	unsigned long flags;
	u32 ecmr;

	spin_lock_irqsave(&priv->lock, flags);
	ecmr = ravb_read(ndev, ECMR);
	if (ndev->flags & IFF_PROMISC)
		ecmr |=  ECMR_PRM;
	else
		ecmr &= ~ECMR_PRM;
	ravb_write(ndev, ecmr, ECMR);
	mmiowb();
	spin_unlock_irqrestore(&priv->lock, flags);
}

/* Device close function for Ethernet AVB */
static int ravb_close(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	struct ravb_tstamp_skb *ts_skb, *ts_skb2;

	netif_tx_stop_all_queues(ndev);

	/* Disable interrupts by clearing the interrupt masks. */
	ravb_write(ndev, 0, RIC0);
	ravb_write(ndev, 0, RIC1);
	ravb_write(ndev, 0, RIC2);
	ravb_write(ndev, 0, TIC);

	/* Stop PTP Clock driver */
	ravb_ptp_stop(ndev);

	/* Set the config mode to stop the AVB-DMAC's processes */
	if (ravb_stop_dma(ndev) < 0)
		netdev_err(ndev,
			   "device will be stopped after h/w processes are done.\n");

	/* Clear the timestamp list */
	list_for_each_entry_safe(ts_skb, ts_skb2, &priv->ts_skb_list, list) {
		list_del(&ts_skb->list);
		kfree(ts_skb);
	}

	/* PHY disconnect */
	if (priv->phydev) {
		phy_stop(priv->phydev);
		phy_disconnect(priv->phydev);
		priv->phydev = NULL;
	}

	free_irq(ndev->irq, ndev);

	napi_disable(&priv->napi[RAVB_NC]);
	napi_disable(&priv->napi[RAVB_BE]);

	/* Free all the skb's in the RX queue and the DMA buffers. */
	ravb_ring_free(ndev, RAVB_BE);
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
	if (priv->tstamp_rx_ctrl & RAVB_RXTSTAMP_TYPE_V2_L2_EVENT)
		config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
	else if (priv->tstamp_rx_ctrl & RAVB_RXTSTAMP_TYPE_ALL)
		config.rx_filter = HWTSTAMP_FILTER_ALL;
	else
		config.rx_filter = HWTSTAMP_FILTER_NONE;

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

	/* Reserved for future extensions */
	if (config.flags)
		return -EINVAL;

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
	struct ravb_private *priv = netdev_priv(ndev);
	struct phy_device *phydev = priv->phydev;

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

static const struct net_device_ops ravb_netdev_ops = {
	.ndo_open		= ravb_open,
	.ndo_stop		= ravb_close,
	.ndo_start_xmit		= ravb_start_xmit,
	.ndo_select_queue	= ravb_select_queue,
	.ndo_get_stats		= ravb_get_stats,
	.ndo_set_rx_mode	= ravb_set_rx_mode,
	.ndo_tx_timeout		= ravb_tx_timeout,
	.ndo_do_ioctl		= ravb_do_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

/* MDIO bus init function */
static int ravb_mdio_init(struct ravb_private *priv)
{
	struct platform_device *pdev = priv->pdev;
	struct device *dev = &pdev->dev;
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

static int ravb_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct ravb_private *priv;
	struct net_device *ndev;
	int error, irq, q;
	struct resource *res;

	if (!np) {
		dev_err(&pdev->dev,
			"this driver is required to be instantiated from device tree\n");
		return -EINVAL;
	}

	/* Get base address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "invalid resource\n");
		return -EINVAL;
	}

	ndev = alloc_etherdev_mqs(sizeof(struct ravb_private),
				  NUM_TX_QUEUE, NUM_RX_QUEUE);
	if (!ndev)
		return -ENOMEM;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	/* The Ether-specific entries in the device structure. */
	ndev->base_addr = res->start;
	ndev->dma = -1;
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		error = irq;
		goto out_release;
	}
	ndev->irq = irq;

	SET_NETDEV_DEV(ndev, &pdev->dev);

	priv = netdev_priv(ndev);
	priv->ndev = ndev;
	priv->pdev = pdev;
	priv->num_tx_ring[RAVB_BE] = BE_TX_RING_SIZE;
	priv->num_rx_ring[RAVB_BE] = BE_RX_RING_SIZE;
	priv->num_tx_ring[RAVB_NC] = NC_TX_RING_SIZE;
	priv->num_rx_ring[RAVB_NC] = NC_RX_RING_SIZE;
	priv->addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->addr)) {
		error = PTR_ERR(priv->addr);
		goto out_release;
	}

	spin_lock_init(&priv->lock);
	INIT_WORK(&priv->work, ravb_tx_timeout_work);

	priv->phy_interface = of_get_phy_mode(np);

	priv->no_avb_link = of_property_read_bool(np, "renesas,no-ether-link");
	priv->avb_link_active_low =
		of_property_read_bool(np, "renesas,ether-link-active-low");

	/* Set function */
	ndev->netdev_ops = &ravb_netdev_ops;
	ndev->ethtool_ops = &ravb_ethtool_ops;

	/* Set AVB config mode */
	ravb_write(ndev, (ravb_read(ndev, CCC) & ~CCC_OPC) | CCC_OPC_CONFIG,
		   CCC);

	/* Set CSEL value */
	ravb_write(ndev, (ravb_read(ndev, CCC) & ~CCC_CSEL) | CCC_CSEL_HPB,
		   CCC);

	/* Set GTI value */
	ravb_write(ndev, ((1000 << 20) / 130) & GTI_TIV, GTI);

	/* Request GTI loading */
	ravb_write(ndev, ravb_read(ndev, GCCR) | GCCR_LTI, GCCR);

	/* Allocate descriptor base address table */
	priv->desc_bat_size = sizeof(struct ravb_desc) * DBAT_ENTRY_NUM;
	priv->desc_bat = dma_alloc_coherent(NULL, priv->desc_bat_size,
					    &priv->desc_bat_dma, GFP_KERNEL);
	if (!priv->desc_bat) {
		dev_err(&ndev->dev,
			"Cannot allocate desc base address table (size %d bytes)\n",
			priv->desc_bat_size);
		error = -ENOMEM;
		goto out_release;
	}
	for (q = RAVB_BE; q < DBAT_ENTRY_NUM; q++)
		priv->desc_bat[q].die_dt = DT_EOS;
	ravb_write(ndev, priv->desc_bat_dma, DBAT);

	/* Initialise HW timestamp list */
	INIT_LIST_HEAD(&priv->ts_skb_list);

	/* Debug message level */
	priv->msg_enable = RAVB_DEF_MSG_ENABLE;

	/* Read and set MAC address */
	ravb_read_mac_address(ndev, of_get_mac_address(np));
	if (!is_valid_ether_addr(ndev->dev_addr)) {
		dev_warn(&pdev->dev,
			 "no valid MAC address supplied, using a random one\n");
		eth_hw_addr_random(ndev);
	}

	/* MDIO bus init */
	error = ravb_mdio_init(priv);
	if (error) {
		dev_err(&ndev->dev, "failed to initialize MDIO\n");
		goto out_dma_free;
	}

	netif_napi_add(ndev, &priv->napi[RAVB_BE], ravb_poll, 64);
	netif_napi_add(ndev, &priv->napi[RAVB_NC], ravb_poll, 64);

	/* Network device register */
	error = register_netdev(ndev);
	if (error)
		goto out_napi_del;

	/* Print device information */
	netdev_info(ndev, "Base address at %#x, %pM, IRQ %d.\n",
		    (u32)ndev->base_addr, ndev->dev_addr, ndev->irq);

	platform_set_drvdata(pdev, ndev);

	return 0;

out_napi_del:
	netif_napi_del(&priv->napi[RAVB_NC]);
	netif_napi_del(&priv->napi[RAVB_BE]);
	ravb_mdio_release(priv);
out_dma_free:
	dma_free_coherent(NULL, priv->desc_bat_size, priv->desc_bat,
			  priv->desc_bat_dma);
out_release:
	if (ndev)
		free_netdev(ndev);

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return error;
}

static int ravb_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct ravb_private *priv = netdev_priv(ndev);

	dma_free_coherent(NULL, priv->desc_bat_size, priv->desc_bat,
			  priv->desc_bat_dma);
	/* Set reset mode */
	ravb_write(ndev, CCC_OPC_RESET, CCC);
	pm_runtime_put_sync(&pdev->dev);
	unregister_netdev(ndev);
	netif_napi_del(&priv->napi[RAVB_NC]);
	netif_napi_del(&priv->napi[RAVB_BE]);
	ravb_mdio_release(priv);
	pm_runtime_disable(&pdev->dev);
	free_netdev(ndev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int ravb_runtime_nop(struct device *dev)
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
	.runtime_suspend = ravb_runtime_nop,
	.runtime_resume = ravb_runtime_nop,
};

#define RAVB_PM_OPS (&ravb_dev_pm_ops)
#else
#define RAVB_PM_OPS NULL
#endif

static const struct of_device_id ravb_match_table[] = {
	{ .compatible = "renesas,etheravb-r8a7790" },
	{ .compatible = "renesas,etheravb-r8a7794" },
	{ }
};
MODULE_DEVICE_TABLE(of, ravb_match_table);

static struct platform_driver ravb_driver = {
	.probe		= ravb_probe,
	.remove		= ravb_remove,
	.driver = {
		.name	= "ravb",
		.pm	= RAVB_PM_OPS,
		.of_match_table = ravb_match_table,
	},
};

module_platform_driver(ravb_driver);

MODULE_AUTHOR("Mitsuhiro Kimura, Masaru Nagai");
MODULE_DESCRIPTION("Renesas Ethernet AVB driver");
MODULE_LICENSE("GPL v2");
