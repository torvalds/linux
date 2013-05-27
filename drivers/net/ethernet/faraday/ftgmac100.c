/*
 * Faraday FTGMAC100 Gigabit Ethernet
 *
 * (C) Copyright 2009-2011 Faraday Technology
 * Po-Yu Chuang <ratbert@faraday-tech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <net/ip.h>

#include "ftgmac100.h"

#define DRV_NAME	"ftgmac100"
#define DRV_VERSION	"0.7"

#define RX_QUEUE_ENTRIES	256	/* must be power of 2 */
#define TX_QUEUE_ENTRIES	512	/* must be power of 2 */

#define MAX_PKT_SIZE		1518
#define RX_BUF_SIZE		PAGE_SIZE	/* must be smaller than 0x3fff */

/******************************************************************************
 * private data
 *****************************************************************************/
struct ftgmac100_descs {
	struct ftgmac100_rxdes rxdes[RX_QUEUE_ENTRIES];
	struct ftgmac100_txdes txdes[TX_QUEUE_ENTRIES];
};

struct ftgmac100 {
	struct resource *res;
	void __iomem *base;
	int irq;

	struct ftgmac100_descs *descs;
	dma_addr_t descs_dma_addr;

	unsigned int rx_pointer;
	unsigned int tx_clean_pointer;
	unsigned int tx_pointer;
	unsigned int tx_pending;

	spinlock_t tx_lock;

	struct net_device *netdev;
	struct device *dev;
	struct napi_struct napi;

	struct mii_bus *mii_bus;
	int phy_irq[PHY_MAX_ADDR];
	struct phy_device *phydev;
	int old_speed;
};

static int ftgmac100_alloc_rx_page(struct ftgmac100 *priv,
				   struct ftgmac100_rxdes *rxdes, gfp_t gfp);

/******************************************************************************
 * internal functions (hardware register access)
 *****************************************************************************/
#define INT_MASK_ALL_ENABLED	(FTGMAC100_INT_RPKT_LOST	| \
				 FTGMAC100_INT_XPKT_ETH		| \
				 FTGMAC100_INT_XPKT_LOST	| \
				 FTGMAC100_INT_AHB_ERR		| \
				 FTGMAC100_INT_PHYSTS_CHG	| \
				 FTGMAC100_INT_RPKT_BUF		| \
				 FTGMAC100_INT_NO_RXBUF)

static void ftgmac100_set_rx_ring_base(struct ftgmac100 *priv, dma_addr_t addr)
{
	iowrite32(addr, priv->base + FTGMAC100_OFFSET_RXR_BADR);
}

static void ftgmac100_set_rx_buffer_size(struct ftgmac100 *priv,
		unsigned int size)
{
	size = FTGMAC100_RBSR_SIZE(size);
	iowrite32(size, priv->base + FTGMAC100_OFFSET_RBSR);
}

static void ftgmac100_set_normal_prio_tx_ring_base(struct ftgmac100 *priv,
						   dma_addr_t addr)
{
	iowrite32(addr, priv->base + FTGMAC100_OFFSET_NPTXR_BADR);
}

static void ftgmac100_txdma_normal_prio_start_polling(struct ftgmac100 *priv)
{
	iowrite32(1, priv->base + FTGMAC100_OFFSET_NPTXPD);
}

static int ftgmac100_reset_hw(struct ftgmac100 *priv)
{
	struct net_device *netdev = priv->netdev;
	int i;

	/* NOTE: reset clears all registers */
	iowrite32(FTGMAC100_MACCR_SW_RST, priv->base + FTGMAC100_OFFSET_MACCR);
	for (i = 0; i < 5; i++) {
		unsigned int maccr;

		maccr = ioread32(priv->base + FTGMAC100_OFFSET_MACCR);
		if (!(maccr & FTGMAC100_MACCR_SW_RST))
			return 0;

		udelay(1000);
	}

	netdev_err(netdev, "software reset failed\n");
	return -EIO;
}

static void ftgmac100_set_mac(struct ftgmac100 *priv, const unsigned char *mac)
{
	unsigned int maddr = mac[0] << 8 | mac[1];
	unsigned int laddr = mac[2] << 24 | mac[3] << 16 | mac[4] << 8 | mac[5];

	iowrite32(maddr, priv->base + FTGMAC100_OFFSET_MAC_MADR);
	iowrite32(laddr, priv->base + FTGMAC100_OFFSET_MAC_LADR);
}

static void ftgmac100_init_hw(struct ftgmac100 *priv)
{
	/* setup ring buffer base registers */
	ftgmac100_set_rx_ring_base(priv,
				   priv->descs_dma_addr +
				   offsetof(struct ftgmac100_descs, rxdes));
	ftgmac100_set_normal_prio_tx_ring_base(priv,
					       priv->descs_dma_addr +
					       offsetof(struct ftgmac100_descs, txdes));

	ftgmac100_set_rx_buffer_size(priv, RX_BUF_SIZE);

	iowrite32(FTGMAC100_APTC_RXPOLL_CNT(1), priv->base + FTGMAC100_OFFSET_APTC);

	ftgmac100_set_mac(priv, priv->netdev->dev_addr);
}

#define MACCR_ENABLE_ALL	(FTGMAC100_MACCR_TXDMA_EN	| \
				 FTGMAC100_MACCR_RXDMA_EN	| \
				 FTGMAC100_MACCR_TXMAC_EN	| \
				 FTGMAC100_MACCR_RXMAC_EN	| \
				 FTGMAC100_MACCR_FULLDUP	| \
				 FTGMAC100_MACCR_CRC_APD	| \
				 FTGMAC100_MACCR_RX_RUNT	| \
				 FTGMAC100_MACCR_RX_BROADPKT)

