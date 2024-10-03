// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Faraday FTMAC100 10/100 Ethernet
 *
 * (C) Copyright 2009-2011 Faraday Technology
 * Po-Yu Chuang <ratbert@faraday-tech.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>

#include "ftmac100.h"

#define DRV_NAME	"ftmac100"

#define RX_QUEUE_ENTRIES	128	/* must be power of 2 */
#define TX_QUEUE_ENTRIES	16	/* must be power of 2 */

#define RX_BUF_SIZE		2044	/* must be smaller than 0x7ff */
#define MAX_PKT_SIZE		RX_BUF_SIZE /* multi-segment not supported */

#if MAX_PKT_SIZE > 0x7ff
#error invalid MAX_PKT_SIZE
#endif

#if RX_BUF_SIZE > 0x7ff || RX_BUF_SIZE > PAGE_SIZE
#error invalid RX_BUF_SIZE
#endif

/******************************************************************************
 * private data
 *****************************************************************************/
struct ftmac100_descs {
	struct ftmac100_rxdes rxdes[RX_QUEUE_ENTRIES];
	struct ftmac100_txdes txdes[TX_QUEUE_ENTRIES];
};

struct ftmac100 {
	struct resource *res;
	void __iomem *base;
	int irq;

	struct ftmac100_descs *descs;
	dma_addr_t descs_dma_addr;

	unsigned int rx_pointer;
	unsigned int tx_clean_pointer;
	unsigned int tx_pointer;
	unsigned int tx_pending;

	spinlock_t tx_lock;

	struct net_device *netdev;
	struct device *dev;
	struct napi_struct napi;

	struct mii_if_info mii;
};

static int ftmac100_alloc_rx_page(struct ftmac100 *priv,
				  struct ftmac100_rxdes *rxdes, gfp_t gfp);

/******************************************************************************
 * internal functions (hardware register access)
 *****************************************************************************/
#define INT_MASK_ALL_ENABLED	(FTMAC100_INT_RPKT_FINISH	| \
				 FTMAC100_INT_NORXBUF		| \
				 FTMAC100_INT_XPKT_OK		| \
				 FTMAC100_INT_XPKT_LOST		| \
				 FTMAC100_INT_RPKT_LOST		| \
				 FTMAC100_INT_AHB_ERR		| \
				 FTMAC100_INT_PHYSTS_CHG)

#define INT_MASK_ALL_DISABLED	0

static void ftmac100_enable_all_int(struct ftmac100 *priv)
{
	iowrite32(INT_MASK_ALL_ENABLED, priv->base + FTMAC100_OFFSET_IMR);
}

static void ftmac100_disable_all_int(struct ftmac100 *priv)
{
	iowrite32(INT_MASK_ALL_DISABLED, priv->base + FTMAC100_OFFSET_IMR);
}

static void ftmac100_set_rx_ring_base(struct ftmac100 *priv, dma_addr_t addr)
{
	iowrite32(addr, priv->base + FTMAC100_OFFSET_RXR_BADR);
}

static void ftmac100_set_tx_ring_base(struct ftmac100 *priv, dma_addr_t addr)
{
	iowrite32(addr, priv->base + FTMAC100_OFFSET_TXR_BADR);
}

static void ftmac100_txdma_start_polling(struct ftmac100 *priv)
{
	iowrite32(1, priv->base + FTMAC100_OFFSET_TXPD);
}

static int ftmac100_reset(struct ftmac100 *priv)
{
	struct net_device *netdev = priv->netdev;
	int i;

	/* NOTE: reset clears all registers */
	iowrite32(FTMAC100_MACCR_SW_RST, priv->base + FTMAC100_OFFSET_MACCR);

	for (i = 0; i < 5; i++) {
		unsigned int maccr;

		maccr = ioread32(priv->base + FTMAC100_OFFSET_MACCR);
		if (!(maccr & FTMAC100_MACCR_SW_RST)) {
			/*
			 * FTMAC100_MACCR_SW_RST cleared does not indicate
			 * that hardware reset completed (what the f*ck).
			 * We still need to wait for a while.
			 */
			udelay(500);
			return 0;
		}

		udelay(1000);
	}

	netdev_err(netdev, "software reset failed\n");
	return -EIO;
}

static void ftmac100_set_mac(struct ftmac100 *priv, const unsigned char *mac)
{
	unsigned int maddr = mac[0] << 8 | mac[1];
	unsigned int laddr = mac[2] << 24 | mac[3] << 16 | mac[4] << 8 | mac[5];

	iowrite32(maddr, priv->base + FTMAC100_OFFSET_MAC_MADR);
	iowrite32(laddr, priv->base + FTMAC100_OFFSET_MAC_LADR);
}

