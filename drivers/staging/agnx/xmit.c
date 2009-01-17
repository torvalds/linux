/**
 * Airgo MIMO wireless driver
 *
 * Copyright (c) 2007 Li YanBo <dreamfly281@gmail.com>

 * Thanks for Jeff Williams <angelbane@gmail.com> do reverse engineer
 * works and published the SPECS at http://airgo.wdwconsulting.net/mymoin

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include "agnx.h"
#include "debug.h"
#include "phy.h"

unsigned int rx_frame_cnt = 0;
//unsigned int local_tx_sent_cnt = 0;

static inline void disable_rx_engine(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	iowrite32(0x100, ctl + AGNX_CIR_RXCTL);
	/* Wait for RX Control to have the Disable Rx Interrupt (0x100) set */
	ioread32(ctl + AGNX_CIR_RXCTL);
}

static inline void enable_rx_engine(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	iowrite32(0x80, ctl + AGNX_CIR_RXCTL);
	ioread32(ctl + AGNX_CIR_RXCTL);
}

inline void disable_rx_interrupt(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;

	disable_rx_engine(priv);
	reg = ioread32(ctl + AGNX_CIR_RXCFG);
	reg &= ~0x20;
	iowrite32(reg, ctl + AGNX_CIR_RXCFG);
	ioread32(ctl + AGNX_CIR_RXCFG);
}

inline void enable_rx_interrupt(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;

	reg = ioread32(ctl + AGNX_CIR_RXCFG);
	reg |= 0x20;
	iowrite32(reg, ctl + AGNX_CIR_RXCFG);
	ioread32(ctl + AGNX_CIR_RXCFG);
	enable_rx_engine(priv);
}

static inline void rx_desc_init(struct agnx_priv *priv, unsigned int idx)
{
	struct agnx_desc *desc = priv->rx.desc + idx;
	struct agnx_info *info = priv->rx.info + idx;

	memset(info, 0, sizeof(*info));

	info->dma_len = IEEE80211_MAX_RTS_THRESHOLD + sizeof(struct agnx_hdr);
	info->skb = dev_alloc_skb(info->dma_len);
	if (info->skb == NULL)
		agnx_bug("refill err");

	info->mapping = pci_map_single(priv->pdev, skb_tail_pointer(info->skb),
				       info->dma_len, PCI_DMA_FROMDEVICE);
	memset(desc, 0, sizeof(*desc));
	desc->dma_addr = cpu_to_be32(info->mapping);
	/* Set the owner to the card */
	desc->frag = cpu_to_be32(be32_to_cpu(desc->frag) | OWNER);
}

static inline void rx_desc_reinit(struct agnx_priv *priv, unsigned int idx)
{
	struct agnx_info *info = priv->rx.info + idx;

	/* Cause ieee80211 will free the skb buffer, so we needn't to free it again?! */
	pci_unmap_single(priv->pdev, info->mapping, info->dma_len, PCI_DMA_FROMDEVICE);
	rx_desc_init(priv, idx);
}

static inline void rx_desc_reusing(struct agnx_priv *priv, unsigned int idx)
{
	struct agnx_desc *desc = priv->rx.desc + idx;
	struct agnx_info *info = priv->rx.info + idx;

	memset(desc, 0, sizeof(*desc));
	desc->dma_addr = cpu_to_be32(info->mapping);
	/* Set the owner to the card */
	desc->frag = cpu_to_be32(be32_to_cpu(desc->frag) | OWNER);
}

static void rx_desc_free(struct agnx_priv *priv, unsigned int idx)
{
	struct agnx_desc *desc = priv->rx.desc + idx;
	struct agnx_info *info = priv->rx.info + idx;

	BUG_ON(!desc || !info);
	if (info->mapping)
		pci_unmap_single(priv->pdev, info->mapping, info->dma_len, PCI_DMA_FROMDEVICE);
	if (info->skb)
		dev_kfree_skb(info->skb);
	memset(info, 0, sizeof(*info));
	memset(desc, 0, sizeof(*desc));
}

static inline void __tx_desc_free(struct agnx_priv *priv,
				  struct agnx_desc *desc, struct agnx_info *info)
{
	BUG_ON(!desc || !info);
	/* TODO make sure mapping, skb and len are consistency */
	if (info->mapping)
		pci_unmap_single(priv->pdev, info->mapping,
				 info->dma_len, PCI_DMA_TODEVICE);
	if (info->type == PACKET)
		dev_kfree_skb(info->skb);