static void ftgmac100_start_hw(struct ftgmac100 *priv, int speed)
{
	int maccr = MACCR_ENABLE_ALL;

	switch (speed) {
	default:
	case 10:
		break;

	case 100:
		maccr |= FTGMAC100_MACCR_FAST_MODE;
		break;

	case 1000:
		maccr |= FTGMAC100_MACCR_GIGA_MODE;
		break;
	}

	iowrite32(maccr, priv->base + FTGMAC100_OFFSET_MACCR);
}

static void ftgmac100_stop_hw(struct ftgmac100 *priv)
{
	iowrite32(0, priv->base + FTGMAC100_OFFSET_MACCR);
}

/******************************************************************************
 * internal functions (receive descriptor)
 *****************************************************************************/
static bool ftgmac100_rxdes_first_segment(struct ftgmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTGMAC100_RXDES0_FRS);
}

static bool ftgmac100_rxdes_last_segment(struct ftgmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTGMAC100_RXDES0_LRS);
}

static bool ftgmac100_rxdes_packet_ready(struct ftgmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTGMAC100_RXDES0_RXPKT_RDY);
}

static void ftgmac100_rxdes_set_dma_own(struct ftgmac100_rxdes *rxdes)
{
	/* clear status bits */
	rxdes->rxdes0 &= cpu_to_le32(FTGMAC100_RXDES0_EDORR);
}

static bool ftgmac100_rxdes_rx_error(struct ftgmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTGMAC100_RXDES0_RX_ERR);
}

static bool ftgmac100_rxdes_crc_error(struct ftgmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTGMAC100_RXDES0_CRC_ERR);
}

static bool ftgmac100_rxdes_frame_too_long(struct ftgmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTGMAC100_RXDES0_FTL);
}

static bool ftgmac100_rxdes_runt(struct ftgmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTGMAC100_RXDES0_RUNT);
}

static bool ftgmac100_rxdes_odd_nibble(struct ftgmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTGMAC100_RXDES0_RX_ODD_NB);
}

static unsigned int ftgmac100_rxdes_data_length(struct ftgmac100_rxdes *rxdes)
{
	return le32_to_cpu(rxdes->rxdes0) & FTGMAC100_RXDES0_VDBC;
}

static bool ftgmac100_rxdes_multicast(struct ftgmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTGMAC100_RXDES0_MULTICAST);
}

static void ftgmac100_rxdes_set_end_of_ring(struct ftgmac100_rxdes *rxdes)
{
	rxdes->rxdes0 |= cpu_to_le32(FTGMAC100_RXDES0_EDORR);
}

static void ftgmac100_rxdes_set_dma_addr(struct ftgmac100_rxdes *rxdes,
					 dma_addr_t addr)
{
	rxdes->rxdes3 = cpu_to_le32(addr);
}

static dma_addr_t ftgmac100_rxdes_get_dma_addr(struct ftgmac100_rxdes *rxdes)
{
	return le32_to_cpu(rxdes->rxdes3);
}

static bool ftgmac100_rxdes_is_tcp(struct ftgmac100_rxdes *rxdes)
{
	return (rxdes->rxdes1 & cpu_to_le32(FTGMAC100_RXDES1_PROT_MASK)) ==
	       cpu_to_le32(FTGMAC100_RXDES1_PROT_TCPIP);
}

static bool ftgmac100_rxdes_is_udp(struct ftgmac100_rxdes *rxdes)
{
	return (rxdes->rxdes1 & cpu_to_le32(FTGMAC100_RXDES1_PROT_MASK)) ==
	       cpu_to_le32(FTGMAC100_RXDES1_PROT_UDPIP);
}

static bool ftgmac100_rxdes_tcpcs_err(struct ftgmac100_rxdes *rxdes)
{
	return rxdes->rxdes1 & cpu_to_le32(FTGMAC100_RXDES1_TCP_CHKSUM_ERR);
}

static bool ftgmac100_rxdes_udpcs_err(struct ftgmac100_rxdes *rxdes)
{
	return rxdes->rxdes1 & cpu_to_le32(FTGMAC100_RXDES1_UDP_CHKSUM_ERR);
}

static bool ftgmac100_rxdes_ipcs_err(struct ftgmac100_rxdes *rxdes)
{
	return rxdes->rxdes1 & cpu_to_le32(FTGMAC100_RXDES1_IP_CHKSUM_ERR);
}

/*
 * rxdes2 is not used by hardware. We use it to keep track of page.
 * Since hardware does not touch it, we can skip cpu_to_le32()/le32_to_cpu().
 */
static void ftgmac100_rxdes_set_page(struct ftgmac100_rxdes *rxdes, struct page *page)
{
	rxdes->rxdes2 = (unsigned int)page;
}

static struct page *ftgmac100_rxdes_get_page(struct ftgmac100_rxdes *rxdes)
{
	return (struct page *)rxdes->rxdes2;
}

/******************************************************************************
 * internal functions (receive)
 *****************************************************************************/
static int ftgmac100_next_rx_pointer(int pointer)
{
	return (pointer + 1) & (RX_QUEUE_ENTRIES - 1);
}

static void ftgmac100_rx_pointer_advance(struct ftgmac100 *priv)
{
	priv->rx_pointer = ftgmac100_next_rx_pointer(priv->rx_pointer);
}

static struct ftgmac100_rxdes *ftgmac100_current_rxdes(struct ftgmac100 *priv)
{
	return &priv->descs->rxdes[priv->rx_pointer];
}

