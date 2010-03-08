
/*
 * Linux device driver for ADMtek ADM8211 (IEEE 802.11b MAC/BBP)
 *
 * Copyright (c) 2003, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2004-2007, Michael Wu <flamingice@sourmilk.net>
 * Some parts copyright (c) 2003 by David Young <dyoung@pobox.com>
 * and used with permission.
 *
 * Much thanks to Infineon-ADMtek for their support of this driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */

#include <linux/init.h>
#include <linux/if.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/eeprom_93cx6.h>
#include <net/mac80211.h>

#include "adm8211.h"

MODULE_AUTHOR("Michael Wu <flamingice@sourmilk.net>");
MODULE_AUTHOR("Jouni Malinen <j@w1.fi>");
MODULE_DESCRIPTION("Driver for IEEE 802.11b wireless cards based on ADMtek ADM8211");
MODULE_SUPPORTED_DEVICE("ADM8211");
MODULE_LICENSE("GPL");

static unsigned int tx_ring_size __read_mostly = 16;
static unsigned int rx_ring_size __read_mostly = 16;

module_param(tx_ring_size, uint, 0);
module_param(rx_ring_size, uint, 0);

static DEFINE_PCI_DEVICE_TABLE(adm8211_pci_id_table) = {
	/* ADMtek ADM8211 */
	{ PCI_DEVICE(0x10B7, 0x6000) }, /* 3Com 3CRSHPW796 */
	{ PCI_DEVICE(0x1200, 0x8201) }, /* ? */
	{ PCI_DEVICE(0x1317, 0x8201) }, /* ADM8211A */
	{ PCI_DEVICE(0x1317, 0x8211) }, /* ADM8211B/C */
	{ 0 }
};

static struct ieee80211_rate adm8211_rates[] = {
	{ .bitrate = 10, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 20, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 220, .flags = IEEE80211_RATE_SHORT_PREAMBLE }, /* XX ?? */
};

static const struct ieee80211_channel adm8211_channels[] = {
	{ .center_freq = 2412},
	{ .center_freq = 2417},
	{ .center_freq = 2422},
	{ .center_freq = 2427},
	{ .center_freq = 2432},
	{ .center_freq = 2437},
	{ .center_freq = 2442},
	{ .center_freq = 2447},
	{ .center_freq = 2452},
	{ .center_freq = 2457},
	{ .center_freq = 2462},
	{ .center_freq = 2467},
	{ .center_freq = 2472},
	{ .center_freq = 2484},
};


static void adm8211_eeprom_register_read(struct eeprom_93cx6 *eeprom)
{
	struct adm8211_priv *priv = eeprom->data;
	u32 reg = ADM8211_CSR_READ(SPR);

	eeprom->reg_data_in = reg & ADM8211_SPR_SDI;
	eeprom->reg_data_out = reg & ADM8211_SPR_SDO;
	eeprom->reg_data_clock = reg & ADM8211_SPR_SCLK;
	eeprom->reg_chip_select = reg & ADM8211_SPR_SCS;
}

static void adm8211_eeprom_register_write(struct eeprom_93cx6 *eeprom)
{
	struct adm8211_priv *priv = eeprom->data;
	u32 reg = 0x4000 | ADM8211_SPR_SRS;

	if (eeprom->reg_data_in)
		reg |= ADM8211_SPR_SDI;
	if (eeprom->reg_data_out)
		reg |= ADM8211_SPR_SDO;
	if (eeprom->reg_data_clock)
		reg |= ADM8211_SPR_SCLK;
	if (eeprom->reg_chip_select)
		reg |= ADM8211_SPR_SCS;

	ADM8211_CSR_WRITE(SPR, reg);
	ADM8211_CSR_READ(SPR);		/* eeprom_delay */
}

static int adm8211_read_eeprom(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;
	unsigned int words, i;
	struct ieee80211_chan_range chan_range;
	u16 cr49;
	struct eeprom_93cx6 eeprom = {
		.data		= priv,
		.register_read	= adm8211_eeprom_register_read,
		.register_write	= adm8211_eeprom_register_write
	};

	if (ADM8211_CSR_READ(CSR_TEST0) & ADM8211_CSR_TEST0_EPTYP) {
		/* 256 * 16-bit = 512 bytes */
		eeprom.width = PCI_EEPROM_WIDTH_93C66;
		words = 256;
	} else {
		/* 64 * 16-bit = 128 bytes */
		eeprom.width = PCI_EEPROM_WIDTH_93C46;
		words = 64;
	}

	priv->eeprom_len = words * 2;
	priv->eeprom = kmalloc(priv->eeprom_len, GFP_KERNEL);
	if (!priv->eeprom)
		return -ENOMEM;

	eeprom_93cx6_multiread(&eeprom, 0, (__le16 *)priv->eeprom, words);

	cr49 = le16_to_cpu(priv->eeprom->cr49);
	priv->rf_type = (cr49 >> 3) & 0x7;
	switch (priv->rf_type) {
	case ADM8211_TYPE_INTERSIL:
	case ADM8211_TYPE_RFMD:
	case ADM8211_TYPE_MARVEL:
	case ADM8211_TYPE_AIROHA:
	case ADM8211_TYPE_ADMTEK:
		break;

	default:
		if (priv->pdev->revision < ADM8211_REV_CA)
			priv->rf_type = ADM8211_TYPE_RFMD;
		else
			priv->rf_type = ADM8211_TYPE_AIROHA;

		printk(KERN_WARNING "%s (adm8211): Unknown RFtype %d\n",
		       pci_name(priv->pdev), (cr49 >> 3) & 0x7);
	}

	priv->bbp_type = cr49 & 0x7;
	switch (priv->bbp_type) {
	case ADM8211_TYPE_INTERSIL:
	case ADM8211_TYPE_RFMD:
	case ADM8211_TYPE_MARVEL:
	case ADM8211_TYPE_AIROHA:
	case ADM8211_TYPE_ADMTEK:
		break;
	default:
		if (priv->pdev->revision < ADM8211_REV_CA)
			priv->bbp_type = ADM8211_TYPE_RFMD;
		else
			priv->bbp_type = ADM8211_TYPE_ADMTEK;

		printk(KERN_WARNING "%s (adm8211): Unknown BBPtype: %d\n",
		       pci_name(priv->pdev), cr49 >> 3);
	}

	if (priv->eeprom->country_code >= ARRAY_SIZE(cranges)) {
		printk(KERN_WARNING "%s (adm8211): Invalid country code (%d)\n",
		       pci_name(priv->pdev), priv->eeprom->country_code);

		chan_range = cranges[2];
	} else
		chan_range = cranges[priv->eeprom->country_code];

	printk(KERN_DEBUG "%s (adm8211): Channel range: %d - %d\n",
	       pci_name(priv->pdev), (int)chan_range.min, (int)chan_range.max);

	BUILD_BUG_ON(sizeof(priv->channels) != sizeof(adm8211_channels));

	memcpy(priv->channels, adm8211_channels, sizeof(priv->channels));
	priv->band.channels = priv->channels;
	priv->band.n_channels = ARRAY_SIZE(adm8211_channels);
	priv->band.bitrates = adm8211_rates;
	priv->band.n_bitrates = ARRAY_SIZE(adm8211_rates);

	for (i = 1; i <= ARRAY_SIZE(adm8211_channels); i++)
		if (i < chan_range.min || i > chan_range.max)
			priv->channels[i - 1].flags |= IEEE80211_CHAN_DISABLED;

	switch (priv->eeprom->specific_bbptype) {
	case ADM8211_BBP_RFMD3000:
	case ADM8211_BBP_RFMD3002:
	case ADM8211_BBP_ADM8011:
		priv->specific_bbptype = priv->eeprom->specific_bbptype;
		break;

	default:
		if (priv->pdev->revision < ADM8211_REV_CA)
			priv->specific_bbptype = ADM8211_BBP_RFMD3000;
		else
			priv->specific_bbptype = ADM8211_BBP_ADM8011;

		printk(KERN_WARNING "%s (adm8211): Unknown specific BBP: %d\n",
		       pci_name(priv->pdev), priv->eeprom->specific_bbptype);
	}

	switch (priv->eeprom->specific_rftype) {
	case ADM8211_RFMD2948:
	case ADM8211_RFMD2958:
	case ADM8211_RFMD2958_RF3000_CONTROL_POWER:
	case ADM8211_MAX2820:
	case ADM8211_AL2210L:
		priv->transceiver_type = priv->eeprom->specific_rftype;
		break;

	default:
		if (priv->pdev->revision == ADM8211_REV_BA)
			priv->transceiver_type = ADM8211_RFMD2958_RF3000_CONTROL_POWER;
		else if (priv->pdev->revision == ADM8211_REV_CA)
			priv->transceiver_type = ADM8211_AL2210L;
		else if (priv->pdev->revision == ADM8211_REV_AB)
			priv->transceiver_type = ADM8211_RFMD2948;

		printk(KERN_WARNING "%s (adm8211): Unknown transceiver: %d\n",
		       pci_name(priv->pdev), priv->eeprom->specific_rftype);

		break;
	}

	printk(KERN_DEBUG "%s (adm8211): RFtype=%d BBPtype=%d Specific BBP=%d "
               "Transceiver=%d\n", pci_name(priv->pdev), priv->rf_type,
	       priv->bbp_type, priv->specific_bbptype, priv->transceiver_type);

	return 0;
}

static inline void adm8211_write_sram(struct ieee80211_hw *dev,
				      u32 addr, u32 data)
{
	struct adm8211_priv *priv = dev->priv;

	ADM8211_CSR_WRITE(WEPCTL, addr | ADM8211_WEPCTL_TABLE_WR |
			  (priv->pdev->revision < ADM8211_REV_BA ?
			   0 : ADM8211_WEPCTL_SEL_WEPTABLE ));
	ADM8211_CSR_READ(WEPCTL);
	msleep(1);

	ADM8211_CSR_WRITE(WESK, data);
	ADM8211_CSR_READ(WESK);
	msleep(1);
}