static void ftmac100_setup_mc_ht(struct ftmac100 *priv)
{
	struct netdev_hw_addr *ha;
	u64 maht = 0; /* Multicast Address Hash Table */

	netdev_for_each_mc_addr(ha, priv->netdev) {
		u32 hash = ether_crc(ETH_ALEN, ha->addr) >> 26;

		maht |= BIT_ULL(hash);
	}

	iowrite32(lower_32_bits(maht), priv->base + FTMAC100_OFFSET_MAHT0);
	iowrite32(upper_32_bits(maht), priv->base + FTMAC100_OFFSET_MAHT1);
}

static void ftmac100_set_rx_bits(struct ftmac100 *priv, unsigned int *maccr)
{
	struct net_device *netdev = priv->netdev;

	/* Clear all */
	*maccr &= ~(FTMAC100_MACCR_RCV_ALL | FTMAC100_MACCR_RX_MULTIPKT |
		   FTMAC100_MACCR_HT_MULTI_EN);

	/* Set the requested bits */
	if (netdev->flags & IFF_PROMISC)
		*maccr |= FTMAC100_MACCR_RCV_ALL;
	if (netdev->flags & IFF_ALLMULTI)
		*maccr |= FTMAC100_MACCR_RX_MULTIPKT;
	else if (netdev_mc_count(netdev)) {
		*maccr |= FTMAC100_MACCR_HT_MULTI_EN;
		ftmac100_setup_mc_ht(priv);
	}
}

#define MACCR_ENABLE_ALL	(FTMAC100_MACCR_XMT_EN	| \
				 FTMAC100_MACCR_RCV_EN	| \
				 FTMAC100_MACCR_XDMA_EN	| \
				 FTMAC100_MACCR_RDMA_EN	| \
				 FTMAC100_MACCR_CRC_APD	| \
				 FTMAC100_MACCR_FULLDUP	| \
				 FTMAC100_MACCR_RX_RUNT	| \
				 FTMAC100_MACCR_RX_BROADPKT)

static int ftmac100_start_hw(struct ftmac100 *priv)
{
	struct net_device *netdev = priv->netdev;
	unsigned int maccr = MACCR_ENABLE_ALL;

	if (ftmac100_reset(priv))
		return -EIO;

	/* setup ring buffer base registers */
	ftmac100_set_rx_ring_base(priv,
				  priv->descs_dma_addr +
				  offsetof(struct ftmac100_descs, rxdes));
	ftmac100_set_tx_ring_base(priv,
				  priv->descs_dma_addr +
				  offsetof(struct ftmac100_descs, txdes));

	iowrite32(FTMAC100_APTC_RXPOLL_CNT(1), priv->base + FTMAC100_OFFSET_APTC);

	ftmac100_set_mac(priv, netdev->dev_addr);

	 /* See ftmac100_change_mtu() */
	if (netdev->mtu > ETH_DATA_LEN)
		maccr |= FTMAC100_MACCR_RX_FTL;

	ftmac100_set_rx_bits(priv, &maccr);

	iowrite32(maccr, priv->base + FTMAC100_OFFSET_MACCR);
	return 0;
}

static void ftmac100_stop_hw(struct ftmac100 *priv)
{
	iowrite32(0, priv->base + FTMAC100_OFFSET_MACCR);
}

/******************************************************************************
 * internal functions (receive descriptor)
 *****************************************************************************/
static bool ftmac100_rxdes_first_segment(struct ftmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTMAC100_RXDES0_FRS);
}

static bool ftmac100_rxdes_last_segment(struct ftmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTMAC100_RXDES0_LRS);
}

static bool ftmac100_rxdes_owned_by_dma(struct ftmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTMAC100_RXDES0_RXDMA_OWN);
}

static void ftmac100_rxdes_set_dma_own(struct ftmac100_rxdes *rxdes)
{
	/* clear status bits */
	rxdes->rxdes0 = cpu_to_le32(FTMAC100_RXDES0_RXDMA_OWN);
}

static bool ftmac100_rxdes_rx_error(struct ftmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTMAC100_RXDES0_RX_ERR);
}

static bool ftmac100_rxdes_crc_error(struct ftmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTMAC100_RXDES0_CRC_ERR);
}

static bool ftmac100_rxdes_runt(struct ftmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTMAC100_RXDES0_RUNT);
}

static bool ftmac100_rxdes_odd_nibble(struct ftmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTMAC100_RXDES0_RX_ODD_NB);
}

static unsigned int ftmac100_rxdes_frame_length(struct ftmac100_rxdes *rxdes)
{
	return le32_to_cpu(rxdes->rxdes0) & FTMAC100_RXDES0_RFL;
}

static bool ftmac100_rxdes_multicast(struct ftmac100_rxdes *rxdes)
{
	return rxdes->rxdes0 & cpu_to_le32(FTMAC100_RXDES0_MULTICAST);
}

static void ftmac100_rxdes_set_buffer_size(struct ftmac100_rxdes *rxdes,
					   unsigned int size)
{
	rxdes->rxdes1 &= cpu_to_le32(FTMAC100_RXDES1_EDORR);
	rxdes->rxdes1 |= cpu_to_le32(FTMAC100_RXDES1_RXBUF_SIZE(size));
}