static struct ftgmac100_rxdes *
ftgmac100_rx_locate_first_segment(struct ftgmac100 *priv)
{
	struct ftgmac100_rxdes *rxdes = ftgmac100_current_rxdes(priv);

	while (ftgmac100_rxdes_packet_ready(rxdes)) {
		if (ftgmac100_rxdes_first_segment(rxdes))
			return rxdes;

		ftgmac100_rxdes_set_dma_own(rxdes);
		ftgmac100_rx_pointer_advance(priv);
		rxdes = ftgmac100_current_rxdes(priv);
	}

	return NULL;
}

static bool ftgmac100_rx_packet_error(struct ftgmac100 *priv,
				      struct ftgmac100_rxdes *rxdes)
{
	struct net_device *netdev = priv->netdev;
	bool error = false;

	if (unlikely(ftgmac100_rxdes_rx_error(rxdes))) {
		if (net_ratelimit())
			netdev_info(netdev, "rx err\n");

		netdev->stats.rx_errors++;
		error = true;
	}

	if (unlikely(ftgmac100_rxdes_crc_error(rxdes))) {
		if (net_ratelimit())
			netdev_info(netdev, "rx crc err\n");

		netdev->stats.rx_crc_errors++;
		error = true;
	} else if (unlikely(ftgmac100_rxdes_ipcs_err(rxdes))) {
		if (net_ratelimit())
			netdev_info(netdev, "rx IP checksum err\n");

		error = true;
	}

	if (unlikely(ftgmac100_rxdes_frame_too_long(rxdes))) {
		if (net_ratelimit())
			netdev_info(netdev, "rx frame too long\n");

		netdev->stats.rx_length_errors++;
		error = true;
	} else if (unlikely(ftgmac100_rxdes_runt(rxdes))) {
		if (net_ratelimit())
			netdev_info(netdev, "rx runt\n");

		netdev->stats.rx_length_errors++;
		error = true;
	} else if (unlikely(ftgmac100_rxdes_odd_nibble(rxdes))) {
		if (net_ratelimit())
			netdev_info(netdev, "rx odd nibble\n");

		netdev->stats.rx_length_errors++;
		error = true;
	}

	return error;
}

static void ftgmac100_rx_drop_packet(struct ftgmac100 *priv)
{
	struct net_device *netdev = priv->netdev;
	struct ftgmac100_rxdes *rxdes = ftgmac100_current_rxdes(priv);
	bool done = false;

	if (net_ratelimit())
		netdev_dbg(netdev, "drop packet %p\n", rxdes);

	do {
		if (ftgmac100_rxdes_last_segment(rxdes))
			done = true;

		ftgmac100_rxdes_set_dma_own(rxdes);
		ftgmac100_rx_pointer_advance(priv);
		rxdes = ftgmac100_current_rxdes(priv);
	} while (!done && ftgmac100_rxdes_packet_ready(rxdes));

	netdev->stats.rx_dropped++;
}

static bool ftgmac100_rx_packet(struct ftgmac100 *priv, int *processed)
{
	struct net_device *netdev = priv->netdev;
	struct ftgmac100_rxdes *rxdes;
	struct sk_buff *skb;
	bool done = false;

	rxdes = ftgmac100_rx_locate_first_segment(priv);
	if (!rxdes)
		return false;

	if (unlikely(ftgmac100_rx_packet_error(priv, rxdes))) {
		ftgmac100_rx_drop_packet(priv);
		return true;
	}

	/* start processing */
	skb = netdev_alloc_skb_ip_align(netdev, 128);
	if (unlikely(!skb)) {
		if (net_ratelimit())
			netdev_err(netdev, "rx skb alloc failed\n");

		ftgmac100_rx_drop_packet(priv);
		return true;
	}

	if (unlikely(ftgmac100_rxdes_multicast(rxdes)))
		netdev->stats.multicast++;

	/*
	 * It seems that HW does checksum incorrectly with fragmented packets,
	 * so we are conservative here - if HW checksum error, let software do
	 * the checksum again.
	 */
	if ((ftgmac100_rxdes_is_tcp(rxdes) && !ftgmac100_rxdes_tcpcs_err(rxdes)) ||
	    (ftgmac100_rxdes_is_udp(rxdes) && !ftgmac100_rxdes_udpcs_err(rxdes)))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	do {
		dma_addr_t map = ftgmac100_rxdes_get_dma_addr(rxdes);
		struct page *page = ftgmac100_rxdes_get_page(rxdes);
		unsigned int size;

		dma_unmap_page(priv->dev, map, RX_BUF_SIZE, DMA_FROM_DEVICE);

		size = ftgmac100_rxdes_data_length(rxdes);
		skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags, page, 0, size);

		skb->len += size;
		skb->data_len += size;
		skb->truesize += PAGE_SIZE;

		if (ftgmac100_rxdes_last_segment(rxdes))
			done = true;

		ftgmac100_alloc_rx_page(priv, rxdes, GFP_ATOMIC);

		ftgmac100_rx_pointer_advance(priv);
		rxdes = ftgmac100_current_rxdes(priv);
	} while (!done);

	/* Small frames are copied into linear part of skb to free one page */
	if (skb->len <= 128) {
		skb->truesize -= PAGE_SIZE;
		__pskb_pull_tail(skb, skb->len);
	} else {
		/* We pull the minimum amount into linear part */
		__pskb_pull_tail(skb, ETH_HLEN);
	}
	skb->protocol = eth_type_trans(skb, netdev);

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += skb->len;

	/* push packet to protocol stack */
	napi_gro_receive(&priv->napi, skb);

	(*processed)++;
	return true;
}