	memset(info, 0, sizeof(*info));
	memset(desc, 0, sizeof(*desc));
}

static void txm_desc_free(struct agnx_priv *priv, unsigned int idx)
{
	struct agnx_desc *desc = priv->txm.desc + idx;
	struct agnx_info *info = priv->txm.info + idx;

	__tx_desc_free(priv, desc, info);
}

static void txd_desc_free(struct agnx_priv *priv, unsigned int idx)
{
	struct agnx_desc *desc = priv->txd.desc + idx;
	struct agnx_info *info = priv->txd.info + idx;

	__tx_desc_free(priv, desc, info);
}

int fill_rings(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	unsigned int i;
	u32 reg;
	AGNX_TRACE;

	priv->txd.idx_sent = priv->txm.idx_sent = 0;
	priv->rx.idx = priv->txm.idx = priv->txd.idx = 0;

	for (i = 0; i < priv->rx.size; i++)
		rx_desc_init(priv, i);
	for (i = 0; i < priv->txm.size; i++) {
		memset(priv->txm.desc + i, 0, sizeof(struct agnx_desc));
		memset(priv->txm.info + i, 0, sizeof(struct agnx_info));
	}
	for (i = 0; i < priv->txd.size; i++) {
		memset(priv->txd.desc + i, 0, sizeof(struct agnx_desc));
		memset(priv->txd.info + i, 0, sizeof(struct agnx_info));
	}

	/* FIXME Set the card RX TXM and TXD address */
	agnx_write32(ctl, AGNX_CIR_RXCMSTART, priv->rx.dma);
	agnx_write32(ctl, AGNX_CIR_RXCMEND, priv->txm.dma);

	agnx_write32(ctl, AGNX_CIR_TXMSTART, priv->txm.dma);
	agnx_write32(ctl, AGNX_CIR_TXMEND, priv->txd.dma);

	agnx_write32(ctl, AGNX_CIR_TXDSTART, priv->txd.dma);
	agnx_write32(ctl, AGNX_CIR_TXDEND, priv->txd.dma +
		     sizeof(struct agnx_desc) * priv->txd.size);

	/* FIXME Relinquish control of rings to card */
	reg = agnx_read32(ctl, AGNX_CIR_BLKCTL);
	reg &= ~0x800;
	agnx_write32(ctl, AGNX_CIR_BLKCTL, reg);
	return 0;
} /* fill_rings */

void unfill_rings(struct agnx_priv *priv)
{
	unsigned long flags;
	unsigned int i;
	AGNX_TRACE;

	spin_lock_irqsave(&priv->lock, flags);

	for (i = 0; i < priv->rx.size; i++)
		rx_desc_free(priv, i);
	for (i = 0; i < priv->txm.size; i++)
		txm_desc_free(priv, i);
	for (i = 0; i < priv->txd.size; i++)
		txd_desc_free(priv, i);

	spin_unlock_irqrestore(&priv->lock, flags);
}

/* Extract the bitrate out of a CCK PLCP header.
   copy from bcm43xx driver */
static inline u8 agnx_plcp_get_bitrate_cck(__be32 *phyhdr_11b)
{
	/* FIXME */
	switch (*(u8 *)phyhdr_11b) {
	case 0x0A:
		return 0;
	case 0x14:
		return 1;
	case 0x37:
		return 2;
	case 0x6E:
		return 3;
	}
	agnx_bug("Wrong plcp rate");
	return 0;
}

/* FIXME */
static inline u8 agnx_plcp_get_bitrate_ofdm(__be32 *phyhdr_11g)
{
	u8 rate = *(u8 *)phyhdr_11g & 0xF;

	printk(PFX "G mode rate is 0x%x\n", rate);
	return rate;
}

/* FIXME */
static void get_rx_stats(struct agnx_priv *priv, struct agnx_hdr *hdr,
			 struct ieee80211_rx_status *stat)
{
	void __iomem *ctl = priv->ctl;
	u8 *rssi;
	u32 noise;
	/* FIXME just for test */
	int snr = 40;		/* signal-to-noise ratio */