static void adm8211_write_sram_bytes(struct ieee80211_hw *dev,
				     unsigned int addr, u8 *buf,
				     unsigned int len)
{
	struct adm8211_priv *priv = dev->priv;
	u32 reg = ADM8211_CSR_READ(WEPCTL);
	unsigned int i;

	if (priv->pdev->revision < ADM8211_REV_BA) {
		for (i = 0; i < len; i += 2) {
			u16 val = buf[i] | (buf[i + 1] << 8);
			adm8211_write_sram(dev, addr + i / 2, val);
		}
	} else {
		for (i = 0; i < len; i += 4) {
			u32 val = (buf[i + 0] << 0 ) | (buf[i + 1] << 8 ) |
				  (buf[i + 2] << 16) | (buf[i + 3] << 24);
			adm8211_write_sram(dev, addr + i / 4, val);
		}
	}

	ADM8211_CSR_WRITE(WEPCTL, reg);
}

static void adm8211_clear_sram(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;
	u32 reg = ADM8211_CSR_READ(WEPCTL);
	unsigned int addr;

	for (addr = 0; addr < ADM8211_SRAM_SIZE; addr++)
		adm8211_write_sram(dev, addr, 0);

	ADM8211_CSR_WRITE(WEPCTL, reg);
}

static int adm8211_get_stats(struct ieee80211_hw *dev,
			     struct ieee80211_low_level_stats *stats)
{
	struct adm8211_priv *priv = dev->priv;

	memcpy(stats, &priv->stats, sizeof(*stats));

	return 0;
}

static void adm8211_interrupt_tci(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;
	unsigned int dirty_tx;

	spin_lock(&priv->lock);

	for (dirty_tx = priv->dirty_tx; priv->cur_tx - dirty_tx; dirty_tx++) {
		unsigned int entry = dirty_tx % priv->tx_ring_size;
		u32 status = le32_to_cpu(priv->tx_ring[entry].status);
		struct ieee80211_tx_info *txi;
		struct adm8211_tx_ring_info *info;
		struct sk_buff *skb;

		if (status & TDES0_CONTROL_OWN ||
		    !(status & TDES0_CONTROL_DONE))
			break;

		info = &priv->tx_buffers[entry];
		skb = info->skb;
		txi = IEEE80211_SKB_CB(skb);

		/* TODO: check TDES0_STATUS_TUF and TDES0_STATUS_TRO */

		pci_unmap_single(priv->pdev, info->mapping,
				 info->skb->len, PCI_DMA_TODEVICE);

		ieee80211_tx_info_clear_status(txi);

		skb_pull(skb, sizeof(struct adm8211_tx_hdr));
		memcpy(skb_push(skb, info->hdrlen), skb->cb, info->hdrlen);
		if (!(txi->flags & IEEE80211_TX_CTL_NO_ACK) &&
		    !(status & TDES0_STATUS_ES))
			txi->flags |= IEEE80211_TX_STAT_ACK;

		ieee80211_tx_status_irqsafe(dev, skb);

		info->skb = NULL;
	}

	if (priv->cur_tx - dirty_tx < priv->tx_ring_size - 2)
		ieee80211_wake_queue(dev, 0);

	priv->dirty_tx = dirty_tx;
	spin_unlock(&priv->lock);
}


static void adm8211_interrupt_rci(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;
	unsigned int entry = priv->cur_rx % priv->rx_ring_size;
	u32 status;
	unsigned int pktlen;
	struct sk_buff *skb, *newskb;
	unsigned int limit = priv->rx_ring_size;
	u8 rssi, rate;

	while (!(priv->rx_ring[entry].status & cpu_to_le32(RDES0_STATUS_OWN))) {
		if (!limit--)
			break;

		status = le32_to_cpu(priv->rx_ring[entry].status);
		rate = (status & RDES0_STATUS_RXDR) >> 12;
		rssi = le32_to_cpu(priv->rx_ring[entry].length) &
			RDES1_STATUS_RSSI;

		pktlen = status & RDES0_STATUS_FL;
		if (pktlen > RX_PKT_SIZE) {
			if (net_ratelimit())
				printk(KERN_DEBUG "%s: frame too long (%d)\n",
				       wiphy_name(dev->wiphy), pktlen);
			pktlen = RX_PKT_SIZE;
		}

		if (!priv->soft_rx_crc && status & RDES0_STATUS_ES) {
			skb = NULL; /* old buffer will be reused */
			/* TODO: update RX error stats */
			/* TODO: check RDES0_STATUS_CRC*E */
		} else if (pktlen < RX_COPY_BREAK) {
			skb = dev_alloc_skb(pktlen);
			if (skb) {
				pci_dma_sync_single_for_cpu(
					priv->pdev,
					priv->rx_buffers[entry].mapping,
					pktlen, PCI_DMA_FROMDEVICE);
				memcpy(skb_put(skb, pktlen),
				       skb_tail_pointer(priv->rx_buffers[entry].skb),
				       pktlen);
				pci_dma_sync_single_for_device(
					priv->pdev,
					priv->rx_buffers[entry].mapping,
					RX_PKT_SIZE, PCI_DMA_FROMDEVICE);
			}
		} else {
			newskb = dev_alloc_skb(RX_PKT_SIZE);
			if (newskb) {
				skb = priv->rx_buffers[entry].skb;
				skb_put(skb, pktlen);
				pci_unmap_single(
					priv->pdev,
					priv->rx_buffers[entry].mapping,
					RX_PKT_SIZE, PCI_DMA_FROMDEVICE);
				priv->rx_buffers[entry].skb = newskb;
				priv->rx_buffers[entry].mapping =
					pci_map_single(priv->pdev,
						       skb_tail_pointer(newskb),
						       RX_PKT_SIZE,
						       PCI_DMA_FROMDEVICE);
			} else {
				skb = NULL;
				/* TODO: update rx dropped stats */
			}

			priv->rx_ring[entry].buffer1 =
				cpu_to_le32(priv->rx_buffers[entry].mapping);
		}

		priv->rx_ring[entry].status = cpu_to_le32(RDES0_STATUS_OWN |
							  RDES0_STATUS_SQL);
		priv->rx_ring[entry].length =
			cpu_to_le32(RX_PKT_SIZE |
				    (entry == priv->rx_ring_size - 1 ?
				     RDES1_CONTROL_RER : 0));

		if (skb) {
			struct ieee80211_rx_status rx_status = {0};

			if (priv->pdev->revision < ADM8211_REV_CA)
				rx_status.signal = rssi;
			else
				rx_status.signal = 100 - rssi;

			rx_status.rate_idx = rate;

			rx_status.freq = adm8211_channels[priv->channel - 1].center_freq;
			rx_status.band = IEEE80211_BAND_2GHZ;

			memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));
			ieee80211_rx_irqsafe(dev, skb);
		}

		entry = (++priv->cur_rx) % priv->rx_ring_size;
	}

	/* TODO: check LPC and update stats? */
}