/******************************************************************************
 * internal functions (transmit descriptor)
 *****************************************************************************/
static void ftgmac100_txdes_reset(struct ftgmac100_txdes *txdes)
{
	/* clear all except end of ring bit */
	txdes->txdes0 &= cpu_to_le32(FTGMAC100_TXDES0_EDOTR);
	txdes->txdes1 = 0;
	txdes->txdes2 = 0;
	txdes->txdes3 = 0;
}

static bool ftgmac100_txdes_owned_by_dma(struct ftgmac100_txdes *txdes)
{
	return txdes->txdes0 & cpu_to_le32(FTGMAC100_TXDES0_TXDMA_OWN);
}

static void ftgmac100_txdes_set_dma_own(struct ftgmac100_txdes *txdes)
{
	/*
	 * Make sure dma own bit will not be set before any other
	 * descriptor fields.
	 */
	wmb();
	txdes->txdes0 |= cpu_to_le32(FTGMAC100_TXDES0_TXDMA_OWN);
}

static void ftgmac100_txdes_set_end_of_ring(struct ftgmac100_txdes *txdes)
{
	txdes->txdes0 |= cpu_to_le32(FTGMAC100_TXDES0_EDOTR);
}

static void ftgmac100_txdes_set_first_segment(struct ftgmac100_txdes *txdes)
{
	txdes->txdes0 |= cpu_to_le32(FTGMAC100_TXDES0_FTS);
}

static void ftgmac100_txdes_set_last_segment(struct ftgmac100_txdes *txdes)
{
	txdes->txdes0 |= cpu_to_le32(FTGMAC100_TXDES0_LTS);
}

static void ftgmac100_txdes_set_buffer_size(struct ftgmac100_txdes *txdes,
					    unsigned int len)
{
	txdes->txdes0 |= cpu_to_le32(FTGMAC100_TXDES0_TXBUF_SIZE(len));
}

static void ftgmac100_txdes_set_txint(struct ftgmac100_txdes *txdes)
{
	txdes->txdes1 |= cpu_to_le32(FTGMAC100_TXDES1_TXIC);
}

static void ftgmac100_txdes_set_tcpcs(struct ftgmac100_txdes *txdes)
{
	txdes->txdes1 |= cpu_to_le32(FTGMAC100_TXDES1_TCP_CHKSUM);
}

static void ftgmac100_txdes_set_udpcs(struct ftgmac100_txdes *txdes)
{
	txdes->txdes1 |= cpu_to_le32(FTGMAC100_TXDES1_UDP_CHKSUM);
}

static void ftgmac100_txdes_set_ipcs(struct ftgmac100_txdes *txdes)
{
	txdes->txdes1 |= cpu_to_le32(FTGMAC100_TXDES1_IP_CHKSUM);
}

static void ftgmac100_txdes_set_dma_addr(struct ftgmac100_txdes *txdes,
					 dma_addr_t addr)
{
	txdes->txdes3 = cpu_to_le32(addr);
}

static dma_addr_t ftgmac100_txdes_get_dma_addr(struct ftgmac100_txdes *txdes)
{
	return le32_to_cpu(txdes->txdes3);
}

/*
 * txdes2 is not used by hardware. We use it to keep track of socket buffer.
 * Since hardware does not touch it, we can skip cpu_to_le32()/le32_to_cpu().
 */
static void ftgmac100_txdes_set_skb(struct ftgmac100_txdes *txdes,
				    struct sk_buff *skb)
{
	txdes->txdes2 = (unsigned int)skb;
}

static struct sk_buff *ftgmac100_txdes_get_skb(struct ftgmac100_txdes *txdes)
{
	return (struct sk_buff *)txdes->txdes2;
}

/******************************************************************************
 * internal functions (transmit)
 *****************************************************************************/
static int ftgmac100_next_tx_pointer(int pointer)
{
	return (pointer + 1) & (TX_QUEUE_ENTRIES - 1);
}

static void ftgmac100_tx_pointer_advance(struct ftgmac100 *priv)
{
	priv->tx_pointer = ftgmac100_next_tx_pointer(priv->tx_pointer);
}

static void ftgmac100_tx_clean_pointer_advance(struct ftgmac100 *priv)
{
	priv->tx_clean_pointer = ftgmac100_next_tx_pointer(priv->tx_clean_pointer);
}

static struct ftgmac100_txdes *ftgmac100_current_txdes(struct ftgmac100 *priv)
{
	return &priv->descs->txdes[priv->tx_pointer];
}

static struct ftgmac100_txdes *
ftgmac100_current_clean_txdes(struct ftgmac100 *priv)
{
	return &priv->descs->txdes[priv->tx_clean_pointer];
}

static bool ftgmac100_tx_complete_packet(struct ftgmac100 *priv)
{
	struct net_device *netdev = priv->netdev;
	struct ftgmac100_txdes *txdes;
	struct sk_buff *skb;
	dma_addr_t map;

	if (priv->tx_pending == 0)
		return false;

	txdes = ftgmac100_current_clean_txdes(priv);

	if (ftgmac100_txdes_owned_by_dma(txdes))
		return false;

	skb = ftgmac100_txdes_get_skb(txdes);
	map = ftgmac100_txdes_get_dma_addr(txdes);

	netdev->stats.tx_packets++;
	netdev->stats.tx_bytes += skb->len;

	dma_unmap_single(priv->dev, map, skb_headlen(skb), DMA_TO_DEVICE);

	dev_kfree_skb(skb);

	ftgmac100_txdes_reset(txdes);

	ftgmac100_tx_clean_pointer_advance(priv);

	spin_lock(&priv->tx_lock);
	priv->tx_pending--;
	spin_unlock(&priv->tx_lock);
	netif_wake_queue(netdev);

	return true;
}