static void ftmac100_rxdes_set_end_of_ring(struct ftmac100_rxdes *rxdes)
{
	rxdes->rxdes1 |= cpu_to_le32(FTMAC100_RXDES1_EDORR);
}

static void ftmac100_rxdes_set_dma_addr(struct ftmac100_rxdes *rxdes,
					dma_addr_t addr)
{
	rxdes->rxdes2 = cpu_to_le32(addr);
}

static dma_addr_t ftmac100_rxdes_get_dma_addr(struct ftmac100_rxdes *rxdes)
{
	return le32_to_cpu(rxdes->rxdes2);
}

/*
 * rxdes3 is not used by hardware. We use it to keep track of page.
 * Since hardware does not touch it, we can skip cpu_to_le32()/le32_to_cpu().
 */
static void ftmac100_rxdes_set_page(struct ftmac100_rxdes *rxdes, struct page *page)
{
	rxdes->rxdes3 = (unsigned int)page;
}

static struct page *ftmac100_rxdes_get_page(struct ftmac100_rxdes *rxdes)
{
	return (struct page *)rxdes->rxdes3;
}

/******************************************************************************
 * internal functions (receive)
 *****************************************************************************/
static int ftmac100_next_rx_pointer(int pointer)
{
	return (pointer + 1) & (RX_QUEUE_ENTRIES - 1);
}

static void ftmac100_rx_pointer_advance(struct ftmac100 *priv)
{
	priv->rx_pointer = ftmac100_next_rx_pointer(priv->rx_pointer);
}

static struct ftmac100_rxdes *ftmac100_current_rxdes(struct ftmac100 *priv)
{
	return &priv->descs->rxdes[priv->rx_pointer];
}

static struct ftmac100_rxdes *
ftmac100_rx_locate_first_segment(struct ftmac100 *priv)
{
	struct ftmac100_rxdes *rxdes = ftmac100_current_rxdes(priv);

	while (!ftmac100_rxdes_owned_by_dma(rxdes)) {
		if (ftmac100_rxdes_first_segment(rxdes))
			return rxdes;

		ftmac100_rxdes_set_dma_own(rxdes);
		ftmac100_rx_pointer_advance(priv);
		rxdes = ftmac100_current_rxdes(priv);
	}

	return NULL;
}

static bool ftmac100_rx_packet_error(struct ftmac100 *priv,
				     struct ftmac100_rxdes *rxdes)
{
	struct net_device *netdev = priv->netdev;
	bool error = false;

	if (unlikely(ftmac100_rxdes_rx_error(rxdes))) {
		if (net_ratelimit())
			netdev_info(netdev, "rx err\n");

		netdev->stats.rx_errors++;
		error = true;
	}

	if (unlikely(ftmac100_rxdes_crc_error(rxdes))) {
		if (net_ratelimit())
			netdev_info(netdev, "rx crc err\n");

		netdev->stats.rx_crc_errors++;
		error = true;
	}

	if (unlikely(ftmac100_rxdes_runt(rxdes))) {
		if (net_ratelimit())
			netdev_info(netdev, "rx runt\n");

		netdev->stats.rx_length_errors++;
		error = true;
	} else if (unlikely(ftmac100_rxdes_odd_nibble(rxdes))) {
		if (net_ratelimit())
			netdev_info(netdev, "rx odd nibble\n");

		netdev->stats.rx_length_errors++;
		error = true;
	}
	/*
	 * FTMAC100_RXDES0_FTL is not an error, it just indicates that the
	 * frame is longer than 1518 octets. Receiving these is possible when
	 * we told the hardware not to drop them, via FTMAC100_MACCR_RX_FTL.
	 */

	return error;
}

static void ftmac100_rx_drop_packet(struct ftmac100 *priv)
{
	struct net_device *netdev = priv->netdev;
	struct ftmac100_rxdes *rxdes = ftmac100_current_rxdes(priv);
	bool done = false;

	if (net_ratelimit())
		netdev_dbg(netdev, "drop packet %p\n", rxdes);

	do {
		if (ftmac100_rxdes_last_segment(rxdes))
			done = true;

		ftmac100_rxdes_set_dma_own(rxdes);
		ftmac100_rx_pointer_advance(priv);
		rxdes = ftmac100_current_rxdes(priv);
	} while (!done && !ftmac100_rxdes_owned_by_dma(rxdes));

	netdev->stats.rx_dropped++;
}