	memset(stat, 0, sizeof(*stat));
	/* RSSI */
	rssi = (u8 *)&hdr->phy_stats_lo;
//	stat->ssi = (rssi[0] + rssi[1] + rssi[2]) / 3;
	/* Noise */
	noise = ioread32(ctl + AGNX_GCR_NOISE0);
	noise += ioread32(ctl + AGNX_GCR_NOISE1);
	noise += ioread32(ctl + AGNX_GCR_NOISE2);
	stat->noise = noise / 3;
	/* Signal quality */
	//snr = stat->ssi - stat->noise;
	if (snr >=0 && snr < 40)
		stat->signal = 5 * snr / 2;
	else if (snr >= 40)
		stat->signal = 100;
	else
		stat->signal = 0;


	if (hdr->_11b0 && !hdr->_11g0) {
		stat->rate_idx = agnx_plcp_get_bitrate_cck(&hdr->_11b0);
	} else if (!hdr->_11b0 && hdr->_11g0) {
		printk(PFX "RX: Found G mode packet\n");
		stat->rate_idx = agnx_plcp_get_bitrate_ofdm(&hdr->_11g0);
	} else
		agnx_bug("Unknown packets type");


	stat->band = IEEE80211_BAND_2GHZ;
	stat->freq = agnx_channels[priv->channel - 1].center_freq;
//	stat->antenna = 3;
//	stat->mactime = be32_to_cpu(hdr->time_stamp);
//	stat->channel = priv->channel;

}

static inline void combine_hdr_frag(struct ieee80211_hdr *ieeehdr,
				    struct sk_buff *skb)
{
	u16 fctl;
	unsigned int hdrlen;

	fctl = le16_to_cpu(ieeehdr->frame_control);
	hdrlen = ieee80211_hdrlen(fctl);
	/* FIXME */
	if (hdrlen < (2+2+6)/*minimum hdr*/ ||
	    hdrlen > sizeof(struct ieee80211_mgmt)) {
		printk(KERN_ERR PFX "hdr len is %d\n", hdrlen);
		agnx_bug("Wrong ieee80211 hdr detected");
	}
	skb_push(skb, hdrlen);
	memcpy(skb->data, ieeehdr, hdrlen);
} /* combine_hdr_frag */

static inline int agnx_packet_check(struct agnx_priv *priv, struct agnx_hdr *agnxhdr,
				    unsigned packet_len)
{
	if (agnx_get_bits(CRC_FAIL, CRC_FAIL_SHIFT, be32_to_cpu(agnxhdr->reg1)) == 1){
		printk(PFX "RX: CRC check fail\n");
		goto drop;
	}
	if (packet_len > 2048) {
		printk(PFX "RX: Too long packet detected\n");
		goto drop;
	}

	/* FIXME Just usable for Promious Mode, for Manage mode exclude FCS */
/* 	if (packet_len - sizeof(*agnxhdr) < FCS_LEN) { */
/* 		printk(PFX "RX: Too short packet detected\n"); */
/* 		goto drop; */
/* 	} */
	return 0;
drop:
	priv->stats.dot11FCSErrorCount++;
	return -1;
}