static void ftgmac100_tx_complete(struct ftgmac100 *priv)
{
	while (ftgmac100_tx_complete_packet(priv))
		;
}

static int ftgmac100_xmit(struct ftgmac100 *priv, struct sk_buff *skb,
			  dma_addr_t map)
{
	struct net_device *netdev = priv->netdev;
	struct ftgmac100_txdes *txdes;
	unsigned int len = (skb->len < ETH_ZLEN) ? ETH_ZLEN : skb->len;

	txdes = ftgmac100_current_txdes(priv);
	ftgmac100_tx_pointer_advance(priv);

	/* setup TX descriptor */
	ftgmac100_txdes_set_skb(txdes, skb);
	ftgmac100_txdes_set_dma_addr(txdes, map);
	ftgmac100_txdes_set_buffer_size(txdes, len);

	ftgmac100_txdes_set_first_segment(txdes);
	ftgmac100_txdes_set_last_segment(txdes);
	ftgmac100_txdes_set_txint(txdes);
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		__be16 protocol = skb->protocol;

		if (protocol == cpu_to_be16(ETH_P_IP)) {
			u8 ip_proto = ip_hdr(skb)->protocol;

			ftgmac100_txdes_set_ipcs(txdes);
			if (ip_proto == IPPROTO_TCP)
				ftgmac100_txdes_set_tcpcs(txdes);
			else if (ip_proto == IPPROTO_UDP)
				ftgmac100_txdes_set_udpcs(txdes);
		}
	}

	spin_lock(&priv->tx_lock);
	priv->tx_pending++;
	if (priv->tx_pending == TX_QUEUE_ENTRIES)
		netif_stop_queue(netdev);

	/* start transmit */
	ftgmac100_txdes_set_dma_own(txdes);
	spin_unlock(&priv->tx_lock);

	ftgmac100_txdma_normal_prio_start_polling(priv);

	return NETDEV_TX_OK;
}

/******************************************************************************
 * internal functions (buffer)
 *****************************************************************************/
static int ftgmac100_alloc_rx_page(struct ftgmac100 *priv,
				   struct ftgmac100_rxdes *rxdes, gfp_t gfp)
{
	struct net_device *netdev = priv->netdev;
	struct page *page;
	dma_addr_t map;

	page = alloc_page(gfp);
	if (!page) {
		if (net_ratelimit())
			netdev_err(netdev, "failed to allocate rx page\n");
		return -ENOMEM;
	}

	map = dma_map_page(priv->dev, page, 0, RX_BUF_SIZE, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(priv->dev, map))) {
		if (net_ratelimit())
			netdev_err(netdev, "failed to map rx page\n");
		__free_page(page);
		return -ENOMEM;
	}

	ftgmac100_rxdes_set_page(rxdes, page);
	ftgmac100_rxdes_set_dma_addr(rxdes, map);
	ftgmac100_rxdes_set_dma_own(rxdes);
	return 0;
}

static void ftgmac100_free_buffers(struct ftgmac100 *priv)
{
	int i;

	for (i = 0; i < RX_QUEUE_ENTRIES; i++) {
		struct ftgmac100_rxdes *rxdes = &priv->descs->rxdes[i];
		struct page *page = ftgmac100_rxdes_get_page(rxdes);
		dma_addr_t map = ftgmac100_rxdes_get_dma_addr(rxdes);

		if (!page)
			continue;

		dma_unmap_page(priv->dev, map, RX_BUF_SIZE, DMA_FROM_DEVICE);
		__free_page(page);
	}

	for (i = 0; i < TX_QUEUE_ENTRIES; i++) {
		struct ftgmac100_txdes *txdes = &priv->descs->txdes[i];
		struct sk_buff *skb = ftgmac100_txdes_get_skb(txdes);
		dma_addr_t map = ftgmac100_txdes_get_dma_addr(txdes);

		if (!skb)
			continue;

		dma_unmap_single(priv->dev, map, skb_headlen(skb), DMA_TO_DEVICE);
		dev_kfree_skb(skb);
	}

	dma_free_coherent(priv->dev, sizeof(struct ftgmac100_descs),
			  priv->descs, priv->descs_dma_addr);
}

static int ftgmac100_alloc_buffers(struct ftgmac100 *priv)
{
	int i;

	priv->descs = dma_alloc_coherent(priv->dev,
					 sizeof(struct ftgmac100_descs),
					 &priv->descs_dma_addr,
					 GFP_KERNEL | __GFP_ZERO);
	if (!priv->descs)
		return -ENOMEM;

	/* initialize RX ring */
	ftgmac100_rxdes_set_end_of_ring(&priv->descs->rxdes[RX_QUEUE_ENTRIES - 1]);

	for (i = 0; i < RX_QUEUE_ENTRIES; i++) {
		struct ftgmac100_rxdes *rxdes = &priv->descs->rxdes[i];

		if (ftgmac100_alloc_rx_page(priv, rxdes, GFP_KERNEL))
			goto err;
	}

	/* initialize TX ring */
	ftgmac100_txdes_set_end_of_ring(&priv->descs->txdes[TX_QUEUE_ENTRIES - 1]);
	return 0;

err:
	ftgmac100_free_buffers(priv);
	return -ENOMEM;
}