static irqreturn_t adm8211_interrupt(int irq, void *dev_id)
{
#define ADM8211_INT(x)							   \
do {									   \
	if (unlikely(stsr & ADM8211_STSR_ ## x))			   \
		printk(KERN_DEBUG "%s: " #x "\n", wiphy_name(dev->wiphy)); \
} while (0)

	struct ieee80211_hw *dev = dev_id;
	struct adm8211_priv *priv = dev->priv;
	u32 stsr = ADM8211_CSR_READ(STSR);
	ADM8211_CSR_WRITE(STSR, stsr);
	if (stsr == 0xffffffff)
		return IRQ_HANDLED;

	if (!(stsr & (ADM8211_STSR_NISS | ADM8211_STSR_AISS)))
		return IRQ_HANDLED;

	if (stsr & ADM8211_STSR_RCI)
		adm8211_interrupt_rci(dev);
	if (stsr & ADM8211_STSR_TCI)
		adm8211_interrupt_tci(dev);

	ADM8211_INT(PCF);
	ADM8211_INT(BCNTC);
	ADM8211_INT(GPINT);
	ADM8211_INT(ATIMTC);
	ADM8211_INT(TSFTF);
	ADM8211_INT(TSCZ);
	ADM8211_INT(SQL);
	ADM8211_INT(WEPTD);
	ADM8211_INT(ATIME);
	ADM8211_INT(TEIS);
	ADM8211_INT(FBE);
	ADM8211_INT(REIS);
	ADM8211_INT(GPTT);
	ADM8211_INT(RPS);
	ADM8211_INT(RDU);
	ADM8211_INT(TUF);
	ADM8211_INT(TPS);

	return IRQ_HANDLED;

#undef ADM8211_INT
}

#define WRITE_SYN(name,v_mask,v_shift,a_mask,a_shift,bits,prewrite,postwrite)\
static void adm8211_rf_write_syn_ ## name (struct ieee80211_hw *dev,	     \
					   u16 addr, u32 value) {	     \
	struct adm8211_priv *priv = dev->priv;				     \
	unsigned int i;							     \
	u32 reg, bitbuf;						     \
									     \
	value &= v_mask;						     \
	addr &= a_mask;							     \
	bitbuf = (value << v_shift) | (addr << a_shift);		     \
									     \
	ADM8211_CSR_WRITE(SYNRF, ADM8211_SYNRF_IF_SELECT_1);		     \
	ADM8211_CSR_READ(SYNRF);					     \
	ADM8211_CSR_WRITE(SYNRF, ADM8211_SYNRF_IF_SELECT_0);		     \
	ADM8211_CSR_READ(SYNRF);					     \
									     \
	if (prewrite) {							     \
		ADM8211_CSR_WRITE(SYNRF, ADM8211_SYNRF_WRITE_SYNDATA_0);     \
		ADM8211_CSR_READ(SYNRF);				     \
	}								     \
									     \
	for (i = 0; i <= bits; i++) {					     \
		if (bitbuf & (1 << (bits - i)))				     \
			reg = ADM8211_SYNRF_WRITE_SYNDATA_1;		     \
		else							     \
			reg = ADM8211_SYNRF_WRITE_SYNDATA_0;		     \
									     \
		ADM8211_CSR_WRITE(SYNRF, reg);				     \
		ADM8211_CSR_READ(SYNRF);				     \
									     \
		ADM8211_CSR_WRITE(SYNRF, reg | ADM8211_SYNRF_WRITE_CLOCK_1); \
		ADM8211_CSR_READ(SYNRF);				     \
		ADM8211_CSR_WRITE(SYNRF, reg | ADM8211_SYNRF_WRITE_CLOCK_0); \
		ADM8211_CSR_READ(SYNRF);				     \
	}								     \
									     \
	if (postwrite == 1) {						     \
		ADM8211_CSR_WRITE(SYNRF, reg | ADM8211_SYNRF_IF_SELECT_0);   \
		ADM8211_CSR_READ(SYNRF);				     \
	}								     \
	if (postwrite == 2) {						     \
		ADM8211_CSR_WRITE(SYNRF, reg | ADM8211_SYNRF_IF_SELECT_1);   \
		ADM8211_CSR_READ(SYNRF);				     \
	}								     \
									     \
	ADM8211_CSR_WRITE(SYNRF, 0);					     \
	ADM8211_CSR_READ(SYNRF);					     \
}

WRITE_SYN(max2820,  0x00FFF, 0, 0x0F, 12, 15, 1, 1)
WRITE_SYN(al2210l,  0xFFFFF, 4, 0x0F,  0, 23, 1, 1)
WRITE_SYN(rfmd2958, 0x3FFFF, 0, 0x1F, 18, 23, 0, 1)
WRITE_SYN(rfmd2948, 0x0FFFF, 4, 0x0F,  0, 21, 0, 2)

#undef WRITE_SYN

static int adm8211_write_bbp(struct ieee80211_hw *dev, u8 addr, u8 data)
{
	struct adm8211_priv *priv = dev->priv;
	unsigned int timeout;
	u32 reg;

	timeout = 10;
	while (timeout > 0) {
		reg = ADM8211_CSR_READ(BBPCTL);
		if (!(reg & (ADM8211_BBPCTL_WR | ADM8211_BBPCTL_RD)))
			break;
		timeout--;
		msleep(2);
	}

	if (timeout == 0) {
		printk(KERN_DEBUG "%s: adm8211_write_bbp(%d,%d) failed"
		       " prewrite (reg=0x%08x)\n",
		       wiphy_name(dev->wiphy), addr, data, reg);
		return -ETIMEDOUT;
	}

	switch (priv->bbp_type) {
	case ADM8211_TYPE_INTERSIL:
		reg = ADM8211_BBPCTL_MMISEL;	/* three wire interface */
		break;
	case ADM8211_TYPE_RFMD:
		reg = (0x20 << 24) | ADM8211_BBPCTL_TXCE | ADM8211_BBPCTL_CCAP |
		      (0x01 << 18);
		break;
	case ADM8211_TYPE_ADMTEK:
		reg = (0x20 << 24) | ADM8211_BBPCTL_TXCE | ADM8211_BBPCTL_CCAP |
		      (0x05 << 18);
		break;
	}
	reg |= ADM8211_BBPCTL_WR | (addr << 8) | data;

	ADM8211_CSR_WRITE(BBPCTL, reg);

	timeout = 10;
	while (timeout > 0) {
		reg = ADM8211_CSR_READ(BBPCTL);
		if (!(reg & ADM8211_BBPCTL_WR))
			break;
		timeout--;
		msleep(2);
	}

	if (timeout == 0) {
		ADM8211_CSR_WRITE(BBPCTL, ADM8211_CSR_READ(BBPCTL) &
				  ~ADM8211_BBPCTL_WR);
		printk(KERN_DEBUG "%s: adm8211_write_bbp(%d,%d) failed"
		       " postwrite (reg=0x%08x)\n",
		       wiphy_name(dev->wiphy), addr, data, reg);
		return -ETIMEDOUT;
	}

	return 0;
}

static int adm8211_rf_set_channel(struct ieee80211_hw *dev, unsigned int chan)
{
	static const u32 adm8211_rfmd2958_reg5[] =
		{0x22BD, 0x22D2, 0x22E8, 0x22FE, 0x2314, 0x232A, 0x2340,
		 0x2355, 0x236B, 0x2381, 0x2397, 0x23AD, 0x23C2, 0x23F7};
	static const u32 adm8211_rfmd2958_reg6[] =
		{0x05D17, 0x3A2E8, 0x2E8BA, 0x22E8B, 0x1745D, 0x0BA2E, 0x00000,
		 0x345D1, 0x28BA2, 0x1D174, 0x11745, 0x05D17, 0x3A2E8, 0x11745};

	struct adm8211_priv *priv = dev->priv;
	u8 ant_power = priv->ant_power > 0x3F ?
		priv->eeprom->antenna_power[chan - 1] : priv->ant_power;
	u8 tx_power = priv->tx_power > 0x3F ?
		priv->eeprom->tx_power[chan - 1] : priv->tx_power;
	u8 lpf_cutoff = priv->lpf_cutoff == 0xFF ?
		priv->eeprom->lpf_cutoff[chan - 1] : priv->lpf_cutoff;
	u8 lnags_thresh = priv->lnags_threshold == 0xFF ?
		priv->eeprom->lnags_threshold[chan - 1] : priv->lnags_threshold;
	u32 reg;

	ADM8211_IDLE();

	/* Program synthesizer to new channel */
	switch (priv->transceiver_type) {
	case ADM8211_RFMD2958:
	case ADM8211_RFMD2958_RF3000_CONTROL_POWER:
		adm8211_rf_write_syn_rfmd2958(dev, 0x00, 0x04007);
		adm8211_rf_write_syn_rfmd2958(dev, 0x02, 0x00033);

		adm8211_rf_write_syn_rfmd2958(dev, 0x05,
			adm8211_rfmd2958_reg5[chan - 1]);
		adm8211_rf_write_syn_rfmd2958(dev, 0x06,
			adm8211_rfmd2958_reg6[chan - 1]);
		break;

	case ADM8211_RFMD2948:
		adm8211_rf_write_syn_rfmd2948(dev, SI4126_MAIN_CONF,
					      SI4126_MAIN_XINDIV2);
		adm8211_rf_write_syn_rfmd2948(dev, SI4126_POWERDOWN,
					      SI4126_POWERDOWN_PDIB |
					      SI4126_POWERDOWN_PDRB);
		adm8211_rf_write_syn_rfmd2948(dev, SI4126_PHASE_DET_GAIN, 0);
		adm8211_rf_write_syn_rfmd2948(dev, SI4126_RF2_N_DIV,
					      (chan == 14 ?
					       2110 : (2033 + (chan * 5))));
		adm8211_rf_write_syn_rfmd2948(dev, SI4126_IF_N_DIV, 1496);
		adm8211_rf_write_syn_rfmd2948(dev, SI4126_RF2_R_DIV, 44);
		adm8211_rf_write_syn_rfmd2948(dev, SI4126_IF_R_DIV, 44);
		break;

	case ADM8211_MAX2820:
		adm8211_rf_write_syn_max2820(dev, 0x3,
			(chan == 14 ? 0x054 : (0x7 + (chan * 5))));
		break;

	case ADM8211_AL2210L:
		adm8211_rf_write_syn_al2210l(dev, 0x0,
			(chan == 14 ? 0x229B4 : (0x22967 + (chan * 5))));
		break;

	default:
		printk(KERN_DEBUG "%s: unsupported transceiver type %d\n",
		       wiphy_name(dev->wiphy), priv->transceiver_type);
		break;
	}

	/* write BBP regs */
	if (priv->bbp_type == ADM8211_TYPE_RFMD) {

	/* SMC 2635W specific? adm8211b doesn't use the 2948 though.. */
	/* TODO: remove if SMC 2635W doesn't need this */
	if (priv->transceiver_type == ADM8211_RFMD2948) {
		reg = ADM8211_CSR_READ(GPIO);
		reg &= 0xfffc0000;
		reg |= ADM8211_CSR_GPIO_EN0;
		if (chan != 14)
			reg |= ADM8211_CSR_GPIO_O0;
		ADM8211_CSR_WRITE(GPIO, reg);
	}

	if (priv->transceiver_type == ADM8211_RFMD2958) {
		/* set PCNT2 */
		adm8211_rf_write_syn_rfmd2958(dev, 0x0B, 0x07100);
		/* set PCNT1 P_DESIRED/MID_BIAS */
		reg = le16_to_cpu(priv->eeprom->cr49);
		reg >>= 13;
		reg <<= 15;
		reg |= ant_power << 9;
		adm8211_rf_write_syn_rfmd2958(dev, 0x0A, reg);
		/* set TXRX TX_GAIN */
		adm8211_rf_write_syn_rfmd2958(dev, 0x09, 0x00050 |
			(priv->pdev->revision < ADM8211_REV_CA ? tx_power : 0));
	} else {
		reg = ADM8211_CSR_READ(PLCPHD);
		reg &= 0xff00ffff;
		reg |= tx_power << 18;
		ADM8211_CSR_WRITE(PLCPHD, reg);
	}

	ADM8211_CSR_WRITE(SYNRF, ADM8211_SYNRF_SELRF |
			  ADM8211_SYNRF_PE1 | ADM8211_SYNRF_PHYRST);
	ADM8211_CSR_READ(SYNRF);
	msleep(30);

	/* RF3000 BBP */
	if (priv->transceiver_type != ADM8211_RFMD2958)
		adm8211_write_bbp(dev, RF3000_TX_VAR_GAIN__TX_LEN_EXT,
				  tx_power<<2);
	adm8211_write_bbp(dev, RF3000_LOW_GAIN_CALIB, lpf_cutoff);
	adm8211_write_bbp(dev, RF3000_HIGH_GAIN_CALIB, lnags_thresh);
	adm8211_write_bbp(dev, 0x1c, priv->pdev->revision == ADM8211_REV_BA ?
				     priv->eeprom->cr28 : 0);
	adm8211_write_bbp(dev, 0x1d, priv->eeprom->cr29);

	ADM8211_CSR_WRITE(SYNRF, 0);

	/* Nothing to do for ADMtek BBP */
	} else if (priv->bbp_type != ADM8211_TYPE_ADMTEK)
		printk(KERN_DEBUG "%s: unsupported BBP type %d\n",
		       wiphy_name(dev->wiphy), priv->bbp_type);

	ADM8211_RESTORE();

	/* update current channel for adhoc (and maybe AP mode) */
	reg = ADM8211_CSR_READ(CAP0);
	reg &= ~0xF;
	reg |= chan;
	ADM8211_CSR_WRITE(CAP0, reg);

	return 0;
}

static void adm8211_update_mode(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;

	ADM8211_IDLE();

	priv->soft_rx_crc = 0;
	switch (priv->mode) {
	case NL80211_IFTYPE_STATION:
		priv->nar &= ~(ADM8211_NAR_PR | ADM8211_NAR_EA);
		priv->nar |= ADM8211_NAR_ST | ADM8211_NAR_SR;
		break;
	case NL80211_IFTYPE_ADHOC:
		priv->nar &= ~ADM8211_NAR_PR;
		priv->nar |= ADM8211_NAR_EA | ADM8211_NAR_ST | ADM8211_NAR_SR;

		/* don't trust the error bits on rev 0x20 and up in adhoc */
		if (priv->pdev->revision >= ADM8211_REV_BA)
			priv->soft_rx_crc = 1;
		break;
	case NL80211_IFTYPE_MONITOR:
		priv->nar &= ~(ADM8211_NAR_EA | ADM8211_NAR_ST);
		priv->nar |= ADM8211_NAR_PR | ADM8211_NAR_SR;
		break;
	}

	ADM8211_RESTORE();
}

static void adm8211_hw_init_syn(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;

	switch (priv->transceiver_type) {
	case ADM8211_RFMD2958:
	case ADM8211_RFMD2958_RF3000_CONTROL_POWER:
		/* comments taken from ADMtek vendor driver */

		/* Reset RF2958 after power on */
		adm8211_rf_write_syn_rfmd2958(dev, 0x1F, 0x00000);
		/* Initialize RF VCO Core Bias to maximum */
		adm8211_rf_write_syn_rfmd2958(dev, 0x0C, 0x3001F);
		/* Initialize IF PLL */
		adm8211_rf_write_syn_rfmd2958(dev, 0x01, 0x29C03);
		/* Initialize IF PLL Coarse Tuning */
		adm8211_rf_write_syn_rfmd2958(dev, 0x03, 0x1FF6F);
		/* Initialize RF PLL */
		adm8211_rf_write_syn_rfmd2958(dev, 0x04, 0x29403);
		/* Initialize RF PLL Coarse Tuning */
		adm8211_rf_write_syn_rfmd2958(dev, 0x07, 0x1456F);
		/* Initialize TX gain and filter BW (R9) */
		adm8211_rf_write_syn_rfmd2958(dev, 0x09,
			(priv->transceiver_type == ADM8211_RFMD2958 ?
			 0x10050 : 0x00050));
		/* Initialize CAL register */
		adm8211_rf_write_syn_rfmd2958(dev, 0x08, 0x3FFF8);
		break;

	case ADM8211_MAX2820:
		adm8211_rf_write_syn_max2820(dev, 0x1, 0x01E);
		adm8211_rf_write_syn_max2820(dev, 0x2, 0x001);
		adm8211_rf_write_syn_max2820(dev, 0x3, 0x054);
		adm8211_rf_write_syn_max2820(dev, 0x4, 0x310);
		adm8211_rf_write_syn_max2820(dev, 0x5, 0x000);
		break;

	case ADM8211_AL2210L:
		adm8211_rf_write_syn_al2210l(dev, 0x0, 0x0196C);
		adm8211_rf_write_syn_al2210l(dev, 0x1, 0x007CB);
		adm8211_rf_write_syn_al2210l(dev, 0x2, 0x3582F);
		adm8211_rf_write_syn_al2210l(dev, 0x3, 0x010A9);
		adm8211_rf_write_syn_al2210l(dev, 0x4, 0x77280);
		adm8211_rf_write_syn_al2210l(dev, 0x5, 0x45641);
		adm8211_rf_write_syn_al2210l(dev, 0x6, 0xEA130);
		adm8211_rf_write_syn_al2210l(dev, 0x7, 0x80000);
		adm8211_rf_write_syn_al2210l(dev, 0x8, 0x7850F);
		adm8211_rf_write_syn_al2210l(dev, 0x9, 0xF900C);
		adm8211_rf_write_syn_al2210l(dev, 0xA, 0x00000);
		adm8211_rf_write_syn_al2210l(dev, 0xB, 0x00000);
		break;

	case ADM8211_RFMD2948:
	default:
		break;
	}
}

static int adm8211_hw_init_bbp(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;
	u32 reg;

	/* write addresses */
	if (priv->bbp_type == ADM8211_TYPE_INTERSIL) {
		ADM8211_CSR_WRITE(MMIWA,  0x100E0C0A);
		ADM8211_CSR_WRITE(MMIRD0, 0x00007C7E);
		ADM8211_CSR_WRITE(MMIRD1, 0x00100000);
	} else if (priv->bbp_type == ADM8211_TYPE_RFMD ||
		   priv->bbp_type == ADM8211_TYPE_ADMTEK) {
		/* check specific BBP type */
		switch (priv->specific_bbptype) {
		case ADM8211_BBP_RFMD3000:
		case ADM8211_BBP_RFMD3002:
			ADM8211_CSR_WRITE(MMIWA,  0x00009101);
			ADM8211_CSR_WRITE(MMIRD0, 0x00000301);
			break;

		case ADM8211_BBP_ADM8011:
			ADM8211_CSR_WRITE(MMIWA,  0x00008903);
			ADM8211_CSR_WRITE(MMIRD0, 0x00001716);

			reg = ADM8211_CSR_READ(BBPCTL);
			reg &= ~ADM8211_BBPCTL_TYPE;
			reg |= 0x5 << 18;
			ADM8211_CSR_WRITE(BBPCTL, reg);
			break;
		}

		switch (priv->pdev->revision) {
		case ADM8211_REV_CA:
			if (priv->transceiver_type == ADM8211_RFMD2958 ||
			    priv->transceiver_type == ADM8211_RFMD2958_RF3000_CONTROL_POWER ||
			    priv->transceiver_type == ADM8211_RFMD2948)
				ADM8211_CSR_WRITE(SYNCTL, 0x1 << 22);
			else if (priv->transceiver_type == ADM8211_MAX2820 ||
				 priv->transceiver_type == ADM8211_AL2210L)
				ADM8211_CSR_WRITE(SYNCTL, 0x3 << 22);
			break;

		case ADM8211_REV_BA:
			reg  = ADM8211_CSR_READ(MMIRD1);
			reg &= 0x0000FFFF;
			reg |= 0x7e100000;
			ADM8211_CSR_WRITE(MMIRD1, reg);
			break;

		case ADM8211_REV_AB:
		case ADM8211_REV_AF:
		default:
			ADM8211_CSR_WRITE(MMIRD1, 0x7e100000);
			break;
		}

		/* For RFMD */
		ADM8211_CSR_WRITE(MACTEST, 0x800);
	}

	adm8211_hw_init_syn(dev);

	/* Set RF Power control IF pin to PE1+PHYRST# */
	ADM8211_CSR_WRITE(SYNRF, ADM8211_SYNRF_SELRF |
			  ADM8211_SYNRF_PE1 | ADM8211_SYNRF_PHYRST);
	ADM8211_CSR_READ(SYNRF);
	msleep(20);

	/* write BBP regs */
	if (priv->bbp_type == ADM8211_TYPE_RFMD) {
		/* RF3000 BBP */
		/* another set:
		 * 11: c8
		 * 14: 14
		 * 15: 50 (chan 1..13; chan 14: d0)
		 * 1c: 00
		 * 1d: 84
		 */
		adm8211_write_bbp(dev, RF3000_CCA_CTRL, 0x80);
		/* antenna selection: diversity */
		adm8211_write_bbp(dev, RF3000_DIVERSITY__RSSI, 0x80);
		adm8211_write_bbp(dev, RF3000_TX_VAR_GAIN__TX_LEN_EXT, 0x74);
		adm8211_write_bbp(dev, RF3000_LOW_GAIN_CALIB, 0x38);
		adm8211_write_bbp(dev, RF3000_HIGH_GAIN_CALIB, 0x40);

		if (priv->eeprom->major_version < 2) {
			adm8211_write_bbp(dev, 0x1c, 0x00);
			adm8211_write_bbp(dev, 0x1d, 0x80);
		} else {
			if (priv->pdev->revision == ADM8211_REV_BA)
				adm8211_write_bbp(dev, 0x1c, priv->eeprom->cr28);
			else
				adm8211_write_bbp(dev, 0x1c, 0x00);

			adm8211_write_bbp(dev, 0x1d, priv->eeprom->cr29);
		}
	} else if (priv->bbp_type == ADM8211_TYPE_ADMTEK) {
		/* reset baseband */
		adm8211_write_bbp(dev, 0x00, 0xFF);
		/* antenna selection: diversity */
		adm8211_write_bbp(dev, 0x07, 0x0A);

		/* TODO: find documentation for this */
		switch (priv->transceiver_type) {
		case ADM8211_RFMD2958:
		case ADM8211_RFMD2958_RF3000_CONTROL_POWER:
			adm8211_write_bbp(dev, 0x00, 0x00);
			adm8211_write_bbp(dev, 0x01, 0x00);
			adm8211_write_bbp(dev, 0x02, 0x00);
			adm8211_write_bbp(dev, 0x03, 0x00);
			adm8211_write_bbp(dev, 0x06, 0x0f);
			adm8211_write_bbp(dev, 0x09, 0x00);
			adm8211_write_bbp(dev, 0x0a, 0x00);
			adm8211_write_bbp(dev, 0x0b, 0x00);
			adm8211_write_bbp(dev, 0x0c, 0x00);
			adm8211_write_bbp(dev, 0x0f, 0xAA);
			adm8211_write_bbp(dev, 0x10, 0x8c);
			adm8211_write_bbp(dev, 0x11, 0x43);
			adm8211_write_bbp(dev, 0x18, 0x40);
			adm8211_write_bbp(dev, 0x20, 0x23);
			adm8211_write_bbp(dev, 0x21, 0x02);
			adm8211_write_bbp(dev, 0x22, 0x28);
			adm8211_write_bbp(dev, 0x23, 0x30);
			adm8211_write_bbp(dev, 0x24, 0x2d);
			adm8211_write_bbp(dev, 0x28, 0x35);
			adm8211_write_bbp(dev, 0x2a, 0x8c);
			adm8211_write_bbp(dev, 0x2b, 0x81);
			adm8211_write_bbp(dev, 0x2c, 0x44);
			adm8211_write_bbp(dev, 0x2d, 0x0A);
			adm8211_write_bbp(dev, 0x29, 0x40);
			adm8211_write_bbp(dev, 0x60, 0x08);
			adm8211_write_bbp(dev, 0x64, 0x01);
			break;

		case ADM8211_MAX2820:
			adm8211_write_bbp(dev, 0x00, 0x00);
			adm8211_write_bbp(dev, 0x01, 0x00);
			adm8211_write_bbp(dev, 0x02, 0x00);
			adm8211_write_bbp(dev, 0x03, 0x00);
			adm8211_write_bbp(dev, 0x06, 0x0f);
			adm8211_write_bbp(dev, 0x09, 0x05);
			adm8211_write_bbp(dev, 0x0a, 0x02);
			adm8211_write_bbp(dev, 0x0b, 0x00);
			adm8211_write_bbp(dev, 0x0c, 0x0f);
			adm8211_write_bbp(dev, 0x0f, 0x55);
			adm8211_write_bbp(dev, 0x10, 0x8d);
			adm8211_write_bbp(dev, 0x11, 0x43);
			adm8211_write_bbp(dev, 0x18, 0x4a);
			adm8211_write_bbp(dev, 0x20, 0x20);
			adm8211_write_bbp(dev, 0x21, 0x02);
			adm8211_write_bbp(dev, 0x22, 0x23);
			adm8211_write_bbp(dev, 0x23, 0x30);
			adm8211_write_bbp(dev, 0x24, 0x2d);
			adm8211_write_bbp(dev, 0x2a, 0x8c);
			adm8211_write_bbp(dev, 0x2b, 0x81);
			adm8211_write_bbp(dev, 0x2c, 0x44);
			adm8211_write_bbp(dev, 0x29, 0x4a);
			adm8211_write_bbp(dev, 0x60, 0x2b);
			adm8211_write_bbp(dev, 0x64, 0x01);
			break;

		case ADM8211_AL2210L:
			adm8211_write_bbp(dev, 0x00, 0x00);
			adm8211_write_bbp(dev, 0x01, 0x00);
			adm8211_write_bbp(dev, 0x02, 0x00);
			adm8211_write_bbp(dev, 0x03, 0x00);
			adm8211_write_bbp(dev, 0x06, 0x0f);
			adm8211_write_bbp(dev, 0x07, 0x05);
			adm8211_write_bbp(dev, 0x08, 0x03);
			adm8211_write_bbp(dev, 0x09, 0x00);
			adm8211_write_bbp(dev, 0x0a, 0x00);
			adm8211_write_bbp(dev, 0x0b, 0x00);
			adm8211_write_bbp(dev, 0x0c, 0x10);
			adm8211_write_bbp(dev, 0x0f, 0x55);
			adm8211_write_bbp(dev, 0x10, 0x8d);
			adm8211_write_bbp(dev, 0x11, 0x43);
			adm8211_write_bbp(dev, 0x18, 0x4a);
			adm8211_write_bbp(dev, 0x20, 0x20);
			adm8211_write_bbp(dev, 0x21, 0x02);
			adm8211_write_bbp(dev, 0x22, 0x23);
			adm8211_write_bbp(dev, 0x23, 0x30);
			adm8211_write_bbp(dev, 0x24, 0x2d);
			adm8211_write_bbp(dev, 0x2a, 0xaa);
			adm8211_write_bbp(dev, 0x2b, 0x81);
			adm8211_write_bbp(dev, 0x2c, 0x44);
			adm8211_write_bbp(dev, 0x29, 0xfa);
			adm8211_write_bbp(dev, 0x60, 0x2d);
			adm8211_write_bbp(dev, 0x64, 0x01);
			break;

		case ADM8211_RFMD2948:
			break;

		default:
			printk(KERN_DEBUG "%s: unsupported transceiver %d\n",
			       wiphy_name(dev->wiphy), priv->transceiver_type);
			break;
		}
	} else
		printk(KERN_DEBUG "%s: unsupported BBP %d\n",
		       wiphy_name(dev->wiphy), priv->bbp_type);

	ADM8211_CSR_WRITE(SYNRF, 0);

	/* Set RF CAL control source to MAC control */
	reg = ADM8211_CSR_READ(SYNCTL);
	reg |= ADM8211_SYNCTL_SELCAL;
	ADM8211_CSR_WRITE(SYNCTL, reg);

	return 0;
}

/* configures hw beacons/probe responses */
static int adm8211_set_rate(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;
	u32 reg;
	int i = 0;
	u8 rate_buf[12] = {0};

	/* write supported rates */
	if (priv->pdev->revision != ADM8211_REV_BA) {
		rate_buf[0] = ARRAY_SIZE(adm8211_rates);
		for (i = 0; i < ARRAY_SIZE(adm8211_rates); i++)
			rate_buf[i + 1] = (adm8211_rates[i].bitrate / 5) | 0x80;
	} else {
		/* workaround for rev BA specific bug */
		rate_buf[0] = 0x04;
		rate_buf[1] = 0x82;
		rate_buf[2] = 0x04;
		rate_buf[3] = 0x0b;
		rate_buf[4] = 0x16;
	}

	adm8211_write_sram_bytes(dev, ADM8211_SRAM_SUPP_RATE, rate_buf,
				 ARRAY_SIZE(adm8211_rates) + 1);

	reg = ADM8211_CSR_READ(PLCPHD) & 0x00FFFFFF; /* keep bits 0-23 */
	reg |= 1 << 15;	/* short preamble */
	reg |= 110 << 24;
	ADM8211_CSR_WRITE(PLCPHD, reg);

	/* MTMLT   = 512 TU (max TX MSDU lifetime)
	 * BCNTSIG = plcp_signal (beacon, probe resp, and atim TX rate)
	 * SRTYLIM = 224 (short retry limit, TX header value is default) */
	ADM8211_CSR_WRITE(TXLMT, (512 << 16) | (110 << 8) | (224 << 0));

	return 0;
}

static void adm8211_hw_init(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;
	u32 reg;
	u8 cline;

	reg = ADM8211_CSR_READ(PAR);
	reg |= ADM8211_PAR_MRLE | ADM8211_PAR_MRME;
	reg &= ~(ADM8211_PAR_BAR | ADM8211_PAR_CAL);

	if (!pci_set_mwi(priv->pdev)) {
		reg |= 0x1 << 24;
		pci_read_config_byte(priv->pdev, PCI_CACHE_LINE_SIZE, &cline);

		switch (cline) {
		case  0x8: reg |= (0x1 << 14);
			   break;
		case 0x16: reg |= (0x2 << 14);
			   break;
		case 0x32: reg |= (0x3 << 14);
			   break;
		  default: reg |= (0x0 << 14);
			   break;
		}
	}

	ADM8211_CSR_WRITE(PAR, reg);

	reg = ADM8211_CSR_READ(CSR_TEST1);
	reg &= ~(0xF << 28);
	reg |= (1 << 28) | (1 << 31);
	ADM8211_CSR_WRITE(CSR_TEST1, reg);

	/* lose link after 4 lost beacons */
	reg = (0x04 << 21) | ADM8211_WCSR_TSFTWE | ADM8211_WCSR_LSOE;
	ADM8211_CSR_WRITE(WCSR, reg);

	/* Disable APM, enable receive FIFO threshold, and set drain receive
	 * threshold to store-and-forward */
	reg = ADM8211_CSR_READ(CMDR);
	reg &= ~(ADM8211_CMDR_APM | ADM8211_CMDR_DRT);
	reg |= ADM8211_CMDR_RTE | ADM8211_CMDR_DRT_SF;
	ADM8211_CSR_WRITE(CMDR, reg);

	adm8211_set_rate(dev);

	/* 4-bit values:
	 * PWR1UP   = 8 * 2 ms
	 * PWR0PAPE = 8 us or 5 us
	 * PWR1PAPE = 1 us or 3 us
	 * PWR0TRSW = 5 us
	 * PWR1TRSW = 12 us
	 * PWR0PE2  = 13 us
	 * PWR1PE2  = 1 us
	 * PWR0TXPE = 8 or 6 */
	if (priv->pdev->revision < ADM8211_REV_CA)
		ADM8211_CSR_WRITE(TOFS2, 0x8815cd18);
	else
		ADM8211_CSR_WRITE(TOFS2, 0x8535cd16);

	/* Enable store and forward for transmit */
	priv->nar = ADM8211_NAR_SF | ADM8211_NAR_PB;
	ADM8211_CSR_WRITE(NAR, priv->nar);

	/* Reset RF */
	ADM8211_CSR_WRITE(SYNRF, ADM8211_SYNRF_RADIO);
	ADM8211_CSR_READ(SYNRF);
	msleep(10);
	ADM8211_CSR_WRITE(SYNRF, 0);
	ADM8211_CSR_READ(SYNRF);
	msleep(5);

	/* Set CFP Max Duration to 0x10 TU */
	reg = ADM8211_CSR_READ(CFPP);
	reg &= ~(0xffff << 8);
	reg |= 0x0010 << 8;
	ADM8211_CSR_WRITE(CFPP, reg);

	/* USCNT = 0x16 (number of system clocks, 22 MHz, in 1us
	 * TUCNT = 0x3ff - Tu counter 1024 us  */
	ADM8211_CSR_WRITE(TOFS0, (0x16 << 24) | 0x3ff);

	/* SLOT=20 us, SIFS=110 cycles of 22 MHz (5 us),
	 * DIFS=50 us, EIFS=100 us */
	if (priv->pdev->revision < ADM8211_REV_CA)
		ADM8211_CSR_WRITE(IFST, (20 << 23) | (110 << 15) |
					(50 << 9)  | 100);
	else
		ADM8211_CSR_WRITE(IFST, (20 << 23) | (24 << 15) |
					(50 << 9)  | 100);

	/* PCNT = 1 (MAC idle time awake/sleep, unit S)
	 * RMRD = 2346 * 8 + 1 us (max RX duration)  */
	ADM8211_CSR_WRITE(RMD, (1 << 16) | 18769);

	/* MART=65535 us, MIRT=256 us, TSFTOFST=0 us */
	ADM8211_CSR_WRITE(RSPT, 0xffffff00);

	/* Initialize BBP (and SYN) */
	adm8211_hw_init_bbp(dev);

	/* make sure interrupts are off */
	ADM8211_CSR_WRITE(IER, 0);

	/* ACK interrupts */
	ADM8211_CSR_WRITE(STSR, ADM8211_CSR_READ(STSR));

	/* Setup WEP (turns it off for now) */
	reg = ADM8211_CSR_READ(MACTEST);
	reg &= ~(7 << 20);
	ADM8211_CSR_WRITE(MACTEST, reg);

	reg = ADM8211_CSR_READ(WEPCTL);
	reg &= ~ADM8211_WEPCTL_WEPENABLE;
	reg |= ADM8211_WEPCTL_WEPRXBYP;
	ADM8211_CSR_WRITE(WEPCTL, reg);

	/* Clear the missed-packet counter. */
	ADM8211_CSR_READ(LPC);
}

static int adm8211_hw_reset(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;
	u32 reg, tmp;
	int timeout = 100;

	/* Power-on issue */
	/* TODO: check if this is necessary */
	ADM8211_CSR_WRITE(FRCTL, 0);

	/* Reset the chip */
	tmp = ADM8211_CSR_READ(PAR);
	ADM8211_CSR_WRITE(PAR, ADM8211_PAR_SWR);

	while ((ADM8211_CSR_READ(PAR) & ADM8211_PAR_SWR) && timeout--)
		msleep(50);

	if (timeout <= 0)
		return -ETIMEDOUT;

	ADM8211_CSR_WRITE(PAR, tmp);

	if (priv->pdev->revision == ADM8211_REV_BA &&
	    (priv->transceiver_type == ADM8211_RFMD2958_RF3000_CONTROL_POWER ||
	     priv->transceiver_type == ADM8211_RFMD2958)) {
		reg = ADM8211_CSR_READ(CSR_TEST1);
		reg |= (1 << 4) | (1 << 5);
		ADM8211_CSR_WRITE(CSR_TEST1, reg);
	} else if (priv->pdev->revision == ADM8211_REV_CA) {
		reg = ADM8211_CSR_READ(CSR_TEST1);
		reg &= ~((1 << 4) | (1 << 5));
		ADM8211_CSR_WRITE(CSR_TEST1, reg);
	}

	ADM8211_CSR_WRITE(FRCTL, 0);

	reg = ADM8211_CSR_READ(CSR_TEST0);
	reg |= ADM8211_CSR_TEST0_EPRLD;	/* EEPROM Recall */
	ADM8211_CSR_WRITE(CSR_TEST0, reg);

	adm8211_clear_sram(dev);

	return 0;
}

static u64 adm8211_get_tsft(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;
	u32 tsftl;
	u64 tsft;

	tsftl = ADM8211_CSR_READ(TSFTL);
	tsft = ADM8211_CSR_READ(TSFTH);
	tsft <<= 32;
	tsft |= tsftl;

	return tsft;
}

static void adm8211_set_interval(struct ieee80211_hw *dev,
				 unsigned short bi, unsigned short li)
{
	struct adm8211_priv *priv = dev->priv;
	u32 reg;

	/* BP (beacon interval) = data->beacon_interval
	 * LI (listen interval) = data->listen_interval (in beacon intervals) */
	reg = (bi << 16) | li;
	ADM8211_CSR_WRITE(BPLI, reg);
}

static void adm8211_set_bssid(struct ieee80211_hw *dev, const u8 *bssid)
{
	struct adm8211_priv *priv = dev->priv;
	u32 reg;

	ADM8211_CSR_WRITE(BSSID0, le32_to_cpu(*(__le32 *)bssid));
	reg = ADM8211_CSR_READ(ABDA1);
	reg &= 0x0000ffff;
	reg |= (bssid[4] << 16) | (bssid[5] << 24);
	ADM8211_CSR_WRITE(ABDA1, reg);
}

static int adm8211_config(struct ieee80211_hw *dev, u32 changed)
{
	struct adm8211_priv *priv = dev->priv;
	struct ieee80211_conf *conf = &dev->conf;
	int channel = ieee80211_frequency_to_channel(conf->channel->center_freq);

	if (channel != priv->channel) {
		priv->channel = channel;
		adm8211_rf_set_channel(dev, priv->channel);
	}

	return 0;
}

static void adm8211_bss_info_changed(struct ieee80211_hw *dev,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *conf,
				     u32 changes)
{
	struct adm8211_priv *priv = dev->priv;

	if (!(changes & BSS_CHANGED_BSSID))
		return;

	if (memcmp(conf->bssid, priv->bssid, ETH_ALEN)) {
		adm8211_set_bssid(dev, conf->bssid);
		memcpy(priv->bssid, conf->bssid, ETH_ALEN);
	}
}

static u64 adm8211_prepare_multicast(struct ieee80211_hw *hw,
				     int mc_count, struct dev_addr_list *mclist)
{
	unsigned int bit_nr, i;
	u32 mc_filter[2];

	mc_filter[1] = mc_filter[0] = 0;

	for (i = 0; i < mc_count; i++) {
		if (!mclist)
			break;
		bit_nr = ether_crc(ETH_ALEN, mclist->dmi_addr) >> 26;

		bit_nr &= 0x3F;
		mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
		mclist = mclist->next;
	}

	return mc_filter[0] | ((u64)(mc_filter[1]) << 32);
}

static void adm8211_configure_filter(struct ieee80211_hw *dev,
				     unsigned int changed_flags,
				     unsigned int *total_flags,
				     u64 multicast)
{
	static const u8 bcast[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	struct adm8211_priv *priv = dev->priv;
	unsigned int new_flags;
	u32 mc_filter[2];

	mc_filter[0] = multicast;
	mc_filter[1] = multicast >> 32;

	new_flags = 0;

	if (*total_flags & FIF_PROMISC_IN_BSS) {
		new_flags |= FIF_PROMISC_IN_BSS;
		priv->nar |= ADM8211_NAR_PR;
		priv->nar &= ~ADM8211_NAR_MM;
		mc_filter[1] = mc_filter[0] = ~0;
	} else if (*total_flags & FIF_ALLMULTI || multicast == ~(0ULL)) {
		new_flags |= FIF_ALLMULTI;
		priv->nar &= ~ADM8211_NAR_PR;
		priv->nar |= ADM8211_NAR_MM;
		mc_filter[1] = mc_filter[0] = ~0;
	} else {
		priv->nar &= ~(ADM8211_NAR_MM | ADM8211_NAR_PR);
	}

	ADM8211_IDLE_RX();

	ADM8211_CSR_WRITE(MAR0, mc_filter[0]);
	ADM8211_CSR_WRITE(MAR1, mc_filter[1]);
	ADM8211_CSR_READ(NAR);

	if (priv->nar & ADM8211_NAR_PR)
		dev->flags |= IEEE80211_HW_RX_INCLUDES_FCS;
	else
		dev->flags &= ~IEEE80211_HW_RX_INCLUDES_FCS;

	if (*total_flags & FIF_BCN_PRBRESP_PROMISC)
		adm8211_set_bssid(dev, bcast);
	else
		adm8211_set_bssid(dev, priv->bssid);

	ADM8211_RESTORE();

	*total_flags = new_flags;
}

static int adm8211_add_interface(struct ieee80211_hw *dev,
				 struct ieee80211_vif *vif)
{
	struct adm8211_priv *priv = dev->priv;
	if (priv->mode != NL80211_IFTYPE_MONITOR)
		return -EOPNOTSUPP;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		priv->mode = vif->type;
		break;
	default:
		return -EOPNOTSUPP;
	}

	ADM8211_IDLE();

	ADM8211_CSR_WRITE(PAR0, le32_to_cpu(*(__le32 *)vif->addr));
	ADM8211_CSR_WRITE(PAR1, le16_to_cpu(*(__le16 *)(vif->addr + 4)));

	adm8211_update_mode(dev);

	ADM8211_RESTORE();

	return 0;
}

static void adm8211_remove_interface(struct ieee80211_hw *dev,
				     struct ieee80211_vif *vif)
{
	struct adm8211_priv *priv = dev->priv;
	priv->mode = NL80211_IFTYPE_MONITOR;
}

static int adm8211_init_rings(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;
	struct adm8211_desc *desc = NULL;
	struct adm8211_rx_ring_info *rx_info;
	struct adm8211_tx_ring_info *tx_info;
	unsigned int i;

	for (i = 0; i < priv->rx_ring_size; i++) {
		desc = &priv->rx_ring[i];
		desc->status = 0;
		desc->length = cpu_to_le32(RX_PKT_SIZE);
		priv->rx_buffers[i].skb = NULL;
	}
	/* Mark the end of RX ring; hw returns to base address after this
	 * descriptor */
	desc->length |= cpu_to_le32(RDES1_CONTROL_RER);

	for (i = 0; i < priv->rx_ring_size; i++) {
		desc = &priv->rx_ring[i];
		rx_info = &priv->rx_buffers[i];

		rx_info->skb = dev_alloc_skb(RX_PKT_SIZE);
		if (rx_info->skb == NULL)
			break;
		rx_info->mapping = pci_map_single(priv->pdev,
						  skb_tail_pointer(rx_info->skb),
						  RX_PKT_SIZE,
						  PCI_DMA_FROMDEVICE);
		desc->buffer1 = cpu_to_le32(rx_info->mapping);
		desc->status = cpu_to_le32(RDES0_STATUS_OWN | RDES0_STATUS_SQL);
	}

	/* Setup TX ring. TX buffers descriptors will be filled in as needed */
	for (i = 0; i < priv->tx_ring_size; i++) {
		desc = &priv->tx_ring[i];
		tx_info = &priv->tx_buffers[i];

		tx_info->skb = NULL;
		tx_info->mapping = 0;
		desc->status = 0;
	}
	desc->length = cpu_to_le32(TDES1_CONTROL_TER);

	priv->cur_rx = priv->cur_tx = priv->dirty_tx = 0;
	ADM8211_CSR_WRITE(RDB, priv->rx_ring_dma);
	ADM8211_CSR_WRITE(TDBD, priv->tx_ring_dma);

	return 0;
}

static void adm8211_free_rings(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;
	unsigned int i;

	for (i = 0; i < priv->rx_ring_size; i++) {
		if (!priv->rx_buffers[i].skb)
			continue;

		pci_unmap_single(
			priv->pdev,
			priv->rx_buffers[i].mapping,
			RX_PKT_SIZE, PCI_DMA_FROMDEVICE);

		dev_kfree_skb(priv->rx_buffers[i].skb);
	}

	for (i = 0; i < priv->tx_ring_size; i++) {
		if (!priv->tx_buffers[i].skb)
			continue;

		pci_unmap_single(priv->pdev,
				 priv->tx_buffers[i].mapping,
				 priv->tx_buffers[i].skb->len,
				 PCI_DMA_TODEVICE);

		dev_kfree_skb(priv->tx_buffers[i].skb);
	}
}

static int adm8211_start(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;
	int retval;

	/* Power up MAC and RF chips */
	retval = adm8211_hw_reset(dev);
	if (retval) {
		printk(KERN_ERR "%s: hardware reset failed\n",
		       wiphy_name(dev->wiphy));
		goto fail;
	}

	retval = adm8211_init_rings(dev);
	if (retval) {
		printk(KERN_ERR "%s: failed to initialize rings\n",
		       wiphy_name(dev->wiphy));
		goto fail;
	}

	/* Init hardware */
	adm8211_hw_init(dev);
	adm8211_rf_set_channel(dev, priv->channel);

	retval = request_irq(priv->pdev->irq, adm8211_interrupt,
			     IRQF_SHARED, "adm8211", dev);
	if (retval) {
		printk(KERN_ERR "%s: failed to register IRQ handler\n",
		       wiphy_name(dev->wiphy));
		goto fail;
	}

	ADM8211_CSR_WRITE(IER, ADM8211_IER_NIE | ADM8211_IER_AIE |
			       ADM8211_IER_RCIE | ADM8211_IER_TCIE |
			       ADM8211_IER_TDUIE | ADM8211_IER_GPTIE);
	priv->mode = NL80211_IFTYPE_MONITOR;
	adm8211_update_mode(dev);
	ADM8211_CSR_WRITE(RDR, 0);

	adm8211_set_interval(dev, 100, 10);
	return 0;

fail:
	return retval;
}

static void adm8211_stop(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;

	priv->mode = NL80211_IFTYPE_UNSPECIFIED;
	priv->nar = 0;
	ADM8211_CSR_WRITE(NAR, 0);
	ADM8211_CSR_WRITE(IER, 0);
	ADM8211_CSR_READ(NAR);

	free_irq(priv->pdev->irq, dev);

	adm8211_free_rings(dev);
}

static void adm8211_calc_durations(int *dur, int *plcp, size_t payload_len, int len,
				   int plcp_signal, int short_preamble)
{
	/* Alternative calculation from NetBSD: */

/* IEEE 802.11b durations for DSSS PHY in microseconds */
#define IEEE80211_DUR_DS_LONG_PREAMBLE	144
#define IEEE80211_DUR_DS_SHORT_PREAMBLE	72
#define IEEE80211_DUR_DS_FAST_PLCPHDR	24
#define IEEE80211_DUR_DS_SLOW_PLCPHDR	48
#define IEEE80211_DUR_DS_SLOW_ACK	112
#define IEEE80211_DUR_DS_FAST_ACK	56
#define IEEE80211_DUR_DS_SLOW_CTS	112
#define IEEE80211_DUR_DS_FAST_CTS	56
#define IEEE80211_DUR_DS_SLOT		20
#define IEEE80211_DUR_DS_SIFS		10

	int remainder;

	*dur = (80 * (24 + payload_len) + plcp_signal - 1)
		/ plcp_signal;

	if (plcp_signal <= PLCP_SIGNAL_2M)
		/* 1-2Mbps WLAN: send ACK/CTS at 1Mbps */
		*dur += 3 * (IEEE80211_DUR_DS_SIFS +
			     IEEE80211_DUR_DS_SHORT_PREAMBLE +
			     IEEE80211_DUR_DS_FAST_PLCPHDR) +
			     IEEE80211_DUR_DS_SLOW_CTS + IEEE80211_DUR_DS_SLOW_ACK;
	else
		/* 5-11Mbps WLAN: send ACK/CTS at 2Mbps */
		*dur += 3 * (IEEE80211_DUR_DS_SIFS +
			     IEEE80211_DUR_DS_SHORT_PREAMBLE +
			     IEEE80211_DUR_DS_FAST_PLCPHDR) +
			     IEEE80211_DUR_DS_FAST_CTS + IEEE80211_DUR_DS_FAST_ACK;

	/* lengthen duration if long preamble */
	if (!short_preamble)
		*dur +=	3 * (IEEE80211_DUR_DS_LONG_PREAMBLE -
			     IEEE80211_DUR_DS_SHORT_PREAMBLE) +
			3 * (IEEE80211_DUR_DS_SLOW_PLCPHDR -
			     IEEE80211_DUR_DS_FAST_PLCPHDR);


	*plcp = (80 * len) / plcp_signal;
	remainder = (80 * len) % plcp_signal;
	if (plcp_signal == PLCP_SIGNAL_11M &&
	    remainder <= 30 && remainder > 0)
		*plcp = (*plcp | 0x8000) + 1;
	else if (remainder)
		(*plcp)++;
}

/* Transmit skb w/adm8211_tx_hdr (802.11 header created by hardware) */
static void adm8211_tx_raw(struct ieee80211_hw *dev, struct sk_buff *skb,
			   u16 plcp_signal,
			   size_t hdrlen)
{
	struct adm8211_priv *priv = dev->priv;
	unsigned long flags;
	dma_addr_t mapping;
	unsigned int entry;
	u32 flag;

	mapping = pci_map_single(priv->pdev, skb->data, skb->len,
				 PCI_DMA_TODEVICE);

	spin_lock_irqsave(&priv->lock, flags);

	if (priv->cur_tx - priv->dirty_tx == priv->tx_ring_size / 2)
		flag = TDES1_CONTROL_IC | TDES1_CONTROL_LS | TDES1_CONTROL_FS;
	else
		flag = TDES1_CONTROL_LS | TDES1_CONTROL_FS;

	if (priv->cur_tx - priv->dirty_tx == priv->tx_ring_size - 2)
		ieee80211_stop_queue(dev, 0);

	entry = priv->cur_tx % priv->tx_ring_size;

	priv->tx_buffers[entry].skb = skb;
	priv->tx_buffers[entry].mapping = mapping;
	priv->tx_buffers[entry].hdrlen = hdrlen;
	priv->tx_ring[entry].buffer1 = cpu_to_le32(mapping);

	if (entry == priv->tx_ring_size - 1)
		flag |= TDES1_CONTROL_TER;
	priv->tx_ring[entry].length = cpu_to_le32(flag | skb->len);

	/* Set TX rate (SIGNAL field in PLCP PPDU format) */
	flag = TDES0_CONTROL_OWN | (plcp_signal << 20) | 8 /* ? */;
	priv->tx_ring[entry].status = cpu_to_le32(flag);

	priv->cur_tx++;

	spin_unlock_irqrestore(&priv->lock, flags);

	/* Trigger transmit poll */
	ADM8211_CSR_WRITE(TDR, 0);
}

/* Put adm8211_tx_hdr on skb and transmit */
static int adm8211_tx(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct adm8211_tx_hdr *txhdr;
	size_t payload_len, hdrlen;
	int plcp, dur, len, plcp_signal, short_preamble;
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_rate *txrate = ieee80211_get_tx_rate(dev, info);
	u8 rc_flags;

	rc_flags = info->control.rates[0].flags;
	short_preamble = !!(rc_flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE);
	plcp_signal = txrate->bitrate;

	hdr = (struct ieee80211_hdr *)skb->data;
	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	memcpy(skb->cb, skb->data, hdrlen);
	hdr = (struct ieee80211_hdr *)skb->cb;
	skb_pull(skb, hdrlen);
	payload_len = skb->len;

	txhdr = (struct adm8211_tx_hdr *) skb_push(skb, sizeof(*txhdr));
	memset(txhdr, 0, sizeof(*txhdr));
	memcpy(txhdr->da, ieee80211_get_DA(hdr), ETH_ALEN);
	txhdr->signal = plcp_signal;
	txhdr->frame_body_size = cpu_to_le16(payload_len);
	txhdr->frame_control = hdr->frame_control;

	len = hdrlen + payload_len + FCS_LEN;

	txhdr->frag = cpu_to_le16(0x0FFF);
	adm8211_calc_durations(&dur, &plcp, payload_len,
			       len, plcp_signal, short_preamble);
	txhdr->plcp_frag_head_len = cpu_to_le16(plcp);
	txhdr->plcp_frag_tail_len = cpu_to_le16(plcp);
	txhdr->dur_frag_head = cpu_to_le16(dur);
	txhdr->dur_frag_tail = cpu_to_le16(dur);

	txhdr->header_control = cpu_to_le16(ADM8211_TXHDRCTL_ENABLE_EXTEND_HEADER);

	if (short_preamble)
		txhdr->header_control |= cpu_to_le16(ADM8211_TXHDRCTL_SHORT_PREAMBLE);

	if (rc_flags & IEEE80211_TX_RC_USE_RTS_CTS)
		txhdr->header_control |= cpu_to_le16(ADM8211_TXHDRCTL_ENABLE_RTS);

	txhdr->retry_limit = info->control.rates[0].count;

	adm8211_tx_raw(dev, skb, plcp_signal, hdrlen);

	return NETDEV_TX_OK;
}

static int adm8211_alloc_rings(struct ieee80211_hw *dev)
{
	struct adm8211_priv *priv = dev->priv;
	unsigned int ring_size;

	priv->rx_buffers = kmalloc(sizeof(*priv->rx_buffers) * priv->rx_ring_size +
				   sizeof(*priv->tx_buffers) * priv->tx_ring_size, GFP_KERNEL);
	if (!priv->rx_buffers)
		return -ENOMEM;

	priv->tx_buffers = (void *)priv->rx_buffers +
			   sizeof(*priv->rx_buffers) * priv->rx_ring_size;

	/* Allocate TX/RX descriptors */
	ring_size = sizeof(struct adm8211_desc) * priv->rx_ring_size +
		    sizeof(struct adm8211_desc) * priv->tx_ring_size;
	priv->rx_ring = pci_alloc_consistent(priv->pdev, ring_size,
					     &priv->rx_ring_dma);

	if (!priv->rx_ring) {
		kfree(priv->rx_buffers);
		priv->rx_buffers = NULL;
		priv->tx_buffers = NULL;
		return -ENOMEM;
	}

	priv->tx_ring = (struct adm8211_desc *)(priv->rx_ring +
						priv->rx_ring_size);
	priv->tx_ring_dma = priv->rx_ring_dma +
			    sizeof(struct adm8211_desc) * priv->rx_ring_size;

	return 0;
}

static const struct ieee80211_ops adm8211_ops = {
	.tx			= adm8211_tx,
	.start			= adm8211_start,
	.stop			= adm8211_stop,
	.add_interface		= adm8211_add_interface,
	.remove_interface	= adm8211_remove_interface,
	.config			= adm8211_config,
	.bss_info_changed	= adm8211_bss_info_changed,
	.prepare_multicast	= adm8211_prepare_multicast,
	.configure_filter	= adm8211_configure_filter,
	.get_stats		= adm8211_get_stats,
	.get_tsf		= adm8211_get_tsft
};

static int __devinit adm8211_probe(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	struct ieee80211_hw *dev;
	struct adm8211_priv *priv;
	unsigned long mem_addr, mem_len;
	unsigned int io_addr, io_len;
	int err;
	u32 reg;
	u8 perm_addr[ETH_ALEN];

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "%s (adm8211): Cannot enable new PCI device\n",
		       pci_name(pdev));
		return err;
	}

	io_addr = pci_resource_start(pdev, 0);
	io_len = pci_resource_len(pdev, 0);
	mem_addr = pci_resource_start(pdev, 1);
	mem_len = pci_resource_len(pdev, 1);
	if (io_len < 256 || mem_len < 1024) {
		printk(KERN_ERR "%s (adm8211): Too short PCI resources\n",
		       pci_name(pdev));
		goto err_disable_pdev;
	}


	/* check signature */
	pci_read_config_dword(pdev, 0x80 /* CR32 */, &reg);
	if (reg != ADM8211_SIG1 && reg != ADM8211_SIG2) {
		printk(KERN_ERR "%s (adm8211): Invalid signature (0x%x)\n",
		       pci_name(pdev), reg);
		goto err_disable_pdev;
	}

	err = pci_request_regions(pdev, "adm8211");
	if (err) {
		printk(KERN_ERR "%s (adm8211): Cannot obtain PCI resources\n",
		       pci_name(pdev));
		return err; /* someone else grabbed it? don't disable it */
	}

	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(32)) ||
	    pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32))) {
		printk(KERN_ERR "%s (adm8211): No suitable DMA available\n",
		       pci_name(pdev));
		goto err_free_reg;
	}

	pci_set_master(pdev);

	dev = ieee80211_alloc_hw(sizeof(*priv), &adm8211_ops);
	if (!dev) {
		printk(KERN_ERR "%s (adm8211): ieee80211 alloc failed\n",
		       pci_name(pdev));
		err = -ENOMEM;
		goto err_free_reg;
	}
	priv = dev->priv;
	priv->pdev = pdev;

	spin_lock_init(&priv->lock);

	SET_IEEE80211_DEV(dev, &pdev->dev);

	pci_set_drvdata(pdev, dev);

	priv->map = pci_iomap(pdev, 1, mem_len);
	if (!priv->map)
		priv->map = pci_iomap(pdev, 0, io_len);

	if (!priv->map) {
		printk(KERN_ERR "%s (adm8211): Cannot map device memory\n",
		       pci_name(pdev));
		goto err_free_dev;
	}

	priv->rx_ring_size = rx_ring_size;
	priv->tx_ring_size = tx_ring_size;

	if (adm8211_alloc_rings(dev)) {
		printk(KERN_ERR "%s (adm8211): Cannot allocate TX/RX ring\n",
		       pci_name(pdev));
		goto err_iounmap;
	}

	*(__le32 *)perm_addr = cpu_to_le32(ADM8211_CSR_READ(PAR0));
	*(__le16 *)&perm_addr[4] =
		cpu_to_le16(ADM8211_CSR_READ(PAR1) & 0xFFFF);

	if (!is_valid_ether_addr(perm_addr)) {
		printk(KERN_WARNING "%s (adm8211): Invalid hwaddr in EEPROM!\n",
		       pci_name(pdev));
		random_ether_addr(perm_addr);
	}
	SET_IEEE80211_PERM_ADDR(dev, perm_addr);

	dev->extra_tx_headroom = sizeof(struct adm8211_tx_hdr);
	/* dev->flags = IEEE80211_HW_RX_INCLUDES_FCS in promisc mode */
	dev->flags = IEEE80211_HW_SIGNAL_UNSPEC;
	dev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);

	dev->channel_change_time = 1000;
	dev->max_signal = 100;    /* FIXME: find better value */

	dev->queues = 1; /* ADM8211C supports more, maybe ADM8211B too */

	priv->retry_limit = 3;
	priv->ant_power = 0x40;
	priv->tx_power = 0x40;
	priv->lpf_cutoff = 0xFF;
	priv->lnags_threshold = 0xFF;
	priv->mode = NL80211_IFTYPE_UNSPECIFIED;

	/* Power-on issue. EEPROM won't read correctly without */
	if (pdev->revision >= ADM8211_REV_BA) {
		ADM8211_CSR_WRITE(FRCTL, 0);
		ADM8211_CSR_READ(FRCTL);
		ADM8211_CSR_WRITE(FRCTL, 1);
		ADM8211_CSR_READ(FRCTL);
		msleep(100);
	}

	err = adm8211_read_eeprom(dev);
	if (err) {
		printk(KERN_ERR "%s (adm8211): Can't alloc eeprom buffer\n",
		       pci_name(pdev));
		goto err_free_desc;
	}

	priv->channel = 1;

	dev->wiphy->bands[IEEE80211_BAND_2GHZ] = &priv->band;

	err = ieee80211_register_hw(dev);
	if (err) {
		printk(KERN_ERR "%s (adm8211): Cannot register device\n",
		       pci_name(pdev));
		goto err_free_desc;
	}

	printk(KERN_INFO "%s: hwaddr %pM, Rev 0x%02x\n",
	       wiphy_name(dev->wiphy), dev->wiphy->perm_addr,
	       pdev->revision);

	return 0;

 err_free_desc:
	pci_free_consistent(pdev,
			    sizeof(struct adm8211_desc) * priv->rx_ring_size +
			    sizeof(struct adm8211_desc) * priv->tx_ring_size,
			    priv->rx_ring, priv->rx_ring_dma);
	kfree(priv->rx_buffers);

 err_iounmap:
	pci_iounmap(pdev, priv->map);

 err_free_dev:
	pci_set_drvdata(pdev, NULL);
	ieee80211_free_hw(dev);

 err_free_reg:
	pci_release_regions(pdev);

 err_disable_pdev:
	pci_disable_device(pdev);
	return err;
}