void handle_rx_irq(struct agnx_priv *priv)
{
	struct ieee80211_rx_status status;
	unsigned int len;
//	AGNX_TRACE;

	do {
		struct agnx_desc *desc;
		u32 frag;
		struct agnx_info *info;
		struct agnx_hdr *hdr;
		struct sk_buff *skb;
		unsigned int i = priv->rx.idx % priv->rx.size;

		desc = priv->rx.desc + i;
		frag = be32_to_cpu(desc->frag);
		if (frag & OWNER)
			break;

		info = priv->rx.info + i;
		skb = info->skb;
		hdr = (struct agnx_hdr *)(skb->data);

		len = (frag & PACKET_LEN) >> PACKET_LEN_SHIFT;
		if (agnx_packet_check(priv, hdr, len) == -1) {
 			rx_desc_reusing(priv, i);
			continue;
		}
		skb_put(skb, len);

		do {
			u16 fctl;
			fctl = le16_to_cpu(((struct ieee80211_hdr *)hdr->mac_hdr)->frame_control);
			if ((fctl & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_BEACON)// && !(fctl & IEEE80211_STYPE_BEACON))
				dump_ieee80211_hdr((struct ieee80211_hdr *)hdr->mac_hdr, "RX");
		} while (0);

		if (hdr->_11b0 && !hdr->_11g0) {
/* 			int j; */
/* 			u16 fctl = le16_to_cpu(((struct ieee80211_hdr *)hdr->mac_hdr) */
/* 					       ->frame_control); */
/* 			if ( (fctl & IEEE80211_FCTL_FTYPE) ==  IEEE80211_FTYPE_DATA) { */
/* 				agnx_print_rx_hdr(hdr); */
// 				agnx_print_sta(priv, BSSID_STAID);
/* 				for (j = 0; j < 8; j++) */
/* 					agnx_print_sta_tx_wq(priv, BSSID_STAID, j);		 */
/* 			} */

			get_rx_stats(priv, hdr, &status);
			skb_pull(skb, sizeof(*hdr));
			combine_hdr_frag((struct ieee80211_hdr *)hdr->mac_hdr, skb);
		} else if (!hdr->_11b0 && hdr->_11g0) {
//			int j;
			agnx_print_rx_hdr(hdr);
			agnx_print_sta(priv, BSSID_STAID);
//			for (j = 0; j < 8; j++)
			agnx_print_sta_tx_wq(priv, BSSID_STAID, 0);

			print_hex_dump_bytes("agnx: RX_PACKET: ", DUMP_PREFIX_NONE,
					     skb->data, skb->len + 8);

//			if (agnx_plcp_get_bitrate_ofdm(&hdr->_11g0) == 0)
			get_rx_stats(priv, hdr, &status);
			skb_pull(skb, sizeof(*hdr));
			combine_hdr_frag((struct ieee80211_hdr *)
					 ((void *)&hdr->mac_hdr), skb);
//			dump_ieee80211_hdr((struct ieee80211_hdr *)skb->data, "RX G");
		} else
			agnx_bug("Unknown packets type");
		ieee80211_rx_irqsafe(priv->hw, skb, &status);
		rx_desc_reinit(priv, i);

	} while ( priv->rx.idx++ );
} /* handle_rx_irq */

static inline void handle_tx_irq(struct agnx_priv *priv, struct agnx_ring *ring)
{
	struct agnx_desc *desc;
	struct agnx_info *info;
	unsigned int idx;

	for (idx = ring->idx_sent; idx < ring->idx; idx++) {
		unsigned int i = idx % ring->size;
		u32  frag;

		desc = ring->desc + i;
		info = ring->info + i;

		frag = be32_to_cpu(desc->frag);
		if (frag & OWNER) {
			if (info->type == HEADER)
				break;
			else
				agnx_bug("TX error");
		}

		pci_unmap_single(priv->pdev, info->mapping, info->dma_len, PCI_DMA_TODEVICE);

		do {
//			int j;
			size_t len;
			len = info->skb->len - sizeof(struct agnx_hdr) + info->hdr_len;
			//	if (len == 614) {
//				agnx_print_desc(desc);
				if (info->type == PACKET) {
//					agnx_print_tx_hdr((struct agnx_hdr *)info->skb->data);
/* 					agnx_print_sta_power(priv, LOCAL_STAID); */
/* 					agnx_print_sta(priv, LOCAL_STAID); */
/* //					for (j = 0; j < 8; j++) */
/* 					agnx_print_sta_tx_wq(priv, LOCAL_STAID, 0); */
//					agnx_print_sta_power(priv, BSSID_STAID);
//					agnx_print_sta(priv, BSSID_STAID);
//					for (j = 0; j < 8; j++)
//					agnx_print_sta_tx_wq(priv, BSSID_STAID, 0);
				}
//			}
		} while (0);

		if (info->type == PACKET) {
//			dump_txm_registers(priv);
//			dump_rxm_registers(priv);
//			dump_bm_registers(priv);
//			dump_cir_registers(priv);
		}

		if (info->type == PACKET) {
//			struct ieee80211_hdr *hdr;
			struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(info->skb);

			skb_pull(info->skb, sizeof(struct agnx_hdr));
			memcpy(skb_push(info->skb, info->hdr_len), &info->hdr, info->hdr_len);

//			dump_ieee80211_hdr((struct ieee80211_hdr *)info->skb->data, "TX_HANDLE");
/* 			print_hex_dump_bytes("agnx: TX_HANDLE: ", DUMP_PREFIX_NONE, */
/* 					     info->skb->data, info->skb->len); */

			if (!(txi->flags & IEEE80211_TX_CTL_NO_ACK))
				txi->flags |= IEEE80211_TX_STAT_ACK;

			ieee80211_tx_status_irqsafe(priv->hw, info->skb);


/* 				info->tx_status.queue_number = (ring->size - i) / 2; */
/* 				ieee80211_tx_status_irqsafe(priv->hw, info->skb, &(info->tx_status)); */
/* 			} else */
/* 				dev_kfree_skb_irq(info->skb); */
 		}
		memset(desc, 0, sizeof(*desc));
		memset(info, 0, sizeof(*info));
	}

	ring->idx_sent = idx;
	/* TODO fill the priv->low_level_stats */

	/* ieee80211_wake_queue(priv->hw, 0); */
}

void handle_txm_irq(struct agnx_priv *priv)
{
	handle_tx_irq(priv, &priv->txm);
}

void handle_txd_irq(struct agnx_priv *priv)
{
	handle_tx_irq(priv, &priv->txd);
}

void handle_other_irq(struct agnx_priv *priv)
{
//	void __iomem *ctl = priv->ctl;
	u32 status = priv->irq_status;
	void __iomem *ctl = priv->ctl;
	u32 reg;

	if (status & IRQ_TX_BEACON) {
		iowrite32(IRQ_TX_BEACON, ctl + AGNX_INT_STAT);
		printk(PFX "IRQ: TX Beacon control is 0X%.8X\n", ioread32(ctl + AGNX_TXM_BEACON_CTL));
		printk(PFX "IRQ: TX Beacon rx frame num: %d\n", rx_frame_cnt);
	}
	if (status & IRQ_TX_RETRY) {
		reg = ioread32(ctl + AGNX_TXM_RETRYSTAID);
		printk(PFX "IRQ: TX Retry, RETRY STA ID is %x\n", reg);
	}
	if (status & IRQ_TX_ACTIVITY)
		printk(PFX "IRQ: TX Activity\n");
	if (status & IRQ_RX_ACTIVITY)
		printk(PFX "IRQ: RX Activity\n");
	if (status & IRQ_RX_X)
		printk(PFX "IRQ: RX X\n");
	if (status & IRQ_RX_Y) {
		reg = ioread32(ctl + AGNX_INT_MASK);
		reg &= ~IRQ_RX_Y;
		iowrite32(reg, ctl + AGNX_INT_MASK);
		iowrite32(IRQ_RX_Y, ctl + AGNX_INT_STAT);
		printk(PFX "IRQ: RX Y\n");
	}
	if (status & IRQ_RX_HASHHIT)  {
		reg = ioread32(ctl + AGNX_INT_MASK);
		reg &= ~IRQ_RX_HASHHIT;
		iowrite32(reg, ctl + AGNX_INT_MASK);
		iowrite32(IRQ_RX_HASHHIT, ctl + AGNX_INT_STAT);
		printk(PFX "IRQ: RX Hash Hit\n");

	}
	if (status & IRQ_RX_FRAME) {
		reg = ioread32(ctl + AGNX_INT_MASK);
		reg &= ~IRQ_RX_FRAME;
		iowrite32(reg, ctl + AGNX_INT_MASK);
		iowrite32(IRQ_RX_FRAME, ctl + AGNX_INT_STAT);
		printk(PFX "IRQ: RX Frame\n");
 		rx_frame_cnt++;
	}
	if (status & IRQ_ERR_INT) {
		iowrite32(IRQ_ERR_INT, ctl + AGNX_INT_STAT);
//		agnx_hw_reset(priv);
		printk(PFX "IRQ: Error Interrupt\n");
	}
	if (status & IRQ_TX_QUE_FULL)
		printk(PFX "IRQ: TX Workqueue Full\n");
	if (status & IRQ_BANDMAN_ERR)
		printk(PFX "IRQ: Bandwidth Management Error\n");
	if (status & IRQ_TX_DISABLE)
		printk(PFX "IRQ: TX Disable\n");
	if (status & IRQ_RX_IVASESKEY)
		printk(PFX "IRQ: RX Invalid Session Key\n");
	if (status & IRQ_REP_THHIT)
		printk(PFX "IRQ: Replay Threshold Hit\n");
	if (status & IRQ_TIMER1)
		printk(PFX "IRQ: Timer1\n");
	if (status & IRQ_TIMER_CNT)
		printk(PFX "IRQ: Timer Count\n");
	if (status & IRQ_PHY_FASTINT)
		printk(PFX "IRQ: Phy Fast Interrupt\n");
	if (status & IRQ_PHY_SLOWINT)
		printk(PFX "IRQ: Phy Slow Interrupt\n");
	if (status & IRQ_OTHER)
		printk(PFX "IRQ: 0x80000000\n");
} /* handle_other_irq */


static inline void route_flag_set(struct agnx_hdr *txhdr)
{
//	u32 reg = 0;

	/* FIXME */
/*  	reg = (0x7 << ROUTE_COMPRESSION_SHIFT) & ROUTE_COMPRESSION; */
/* 	txhdr->reg5 = cpu_to_be32(reg); */
 	txhdr->reg5 = (0xa << 0x0) | (0x7 << 0x18);
// 	txhdr->reg5 = cpu_to_be32((0xa << 0x0) | (0x7 << 0x18));
// 	txhdr->reg5 = cpu_to_be32(0x7 << 0x0);
}

/* Return 0 if no match */
static inline unsigned int get_power_level(unsigned int rate, unsigned int antennas_num)
{
	unsigned int power_level;

	switch (rate) {
	case 10:
	case 20:
	case 55:
	case 60:
	case 90:
	case 120: power_level = 22; break;
	case 180: power_level = 19; break;
	case 240: power_level = 18; break;
	case 360: power_level = 16; break;
	case 480: power_level = 15; break;
	case 540: power_level = 14; break;
	default:
		agnx_bug("Error rate setting\n");
	}

	if (power_level && (antennas_num == 2))
		power_level -= 3;

	return power_level;
}

static inline void fill_agnx_hdr(struct agnx_priv *priv, struct agnx_info *tx_info)
{
	struct agnx_hdr *txhdr = (struct agnx_hdr *)tx_info->skb->data;
	size_t len;
	u16 fc = le16_to_cpu(*(__le16 *)&tx_info->hdr);
	u32 reg;

	memset(txhdr, 0, sizeof(*txhdr));

//	reg = agnx_set_bits(STATION_ID, STATION_ID_SHIFT, LOCAL_STAID);
	reg = agnx_set_bits(STATION_ID, STATION_ID_SHIFT, BSSID_STAID);
	reg |= agnx_set_bits(WORKQUEUE_ID, WORKQUEUE_ID_SHIFT, 0);
	txhdr->reg4 = cpu_to_be32(reg);

	/* Set the Hardware Sequence Number to 1? */
	reg = agnx_set_bits(SEQUENCE_NUMBER, SEQUENCE_NUMBER_SHIFT, 0);
//	reg = agnx_set_bits(SEQUENCE_NUMBER, SEQUENCE_NUMBER_SHIFT, 1);
	reg |= agnx_set_bits(MAC_HDR_LEN, MAC_HDR_LEN_SHIFT, tx_info->hdr_len);
	txhdr->reg1 = cpu_to_be32(reg);
	/* Set the agnx_hdr's MAC header */
	memcpy(txhdr->mac_hdr, &tx_info->hdr, tx_info->hdr_len);

	reg = agnx_set_bits(ACK, ACK_SHIFT, 1);
//	reg = agnx_set_bits(ACK, ACK_SHIFT, 0);
	reg |= agnx_set_bits(MULTICAST, MULTICAST_SHIFT, 0);
//	reg |= agnx_set_bits(MULTICAST, MULTICAST_SHIFT, 1);
	reg |= agnx_set_bits(RELAY, RELAY_SHIFT, 0);
	reg |= agnx_set_bits(TM, TM_SHIFT, 0);
	txhdr->reg0 = cpu_to_be32(reg);

	/* Set the long and short retry limits */
 	txhdr->tx.short_retry_limit = tx_info->txi->control.rates[0].count;
 	txhdr->tx.long_retry_limit = tx_info->txi->control.rates[0].count;

	/* FIXME */
	len = tx_info->skb->len - sizeof(*txhdr) + tx_info->hdr_len + FCS_LEN;
	if (fc & IEEE80211_FCTL_PROTECTED)
		len += 8;
	len = 2398;
	reg = agnx_set_bits(FRAG_SIZE, FRAG_SIZE_SHIFT, len);
	len = tx_info->skb->len - sizeof(*txhdr);
	reg |= agnx_set_bits(PAYLOAD_LEN, PAYLOAD_LEN_SHIFT, len);
	txhdr->reg3 = cpu_to_be32(reg);

	route_flag_set(txhdr);
} /* fill_hdr */

static void txm_power_set(struct agnx_priv *priv,
			  struct ieee80211_tx_info *txi)
{
	struct agnx_sta_power power;
	u32 reg;

	/* FIXME */
	if (txi->control.rates[0].idx < 0) {
		/* For B mode Short Preamble */
		reg = agnx_set_bits(PHY_MODE, PHY_MODE_SHIFT, AGNX_MODE_80211B_SHORT);
//		control->tx_rate = -control->tx_rate;
	} else
		reg = agnx_set_bits(PHY_MODE, PHY_MODE_SHIFT, AGNX_MODE_80211G);
//		reg = agnx_set_bits(PHY_MODE, PHY_MODE_SHIFT, AGNX_MODE_80211B_LONG);
	reg |= agnx_set_bits(SIGNAL, SIGNAL_SHIFT, 0xB);
	reg |= agnx_set_bits(RATE, RATE_SHIFT, 0xB);
//	reg |= agnx_set_bits(POWER_LEVEL, POWER_LEVEL_SHIFT, 15);
	reg |= agnx_set_bits(POWER_LEVEL, POWER_LEVEL_SHIFT, 20);
	/* if rate < 11M set it to 0 */
	reg |= agnx_set_bits(NUM_TRANSMITTERS, NUM_TRANSMITTERS_SHIFT, 1);
//	reg |= agnx_set_bits(EDCF, EDCF_SHIFT, 1);
//	reg |= agnx_set_bits(TIFS, TIFS_SHIFT, 1);

	power.reg = reg;
//	power.reg = cpu_to_le32(reg);

//	set_sta_power(priv, &power, LOCAL_STAID);
	set_sta_power(priv, &power, BSSID_STAID);
}

static inline int tx_packet_check(struct sk_buff *skb)
{
	unsigned int ieee_len = ieee80211_get_hdrlen_from_skb(skb);
	if (skb->len > 2048) {
		printk(KERN_ERR PFX "length is %d\n", skb->len);
		agnx_bug("Too long TX skb");
		return -1;
	}
	/* FIXME */
	if (skb->len == ieee_len) {
		printk(PFX "A strange TX packet\n");
		return -1;
		/* tx_faile_irqsafe(); */
	}
	return 0;
}

static int __agnx_tx(struct agnx_priv *priv, struct sk_buff *skb,
		     struct agnx_ring *ring)
{
	struct agnx_desc *hdr_desc, *frag_desc;
	struct agnx_info *hdr_info, *frag_info;
	struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&priv->lock, flags);

	/* The RX interrupt need be Disable until this TX packet
	   is handled in the next tx interrupt */
	disable_rx_interrupt(priv);

	i = ring->idx;
	ring->idx += 2;
/*   	if (priv->txm_idx - priv->txm_idx_sent == AGNX_TXM_RING_SIZE - 2) */
/* 		ieee80211_stop_queue(priv->hw, 0); */

	/* Set agnx header's info and desc */
	i %= ring->size;
	hdr_desc = ring->desc + i;
	hdr_info = ring->info + i;
	hdr_info->hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	memcpy(&hdr_info->hdr, skb->data, hdr_info->hdr_len);

	/* Add the agnx header to the front of the SKB */
	skb_push(skb, sizeof(struct agnx_hdr) - hdr_info->hdr_len);

	hdr_info->txi = txi;
	hdr_info->dma_len = sizeof(struct agnx_hdr);
	hdr_info->skb = skb;
	hdr_info->type = HEADER;
	fill_agnx_hdr(priv, hdr_info);
	hdr_info->mapping = pci_map_single(priv->pdev, skb->data,
					   hdr_info->dma_len, PCI_DMA_TODEVICE);
	do {
		u32 frag = 0;
		frag |= agnx_set_bits(FIRST_FRAG, FIRST_FRAG_SHIFT, 1);
		frag |= agnx_set_bits(LAST_FRAG, LAST_FRAG_SHIFT, 0);
		frag |= agnx_set_bits(PACKET_LEN, PACKET_LEN_SHIFT, skb->len);
		frag |= agnx_set_bits(FIRST_FRAG_LEN, FIRST_FRAG_LEN_SHIFT, 1);
		frag |= agnx_set_bits(OWNER, OWNER_SHIFT, 1);
		hdr_desc->frag = cpu_to_be32(frag);
	} while (0);
	hdr_desc->dma_addr = cpu_to_be32(hdr_info->mapping);


	/* Set Frag's info and desc */
	i = (i + 1) % ring->size;
	frag_desc = ring->desc + i;
	frag_info = ring->info + i;
	memcpy(frag_info, hdr_info, sizeof(struct agnx_info));
	frag_info->type = PACKET;
	frag_info->dma_len = skb->len - hdr_info->dma_len;
	frag_info->mapping = pci_map_single(priv->pdev, skb->data + hdr_info->dma_len,
					    frag_info->dma_len, PCI_DMA_TODEVICE);
	do {
		u32 frag = 0;
		frag |= agnx_set_bits(FIRST_FRAG, FIRST_FRAG_SHIFT, 0);
		frag |= agnx_set_bits(LAST_FRAG, LAST_FRAG_SHIFT, 1);
		frag |= agnx_set_bits(PACKET_LEN, PACKET_LEN_SHIFT, skb->len);
		frag |= agnx_set_bits(SUB_FRAG_LEN, SUB_FRAG_LEN_SHIFT, frag_info->dma_len);
		frag_desc->frag = cpu_to_be32(frag);
	} while (0);
	frag_desc->dma_addr = cpu_to_be32(frag_info->mapping);

	txm_power_set(priv, txi);

/* 	do { */
/* 		int j; */
/* 		size_t len; */
/* 		len = skb->len - hdr_info->dma_len + hdr_info->hdr_len;  */
/* //		if (len == 614) { */
/* 			agnx_print_desc(hdr_desc); */
/* 			agnx_print_desc(frag_desc); */
/* 			agnx_print_tx_hdr((struct agnx_hdr *)skb->data); */
/* 			agnx_print_sta_power(priv, LOCAL_STAID); */
/* 			agnx_print_sta(priv, LOCAL_STAID); */
/* 			for (j = 0; j < 8; j++) */
/* 				agnx_print_sta_tx_wq(priv, LOCAL_STAID, j); */
/* 			agnx_print_sta_power(priv, BSSID_STAID); */
/* 			agnx_print_sta(priv, BSSID_STAID); */
/* 			for (j = 0; j < 8; j++) */
/* 				agnx_print_sta_tx_wq(priv, BSSID_STAID, j); */
/* 			//	} */
/* 	} while (0); */

	spin_unlock_irqrestore(&priv->lock, flags);

	/* FIXME ugly code */
	/* Trigger TXM */
	do {
		u32 reg;
		reg = (ioread32(priv->ctl + AGNX_CIR_TXMCTL));
		reg |= 0x8;
		iowrite32((reg), priv->ctl + AGNX_CIR_TXMCTL);
	}while (0);

	/* Trigger TXD */
	do {
		u32 reg;
		reg = (ioread32(priv->ctl + AGNX_CIR_TXDCTL));
		reg |= 0x8;
		iowrite32((reg), priv->ctl + AGNX_CIR_TXDCTL);
	}while (0);

	return 0;
}

int _agnx_tx(struct agnx_priv *priv, struct sk_buff *skb)
{
	u16 fctl;

	if (tx_packet_check(skb))
		return 0;

/* 	print_hex_dump_bytes("agnx: TX_PACKET: ", DUMP_PREFIX_NONE, */
/* 			     skb->data, skb->len); */

        fctl = le16_to_cpu(*((__le16 *)skb->data));

	if ( (fctl & IEEE80211_FCTL_FTYPE)  == IEEE80211_FTYPE_DATA )
		return __agnx_tx(priv, skb, &priv->txd);
	else
		return __agnx_tx(priv, skb, &priv->txm);
}