/******************************************************************************
 * internal functions (mdio)
 *****************************************************************************/
static void ftgmac100_adjust_link(struct net_device *netdev)
{
	struct ftgmac100 *priv = netdev_priv(netdev);
	struct phy_device *phydev = priv->phydev;
	int ier;

	if (phydev->speed == priv->old_speed)
		return;

	priv->old_speed = phydev->speed;

	ier = ioread32(priv->base + FTGMAC100_OFFSET_IER);

	/* disable all interrupts */
	iowrite32(0, priv->base + FTGMAC100_OFFSET_IER);

	netif_stop_queue(netdev);
	ftgmac100_stop_hw(priv);

	netif_start_queue(netdev);
	ftgmac100_init_hw(priv);
	ftgmac100_start_hw(priv, phydev->speed);

	/* re-enable interrupts */
	iowrite32(ier, priv->base + FTGMAC100_OFFSET_IER);
}

static int ftgmac100_mii_probe(struct ftgmac100 *priv)
{
	struct net_device *netdev = priv->netdev;
	struct phy_device *phydev = NULL;
	int i;

	/* search for connect PHY device */
	for (i = 0; i < PHY_MAX_ADDR; i++) {
		struct phy_device *tmp = priv->mii_bus->phy_map[i];

		if (tmp) {
			phydev = tmp;
			break;
		}
	}

	/* now we are supposed to have a proper phydev, to attach to... */
	if (!phydev) {
		netdev_info(netdev, "%s: no PHY found\n", netdev->name);
		return -ENODEV;
	}

	phydev = phy_connect(netdev, dev_name(&phydev->dev),
			     &ftgmac100_adjust_link, PHY_INTERFACE_MODE_GMII);

	if (IS_ERR(phydev)) {
		netdev_err(netdev, "%s: Could not attach to PHY\n", netdev->name);
		return PTR_ERR(phydev);
	}

	priv->phydev = phydev;
	return 0;
}

/******************************************************************************
 * struct mii_bus functions
 *****************************************************************************/
static int ftgmac100_mdiobus_read(struct mii_bus *bus, int phy_addr, int regnum)
{
	struct net_device *netdev = bus->priv;
	struct ftgmac100 *priv = netdev_priv(netdev);
	unsigned int phycr;
	int i;

	phycr = ioread32(priv->base + FTGMAC100_OFFSET_PHYCR);

	/* preserve MDC cycle threshold */
	phycr &= FTGMAC100_PHYCR_MDC_CYCTHR_MASK;

	phycr |= FTGMAC100_PHYCR_PHYAD(phy_addr) |
		 FTGMAC100_PHYCR_REGAD(regnum) |
		 FTGMAC100_PHYCR_MIIRD;

	iowrite32(phycr, priv->base + FTGMAC100_OFFSET_PHYCR);

	for (i = 0; i < 10; i++) {
		phycr = ioread32(priv->base + FTGMAC100_OFFSET_PHYCR);

		if ((phycr & FTGMAC100_PHYCR_MIIRD) == 0) {
			int data;

			data = ioread32(priv->base + FTGMAC100_OFFSET_PHYDATA);
			return FTGMAC100_PHYDATA_MIIRDATA(data);
		}

		udelay(100);
	}

	netdev_err(netdev, "mdio read timed out\n");
	return -EIO;
}

static int ftgmac100_mdiobus_write(struct mii_bus *bus, int phy_addr,
				   int regnum, u16 value)
{
	struct net_device *netdev = bus->priv;
	struct ftgmac100 *priv = netdev_priv(netdev);
	unsigned int phycr;
	int data;
	int i;

	phycr = ioread32(priv->base + FTGMAC100_OFFSET_PHYCR);

	/* preserve MDC cycle threshold */
	phycr &= FTGMAC100_PHYCR_MDC_CYCTHR_MASK;

	phycr |= FTGMAC100_PHYCR_PHYAD(phy_addr) |
		 FTGMAC100_PHYCR_REGAD(regnum) |
		 FTGMAC100_PHYCR_MIIWR;

	data = FTGMAC100_PHYDATA_MIIWDATA(value);

	iowrite32(data, priv->base + FTGMAC100_OFFSET_PHYDATA);
	iowrite32(phycr, priv->base + FTGMAC100_OFFSET_PHYCR);

	for (i = 0; i < 10; i++) {
		phycr = ioread32(priv->base + FTGMAC100_OFFSET_PHYCR);

		if ((phycr & FTGMAC100_PHYCR_MIIWR) == 0)
			return 0;

		udelay(100);
	}

	netdev_err(netdev, "mdio write timed out\n");
	return -EIO;
}

static int ftgmac100_mdiobus_reset(struct mii_bus *bus)
{
	return 0;
}

/******************************************************************************
 * struct ethtool_ops functions
 *****************************************************************************/
static void ftgmac100_get_drvinfo(struct net_device *netdev,
				  struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev_name(&netdev->dev), sizeof(info->bus_info));
}

static int ftgmac100_get_settings(struct net_device *netdev,
				  struct ethtool_cmd *cmd)
{
	struct ftgmac100 *priv = netdev_priv(netdev);

	return phy_ethtool_gset(priv->phydev, cmd);
}

static int ftgmac100_set_settings(struct net_device *netdev,
				  struct ethtool_cmd *cmd)
{
	struct ftgmac100 *priv = netdev_priv(netdev);

	return phy_ethtool_sset(priv->phydev, cmd);
}