static void __devexit adm8211_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *dev = pci_get_drvdata(pdev);
	struct adm8211_priv *priv;

	if (!dev)
		return;

	ieee80211_unregister_hw(dev);

	priv = dev->priv;

	pci_free_consistent(pdev,
			    sizeof(struct adm8211_desc) * priv->rx_ring_size +
			    sizeof(struct adm8211_desc) * priv->tx_ring_size,
			    priv->rx_ring, priv->rx_ring_dma);

	kfree(priv->rx_buffers);
	kfree(priv->eeprom);
	pci_iounmap(pdev, priv->map);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	ieee80211_free_hw(dev);
}


#ifdef CONFIG_PM
static int adm8211_suspend(struct pci_dev *pdev, pm_message_t state)
{
	pci_save_state(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return 0;
}

static int adm8211_resume(struct pci_dev *pdev)
{
	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	return 0;
}
#endif /* CONFIG_PM */


MODULE_DEVICE_TABLE(pci, adm8211_pci_id_table);

/* TODO: implement enable_wake */
static struct pci_driver adm8211_driver = {
	.name		= "adm8211",
	.id_table	= adm8211_pci_id_table,
	.probe		= adm8211_probe,
	.remove		= __devexit_p(adm8211_remove),
#ifdef CONFIG_PM
	.suspend	= adm8211_suspend,
	.resume		= adm8211_resume,
#endif /* CONFIG_PM */
};



static int __init adm8211_init(void)
{
	return pci_register_driver(&adm8211_driver);
}


static void __exit adm8211_exit(void)
{
	pci_unregister_driver(&adm8211_driver);
}


module_init(adm8211_init);
module_exit(adm8211_exit);