static bool ftmac100_rx_packet(struct ftmac100 *priv, int *processed)
{
	struct net_device *netdev = priv->netdev;
	struct ftmac100_rxdes *rxdes;
	struct sk_buff *skb;
	struct page *page;
	dma_addr_t map;
	int length;
	bool ret;

	rxdes = ftmac100_rx_locate_first_segment(priv);
	if (!rxdes)
		return false;

	if (unlikely(ftmac100_rx_packet_error(priv, rxdes))) {
		ftmac100_rx_drop_packet(priv);
		return true;
	}

	/* We don't support multi-segment packets for now, so drop them. */
	ret = ftmac100_rxdes_last_segment(rxdes);
	if (unlikely(!ret)) {
		netdev->stats.rx_length_errors++;
		ftmac100_rx_drop_packet(priv);
		return true;
	}

	/* start processing */
	skb = netdev_alloc_skb_ip_align(netdev, 128);
	if (unlikely(!skb)) {
		if (net_ratelimit())
			netdev_err(netdev, "rx skb alloc failed\n");

		ftmac100_rx_drop_packet(priv);
		return true;
	}

	if (unlikely(ftmac100_rxdes_multicast(rxdes)))
		netdev->stats.multicast++;

	map = ftmac100_rxdes_get_dma_addr(rxdes);
	dma_unmap_page(priv->dev, map, RX_BUF_SIZE, DMA_FROM_DEVICE);

	length = ftmac100_rxdes_frame_length(rxdes);
	page = ftmac100_rxdes_get_page(rxdes);
	skb_fill_page_desc(skb, 0, page, 0, length);
	skb->len += length;
	skb->data_len += length;

	if (length > 128) {
		skb->truesize += PAGE_SIZE;
		/* We pull the minimum amount into linear part */
		__pskb_pull_tail(skb, ETH_HLEN);
	} else {
		/* Small frames are copied into linear part to free one page */
		__pskb_pull_tail(skb, length);
	}
	ftmac100_alloc_rx_page(priv, rxdes, GFP_ATOMIC);

	ftmac100_rx_pointer_advance(priv);

	skb->protocol = eth_type_trans(skb, netdev);

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += skb->len;

	/* push packet to protocol stack */
	netif_receive_skb(skb);

	(*processed)++;
	return true;
}

/******************************************************************************
 * internal functions (transmit descriptor)
 *****************************************************************************/
static void ftmac100_txdes_reset(struct ftmac100_txdes *txdes)
{
	/* clear all except end of ring bit */
	txdes->txdes0 = 0;
	txdes->txdes1 &= cpu_to_le32(FTMAC100_TXDES1_EDOTR);
	txdes->txdes2 = 0;
	txdes->txdes3 = 0;
}

static bool ftmac100_txdes_owned_by_dma(struct ftmac100_txdes *txdes)
{
	return txdes->txdes0 & cpu_to_le32(FTMAC100_TXDES0_TXDMA_OWN);
}

static void ftmac100_txdes_set_dma_own(struct ftmac100_txdes *txdes)
{
	/*
	 * Make sure dma own bit will not be set before any other
	 * descriptor fields.
	 */
	wmb();
	txdes->txdes0 |= cpu_to_le32(FTMAC100_TXDES0_TXDMA_OWN);
}

static bool ftmac100_txdes_excessive_collision(struct ftmac100_txdes *txdes)
{
	return txdes->txdes0 & cpu_to_le32(FTMAC100_TXDES0_TXPKT_EXSCOL);
}

static bool ftmac100_txdes_late_collision(struct ftmac100_txdes *txdes)
{
	return txdes->txdes0 & cpu_to_le32(FTMAC100_TXDES0_TXPKT_LATECOL);
}

static void ftmac100_txdes_set_end_of_ring(struct ftmac100_txdes *txdes)
{
	txdes->txdes1 |= cpu_to_le32(FTMAC100_TXDES1_EDOTR);
}

static void ftmac100_txdes_set_first_segment(struct ftmac100_txdes *txdes)
{
	txdes->txdes1 |= cpu_to_le32(FTMAC100_TXDES1_FTS);
}

static void ftmac100_txdes_set_last_segment(struct ftmac100_txdes *txdes)
{
	txdes->txdes1 |= cpu_to_le32(FTMAC100_TXDES1_LTS);
}

static void ftmac100_txdes_set_txint(struct ftmac100_txdes *txdes)
{
	txdes->txdes1 |= cpu_to_le32(FTMAC100_TXDES1_TXIC);
}

static void ftmac100_txdes_set_buffer_size(struct ftmac100_txdes *txdes,
					   unsigned int len)
{
	txdes->txdes1 |= cpu_to_le32(FTMAC100_TXDES1_TXBUF_SIZE(len));
}

static void ftmac100_txdes_set_dma_addr(struct ftmac100_txdes *txdes,
					dma_addr_t addr)
{
	txdes->txdes2 = cpu_to_le32(addr);
}

static dma_addr_t ftmac100_txdes_get_dma_addr(struct ftmac100_txdes *txdes)
{
	return le32_to_cpu(txdes->txdes2);
}

/*
 * txdes3 is not used by hardware. We use it to keep track of socket buffer.
 * Since hardware does not touch it, we can skip cpu_to_le32()/le32_to_cpu().
 */