static const struct ethtool_ops ftgmac100_ethtool_ops = {
	.set_settings		= ftgmac100_set_settings,
	.get_settings		= ftgmac100_get_settings,
	.get_drvinfo		= ftgmac100_get_drvinfo,
	.get_link		= ethtool_op_get_link,
};

/******************************************************************************
 * interrupt handler
 *****************************************************************************/
static irqreturn_t ftgmac100_interrupt(int irq, void *dev_id)
{
	struct net_device *netdev = dev_id;
	struct ftgmac100 *priv = netdev_priv(netdev);

	if (likely(netif_running(netdev))) {
		/* Disable interrupts for polling */
		iowrite32(0, priv->base + FTGMAC100_OFFSET_IER);
		napi_schedule(&priv->napi);
	}

	return IRQ_HANDLED;
}

/******************************************************************************
 * struct napi_struct functions
 *****************************************************************************/
static int ftgmac100_poll(struct napi_struct *napi, int budget)
{
	struct ftgmac100 *priv = container_of(napi, struct ftgmac100, napi);
	struct net_device *netdev = priv->netdev;
	unsigned int status;
	bool completed = true;
	int rx = 0;

	status = ioread32(priv->base + FTGMAC100_OFFSET_ISR);
	iowrite32(status, priv->base + FTGMAC100_OFFSET_ISR);

	if (status & (FTGMAC100_INT_RPKT_BUF | FTGMAC100_INT_NO_RXBUF)) {
		/*
		 * FTGMAC100_INT_RPKT_BUF:
		 *	RX DMA has received packets into RX buffer successfully
		 *
		 * FTGMAC100_INT_NO_RXBUF:
		 *	RX buffer unavailable
		 */
		bool retry;

		do {
			retry = ftgmac100_rx_packet(priv, &rx);
		} while (retry && rx < budget);

		if (retry && rx == budget)
			completed = false;
	}

	if (status & (FTGMAC100_INT_XPKT_ETH | FTGMAC100_INT_XPKT_LOST)) {
		/*
		 * FTGMAC100_INT_XPKT_ETH:
		 *	packet transmitted to ethernet successfully
		 *
		 * FTGMAC100_INT_XPKT_LOST:
		 *	packet transmitted to ethernet lost due to late
		 *	collision or excessive collision
		 */
		ftgmac100_tx_complete(priv);
	}

	if (status & (FTGMAC100_INT_NO_RXBUF | FTGMAC100_INT_RPKT_LOST |
		      FTGMAC100_INT_AHB_ERR | FTGMAC100_INT_PHYSTS_CHG)) {
		if (net_ratelimit())
			netdev_info(netdev, "[ISR] = 0x%x: %s%s%s%s\n", status,
				    status & FTGMAC100_INT_NO_RXBUF ? "NO_RXBUF " : "",
				    status & FTGMAC100_INT_RPKT_LOST ? "RPKT_LOST " : "",
				    status & FTGMAC100_INT_AHB_ERR ? "AHB_ERR " : "",
				    status & FTGMAC100_INT_PHYSTS_CHG ? "PHYSTS_CHG" : "");

		if (status & FTGMAC100_INT_NO_RXBUF) {
			/* RX buffer unavailable */
			netdev->stats.rx_over_errors++;
		}

		if (status & FTGMAC100_INT_RPKT_LOST) {
			/* received packet lost due to RX FIFO full */
			netdev->stats.rx_fifo_errors++;
		}
	}

	if (completed) {
		napi_complete(napi);

		/* enable all interrupts */
		iowrite32(INT_MASK_ALL_ENABLED, priv->base + FTGMAC100_OFFSET_IER);
	}

	return rx;
}

/******************************************************************************
 * struct net_device_ops functions
 *****************************************************************************/
static int ftgmac100_open(struct net_device *netdev)
{
	struct ftgmac100 *priv = netdev_priv(netdev);
	int err;

	err = ftgmac100_alloc_buffers(priv);
	if (err) {
		netdev_err(netdev, "failed to allocate buffers\n");
		goto err_alloc;
	}

	err = request_irq(priv->irq, ftgmac100_interrupt, 0, netdev->name, netdev);
	if (err) {
		netdev_err(netdev, "failed to request irq %d\n", priv->irq);
		goto err_irq;
	}

	priv->rx_pointer = 0;
	priv->tx_clean_pointer = 0;
	priv->tx_pointer = 0;
	priv->tx_pending = 0;

	err = ftgmac100_reset_hw(priv);
	if (err)
		goto err_hw;

	ftgmac100_init_hw(priv);
	ftgmac100_start_hw(priv, 10);

	phy_start(priv->phydev);

	napi_enable(&priv->napi);
	netif_start_queue(netdev);

	/* enable all interrupts */
	iowrite32(INT_MASK_ALL_ENABLED, priv->base + FTGMAC100_OFFSET_IER);
	return 0;

err_hw:
	free_irq(priv->irq, netdev);
err_irq:
	ftgmac100_free_buffers(priv);
err_alloc:
	return err;
}

static int ftgmac100_stop(struct net_device *netdev)
{
	struct ftgmac100 *priv = netdev_priv(netdev);

	/* disable all interrupts */
	iowrite32(0, priv->base + FTGMAC100_OFFSET_IER);

	netif_stop_queue(netdev);
	napi_disable(&priv->napi);
	phy_stop(priv->phydev);

	ftgmac100_stop_hw(priv);
	free_irq(priv->irq, netdev);
	ftgmac100_free_buffers(priv);

	return 0;
}

static int ftgmac100_hard_start_xmit(struct sk_buff *skb,
				     struct net_device *netdev)
{
	struct ftgmac100 *priv = netdev_priv(netdev);
	dma_addr_t map;

	if (unlikely(skb->len > MAX_PKT_SIZE)) {
		if (net_ratelimit())
			netdev_dbg(netdev, "tx packet too big\n");

		netdev->stats.tx_dropped++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	map = dma_map_single(priv->dev, skb->data, skb_headlen(skb), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(priv->dev, map))) {
		/* drop packet */
		if (net_ratelimit())
			netdev_err(netdev, "map socket buffer failed\n");

		netdev->stats.tx_dropped++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	return ftgmac100_xmit(priv, skb, map);
}

/* optional */
static int ftgmac100_do_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct ftgmac100 *priv = netdev_priv(netdev);

	return phy_mii_ioctl(priv->phydev, ifr, cmd);
}

static const struct net_device_ops ftgmac100_netdev_ops = {
	.ndo_open		= ftgmac100_open,
	.ndo_stop		= ftgmac100_stop,
	.ndo_start_xmit		= ftgmac100_hard_start_xmit,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= ftgmac100_do_ioctl,
};

/******************************************************************************
 * struct platform_driver functions
 *****************************************************************************/
static int ftgmac100_probe(struct platform_device *pdev)
{
	struct resource *res;
	int irq;
	struct net_device *netdev;
	struct ftgmac100 *priv;
	int err;
	int i;

	if (!pdev)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	/* setup net_device */
	netdev = alloc_etherdev(sizeof(*priv));
	if (!netdev) {
		err = -ENOMEM;
		goto err_alloc_etherdev;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	SET_ETHTOOL_OPS(netdev, &ftgmac100_ethtool_ops);
	netdev->netdev_ops = &ftgmac100_netdev_ops;
	netdev->features = NETIF_F_IP_CSUM | NETIF_F_GRO;

	platform_set_drvdata(pdev, netdev);

	/* setup private data */
	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->dev = &pdev->dev;

	spin_lock_init(&priv->tx_lock);

	/* initialize NAPI */
	netif_napi_add(netdev, &priv->napi, ftgmac100_poll, 64);

	/* map io memory */
	priv->res = request_mem_region(res->start, resource_size(res),
				       dev_name(&pdev->dev));
	if (!priv->res) {
		dev_err(&pdev->dev, "Could not reserve memory region\n");
		err = -ENOMEM;
		goto err_req_mem;
	}

	priv->base = ioremap(res->start, resource_size(res));
	if (!priv->base) {
		dev_err(&pdev->dev, "Failed to ioremap ethernet registers\n");
		err = -EIO;
		goto err_ioremap;
	}

	priv->irq = irq;

	/* initialize mdio bus */
	priv->mii_bus = mdiobus_alloc();
	if (!priv->mii_bus) {
		err = -EIO;
		goto err_alloc_mdiobus;
	}

	priv->mii_bus->name = "ftgmac100_mdio";
	snprintf(priv->mii_bus->id, MII_BUS_ID_SIZE, "ftgmac100_mii");

	priv->mii_bus->priv = netdev;
	priv->mii_bus->read = ftgmac100_mdiobus_read;
	priv->mii_bus->write = ftgmac100_mdiobus_write;
	priv->mii_bus->reset = ftgmac100_mdiobus_reset;
	priv->mii_bus->irq = priv->phy_irq;

	for (i = 0; i < PHY_MAX_ADDR; i++)
		priv->mii_bus->irq[i] = PHY_POLL;

	err = mdiobus_register(priv->mii_bus);
	if (err) {
		dev_err(&pdev->dev, "Cannot register MDIO bus!\n");
		goto err_register_mdiobus;
	}

	err = ftgmac100_mii_probe(priv);
	if (err) {
		dev_err(&pdev->dev, "MII Probe failed!\n");
		goto err_mii_probe;
	}

	/* register network device */
	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register netdev\n");
		goto err_register_netdev;
	}

	netdev_info(netdev, "irq %d, mapped at %p\n", priv->irq, priv->base);

	if (!is_valid_ether_addr(netdev->dev_addr)) {
		eth_hw_addr_random(netdev);
		netdev_info(netdev, "generated random MAC address %pM\n",
			    netdev->dev_addr);
	}

	return 0;

err_register_netdev:
	phy_disconnect(priv->phydev);
err_mii_probe:
	mdiobus_unregister(priv->mii_bus);
err_register_mdiobus:
	mdiobus_free(priv->mii_bus);
err_alloc_mdiobus:
	iounmap(priv->base);
err_ioremap:
	release_resource(priv->res);
err_req_mem:
	netif_napi_del(&priv->napi);
	free_netdev(netdev);
err_alloc_etherdev:
	return err;
}

static int __exit ftgmac100_remove(struct platform_device *pdev)
{
	struct net_device *netdev;
	struct ftgmac100 *priv;

	netdev = platform_get_drvdata(pdev);
	priv = netdev_priv(netdev);

	unregister_netdev(netdev);

	phy_disconnect(priv->phydev);
	mdiobus_unregister(priv->mii_bus);
	mdiobus_free(priv->mii_bus);

	iounmap(priv->base);
	release_resource(priv->res);

	netif_napi_del(&priv->napi);
	free_netdev(netdev);
	return 0;
}

static struct platform_driver ftgmac100_driver = {
	.probe		= ftgmac100_probe,
	.remove		= __exit_p(ftgmac100_remove),
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(ftgmac100_driver);

MODULE_AUTHOR("Po-Yu Chuang <ratbert@faraday-tech.com>");
MODULE_DESCRIPTION("FTGMAC100 driver");
MODULE_LICENSE("GPL");