static void ftmac100_txdes_set_skb(struct ftmac100_txdes *txdes, struct sk_buff *skb)
{
	txdes->txdes3 = (unsigned int)skb;
}

static struct sk_buff *ftmac100_txdes_get_skb(struct ftmac100_txdes *txdes)
{
	return (struct sk_buff *)txdes->txdes3;
}

/******************************************************************************
 * internal functions (transmit)
 *****************************************************************************/
static int ftmac100_next_tx_pointer(int pointer)
{
	return (pointer + 1) & (TX_QUEUE_ENTRIES - 1);
}

static void ftmac100_tx_pointer_advance(struct ftmac100 *priv)
{
	priv->tx_pointer = ftmac100_next_tx_pointer(priv->tx_pointer);
}

static void ftmac100_tx_clean_pointer_advance(struct ftmac100 *priv)
{
	priv->tx_clean_pointer = ftmac100_next_tx_pointer(priv->tx_clean_pointer);
}

static struct ftmac100_txdes *ftmac100_current_txdes(struct ftmac100 *priv)
{
	return &priv->descs->txdes[priv->tx_pointer];
}

static struct ftmac100_txdes *ftmac100_current_clean_txdes(struct ftmac100 *priv)
{
	return &priv->descs->txdes[priv->tx_clean_pointer];
}

static bool ftmac100_tx_complete_packet(struct ftmac100 *priv)
{
	struct net_device *netdev = priv->netdev;
	struct ftmac100_txdes *txdes;
	struct sk_buff *skb;
	dma_addr_t map;

	if (priv->tx_pending == 0)
		return false;

	txdes = ftmac100_current_clean_txdes(priv);

	if (ftmac100_txdes_owned_by_dma(txdes))
		return false;

	skb = ftmac100_txdes_get_skb(txdes);
	map = ftmac100_txdes_get_dma_addr(txdes);

	if (unlikely(ftmac100_txdes_excessive_collision(txdes) ||
		     ftmac100_txdes_late_collision(txdes))) {
		/*
		 * packet transmitted to ethernet lost due to late collision
		 * or excessive collision
		 */
		netdev->stats.tx_aborted_errors++;
	} else {
		netdev->stats.tx_packets++;
		netdev->stats.tx_bytes += skb->len;
	}

	dma_unmap_single(priv->dev, map, skb_headlen(skb), DMA_TO_DEVICE);
	dev_kfree_skb(skb);

	ftmac100_txdes_reset(txdes);

	ftmac100_tx_clean_pointer_advance(priv);

	spin_lock(&priv->tx_lock);
	priv->tx_pending--;
	spin_unlock(&priv->tx_lock);
	netif_wake_queue(netdev);

	return true;
}

static void ftmac100_tx_complete(struct ftmac100 *priv)
{
	while (ftmac100_tx_complete_packet(priv))
		;
}

static netdev_tx_t ftmac100_xmit(struct ftmac100 *priv, struct sk_buff *skb,
				 dma_addr_t map)
{
	struct net_device *netdev = priv->netdev;
	struct ftmac100_txdes *txdes;
	unsigned int len = (skb->len < ETH_ZLEN) ? ETH_ZLEN : skb->len;

	txdes = ftmac100_current_txdes(priv);
	ftmac100_tx_pointer_advance(priv);

	/* setup TX descriptor */
	ftmac100_txdes_set_skb(txdes, skb);
	ftmac100_txdes_set_dma_addr(txdes, map);

	ftmac100_txdes_set_first_segment(txdes);
	ftmac100_txdes_set_last_segment(txdes);
	ftmac100_txdes_set_txint(txdes);
	ftmac100_txdes_set_buffer_size(txdes, len);

	spin_lock(&priv->tx_lock);
	priv->tx_pending++;
	if (priv->tx_pending == TX_QUEUE_ENTRIES)
		netif_stop_queue(netdev);

	/* start transmit */
	ftmac100_txdes_set_dma_own(txdes);
	spin_unlock(&priv->tx_lock);

	ftmac100_txdma_start_polling(priv);
	return NETDEV_TX_OK;
}

/******************************************************************************
 * internal functions (buffer)
 *****************************************************************************/
static int ftmac100_alloc_rx_page(struct ftmac100 *priv,
				  struct ftmac100_rxdes *rxdes, gfp_t gfp)
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

	ftmac100_rxdes_set_page(rxdes, page);
	ftmac100_rxdes_set_dma_addr(rxdes, map);
	ftmac100_rxdes_set_buffer_size(rxdes, RX_BUF_SIZE);
	ftmac100_rxdes_set_dma_own(rxdes);
	return 0;
}

static void ftmac100_free_buffers(struct ftmac100 *priv)
{
	int i;

	for (i = 0; i < RX_QUEUE_ENTRIES; i++) {
		struct ftmac100_rxdes *rxdes = &priv->descs->rxdes[i];
		struct page *page = ftmac100_rxdes_get_page(rxdes);
		dma_addr_t map = ftmac100_rxdes_get_dma_addr(rxdes);

		if (!page)
			continue;

		dma_unmap_page(priv->dev, map, RX_BUF_SIZE, DMA_FROM_DEVICE);
		__free_page(page);
	}

	for (i = 0; i < TX_QUEUE_ENTRIES; i++) {
		struct ftmac100_txdes *txdes = &priv->descs->txdes[i];
		struct sk_buff *skb = ftmac100_txdes_get_skb(txdes);
		dma_addr_t map = ftmac100_txdes_get_dma_addr(txdes);

		if (!skb)
			continue;

		dma_unmap_single(priv->dev, map, skb_headlen(skb), DMA_TO_DEVICE);
		dev_kfree_skb(skb);
	}

	dma_free_coherent(priv->dev, sizeof(struct ftmac100_descs),
			  priv->descs, priv->descs_dma_addr);
}

static int ftmac100_alloc_buffers(struct ftmac100 *priv)
{
	int i;

	priv->descs = dma_alloc_coherent(priv->dev,
					 sizeof(struct ftmac100_descs),
					 &priv->descs_dma_addr, GFP_KERNEL);
	if (!priv->descs)
		return -ENOMEM;

	/* initialize RX ring */
	ftmac100_rxdes_set_end_of_ring(&priv->descs->rxdes[RX_QUEUE_ENTRIES - 1]);

	for (i = 0; i < RX_QUEUE_ENTRIES; i++) {
		struct ftmac100_rxdes *rxdes = &priv->descs->rxdes[i];

		if (ftmac100_alloc_rx_page(priv, rxdes, GFP_KERNEL))
			goto err;
	}

	/* initialize TX ring */
	ftmac100_txdes_set_end_of_ring(&priv->descs->txdes[TX_QUEUE_ENTRIES - 1]);
	return 0;

err:
	ftmac100_free_buffers(priv);
	return -ENOMEM;
}

/******************************************************************************
 * struct mii_if_info functions
 *****************************************************************************/
static int ftmac100_mdio_read(struct net_device *netdev, int phy_id, int reg)
{
	struct ftmac100 *priv = netdev_priv(netdev);
	unsigned int phycr;
	int i;

	phycr = FTMAC100_PHYCR_PHYAD(phy_id) |
		FTMAC100_PHYCR_REGAD(reg) |
		FTMAC100_PHYCR_MIIRD;

	iowrite32(phycr, priv->base + FTMAC100_OFFSET_PHYCR);

	for (i = 0; i < 10; i++) {
		phycr = ioread32(priv->base + FTMAC100_OFFSET_PHYCR);

		if ((phycr & FTMAC100_PHYCR_MIIRD) == 0)
			return phycr & FTMAC100_PHYCR_MIIRDATA;

		udelay(100);
	}

	netdev_err(netdev, "mdio read timed out\n");
	return 0;
}

static void ftmac100_mdio_write(struct net_device *netdev, int phy_id, int reg,
				int data)
{
	struct ftmac100 *priv = netdev_priv(netdev);
	unsigned int phycr;
	int i;

	phycr = FTMAC100_PHYCR_PHYAD(phy_id) |
		FTMAC100_PHYCR_REGAD(reg) |
		FTMAC100_PHYCR_MIIWR;

	data = FTMAC100_PHYWDATA_MIIWDATA(data);

	iowrite32(data, priv->base + FTMAC100_OFFSET_PHYWDATA);
	iowrite32(phycr, priv->base + FTMAC100_OFFSET_PHYCR);

	for (i = 0; i < 10; i++) {
		phycr = ioread32(priv->base + FTMAC100_OFFSET_PHYCR);

		if ((phycr & FTMAC100_PHYCR_MIIWR) == 0)
			return;

		udelay(100);
	}

	netdev_err(netdev, "mdio write timed out\n");
}

/******************************************************************************
 * struct ethtool_ops functions
 *****************************************************************************/
static void ftmac100_get_drvinfo(struct net_device *netdev,
				 struct ethtool_drvinfo *info)
{
	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
	strscpy(info->bus_info, dev_name(&netdev->dev), sizeof(info->bus_info));
}

static int ftmac100_get_link_ksettings(struct net_device *netdev,
				       struct ethtool_link_ksettings *cmd)
{
	struct ftmac100 *priv = netdev_priv(netdev);

	mii_ethtool_get_link_ksettings(&priv->mii, cmd);

	return 0;
}

static int ftmac100_set_link_ksettings(struct net_device *netdev,
				       const struct ethtool_link_ksettings *cmd)
{
	struct ftmac100 *priv = netdev_priv(netdev);
	return mii_ethtool_set_link_ksettings(&priv->mii, cmd);
}

static int ftmac100_nway_reset(struct net_device *netdev)
{
	struct ftmac100 *priv = netdev_priv(netdev);
	return mii_nway_restart(&priv->mii);
}

static u32 ftmac100_get_link(struct net_device *netdev)
{
	struct ftmac100 *priv = netdev_priv(netdev);
	return mii_link_ok(&priv->mii);
}

static const struct ethtool_ops ftmac100_ethtool_ops = {
	.get_drvinfo		= ftmac100_get_drvinfo,
	.nway_reset		= ftmac100_nway_reset,
	.get_link		= ftmac100_get_link,
	.get_link_ksettings	= ftmac100_get_link_ksettings,
	.set_link_ksettings	= ftmac100_set_link_ksettings,
};

/******************************************************************************
 * interrupt handler
 *****************************************************************************/
static irqreturn_t ftmac100_interrupt(int irq, void *dev_id)
{
	struct net_device *netdev = dev_id;
	struct ftmac100 *priv = netdev_priv(netdev);

	/* Disable interrupts for polling */
	ftmac100_disable_all_int(priv);
	if (likely(netif_running(netdev)))
		napi_schedule(&priv->napi);

	return IRQ_HANDLED;
}

/******************************************************************************
 * struct napi_struct functions
 *****************************************************************************/
static int ftmac100_poll(struct napi_struct *napi, int budget)
{
	struct ftmac100 *priv = container_of(napi, struct ftmac100, napi);
	struct net_device *netdev = priv->netdev;
	unsigned int status;
	bool completed = true;
	int rx = 0;

	status = ioread32(priv->base + FTMAC100_OFFSET_ISR);

	if (status & (FTMAC100_INT_RPKT_FINISH | FTMAC100_INT_NORXBUF)) {
		/*
		 * FTMAC100_INT_RPKT_FINISH:
		 *	RX DMA has received packets into RX buffer successfully
		 *
		 * FTMAC100_INT_NORXBUF:
		 *	RX buffer unavailable
		 */
		bool retry;

		do {
			retry = ftmac100_rx_packet(priv, &rx);
		} while (retry && rx < budget);

		if (retry && rx == budget)
			completed = false;
	}

	if (status & (FTMAC100_INT_XPKT_OK | FTMAC100_INT_XPKT_LOST)) {
		/*
		 * FTMAC100_INT_XPKT_OK:
		 *	packet transmitted to ethernet successfully
		 *
		 * FTMAC100_INT_XPKT_LOST:
		 *	packet transmitted to ethernet lost due to late
		 *	collision or excessive collision
		 */
		ftmac100_tx_complete(priv);
	}

	if (status & (FTMAC100_INT_NORXBUF | FTMAC100_INT_RPKT_LOST |
		      FTMAC100_INT_AHB_ERR | FTMAC100_INT_PHYSTS_CHG)) {
		if (net_ratelimit())
			netdev_info(netdev, "[ISR] = 0x%x: %s%s%s%s\n", status,
				    status & FTMAC100_INT_NORXBUF ? "NORXBUF " : "",
				    status & FTMAC100_INT_RPKT_LOST ? "RPKT_LOST " : "",
				    status & FTMAC100_INT_AHB_ERR ? "AHB_ERR " : "",
				    status & FTMAC100_INT_PHYSTS_CHG ? "PHYSTS_CHG" : "");

		if (status & FTMAC100_INT_NORXBUF) {
			/* RX buffer unavailable */
			netdev->stats.rx_over_errors++;
		}

		if (status & FTMAC100_INT_RPKT_LOST) {
			/* received packet lost due to RX FIFO full */
			netdev->stats.rx_fifo_errors++;
		}

		if (status & FTMAC100_INT_PHYSTS_CHG) {
			/* PHY link status change */
			mii_check_link(&priv->mii);
		}
	}

	if (completed) {
		/* stop polling */
		napi_complete(napi);
		ftmac100_enable_all_int(priv);
	}

	return rx;
}

/******************************************************************************
 * struct net_device_ops functions
 *****************************************************************************/
static int ftmac100_open(struct net_device *netdev)
{
	struct ftmac100 *priv = netdev_priv(netdev);
	int err;

	err = ftmac100_alloc_buffers(priv);
	if (err) {
		netdev_err(netdev, "failed to allocate buffers\n");
		goto err_alloc;
	}

	err = request_irq(priv->irq, ftmac100_interrupt, 0, netdev->name, netdev);
	if (err) {
		netdev_err(netdev, "failed to request irq %d\n", priv->irq);
		goto err_irq;
	}

	priv->rx_pointer = 0;
	priv->tx_clean_pointer = 0;
	priv->tx_pointer = 0;
	priv->tx_pending = 0;

	err = ftmac100_start_hw(priv);
	if (err)
		goto err_hw;

	napi_enable(&priv->napi);
	netif_start_queue(netdev);

	ftmac100_enable_all_int(priv);

	return 0;

err_hw:
	free_irq(priv->irq, netdev);
err_irq:
	ftmac100_free_buffers(priv);
err_alloc:
	return err;
}

static int ftmac100_stop(struct net_device *netdev)
{
	struct ftmac100 *priv = netdev_priv(netdev);

	ftmac100_disable_all_int(priv);
	netif_stop_queue(netdev);
	napi_disable(&priv->napi);
	ftmac100_stop_hw(priv);
	free_irq(priv->irq, netdev);
	ftmac100_free_buffers(priv);

	return 0;
}

static netdev_tx_t
ftmac100_hard_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct ftmac100 *priv = netdev_priv(netdev);
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

	return ftmac100_xmit(priv, skb, map);
}

/* optional */
static int ftmac100_do_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct ftmac100 *priv = netdev_priv(netdev);
	struct mii_ioctl_data *data = if_mii(ifr);

	return generic_mii_ioctl(&priv->mii, data, cmd, NULL);
}

static int ftmac100_change_mtu(struct net_device *netdev, int mtu)
{
	struct ftmac100 *priv = netdev_priv(netdev);
	unsigned int maccr;

	maccr = ioread32(priv->base + FTMAC100_OFFSET_MACCR);
	if (mtu > ETH_DATA_LEN) {
		/* process long packets in the driver */
		maccr |= FTMAC100_MACCR_RX_FTL;
	} else {
		/* Let the controller drop incoming packets greater
		 * than 1518 (that is 1500 + 14 Ethernet + 4 FCS).
		 */
		maccr &= ~FTMAC100_MACCR_RX_FTL;
	}
	iowrite32(maccr, priv->base + FTMAC100_OFFSET_MACCR);

	WRITE_ONCE(netdev->mtu, mtu);

	return 0;
}

static void ftmac100_set_rx_mode(struct net_device *netdev)
{
	struct ftmac100 *priv = netdev_priv(netdev);
	unsigned int maccr = ioread32(priv->base + FTMAC100_OFFSET_MACCR);

	ftmac100_set_rx_bits(priv, &maccr);
	iowrite32(maccr, priv->base + FTMAC100_OFFSET_MACCR);
}

static const struct net_device_ops ftmac100_netdev_ops = {
	.ndo_open		= ftmac100_open,
	.ndo_stop		= ftmac100_stop,
	.ndo_start_xmit		= ftmac100_hard_start_xmit,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_eth_ioctl		= ftmac100_do_ioctl,
	.ndo_change_mtu		= ftmac100_change_mtu,
	.ndo_set_rx_mode	= ftmac100_set_rx_mode,
};

/******************************************************************************
 * struct platform_driver functions
 *****************************************************************************/
static int ftmac100_probe(struct platform_device *pdev)
{
	struct resource *res;
	int irq;
	struct net_device *netdev;
	struct ftmac100 *priv;
	int err;

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
	netdev->ethtool_ops = &ftmac100_ethtool_ops;
	netdev->netdev_ops = &ftmac100_netdev_ops;
	netdev->max_mtu = MAX_PKT_SIZE - VLAN_ETH_HLEN;

	err = platform_get_ethdev_address(&pdev->dev, netdev);
	if (err == -EPROBE_DEFER)
		goto defer_get_mac;

	platform_set_drvdata(pdev, netdev);

	/* setup private data */
	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->dev = &pdev->dev;

	spin_lock_init(&priv->tx_lock);

	/* initialize NAPI */
	netif_napi_add(netdev, &priv->napi, ftmac100_poll);

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

	/* initialize struct mii_if_info */
	priv->mii.phy_id	= 0;
	priv->mii.phy_id_mask	= 0x1f;
	priv->mii.reg_num_mask	= 0x1f;
	priv->mii.dev		= netdev;
	priv->mii.mdio_read	= ftmac100_mdio_read;
	priv->mii.mdio_write	= ftmac100_mdio_write;

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
	iounmap(priv->base);
err_ioremap:
	release_resource(priv->res);
err_req_mem:
	netif_napi_del(&priv->napi);
defer_get_mac:
	free_netdev(netdev);
err_alloc_etherdev:
	return err;
}

static void ftmac100_remove(struct platform_device *pdev)
{
	struct net_device *netdev;
	struct ftmac100 *priv;

	netdev = platform_get_drvdata(pdev);
	priv = netdev_priv(netdev);

	unregister_netdev(netdev);

	iounmap(priv->base);
	release_resource(priv->res);

	netif_napi_del(&priv->napi);
	free_netdev(netdev);
}

static const struct of_device_id ftmac100_of_ids[] = {
	{ .compatible = "andestech,atmac100" },
	{ }
};

static struct platform_driver ftmac100_driver = {
	.probe		= ftmac100_probe,
	.remove		= ftmac100_remove,
	.driver		= {
		.name	= DRV_NAME,
		.of_match_table = ftmac100_of_ids
	},
};

/******************************************************************************
 * initialization / finalization
 *****************************************************************************/
module_platform_driver(ftmac100_driver);

MODULE_AUTHOR("Po-Yu Chuang <ratbert@faraday-tech.com>");
MODULE_DESCRIPTION("FTMAC100 driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, ftmac100_of_ids);
