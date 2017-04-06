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
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <net/ip.h>
#include <net/ncsi.h>

#include "ftgmac100.h"

#define DRV_NAME	"ftgmac100"
#define DRV_VERSION	"0.7"

#define RX_QUEUE_ENTRIES	256	/* must be power of 2 */
#define TX_QUEUE_ENTRIES	512	/* must be power of 2 */

#define MAX_PKT_SIZE		1536
#define RX_BUF_SIZE		MAX_PKT_SIZE	/* must be smaller than 0x3fff */

struct ftgmac100_descs {
	struct ftgmac100_rxdes rxdes[RX_QUEUE_ENTRIES];
	struct ftgmac100_txdes txdes[TX_QUEUE_ENTRIES];
};

struct ftgmac100 {
	/* Registers */
	struct resource *res;
	void __iomem *base;

	struct ftgmac100_descs *descs;
	dma_addr_t descs_dma_addr;

	/* Rx ring */
	struct sk_buff *rx_skbs[RX_QUEUE_ENTRIES];
	unsigned int rx_pointer;
	u32 rxdes0_edorr_mask;

	/* Tx ring */
	unsigned int tx_clean_pointer;
	unsigned int tx_pointer;
	unsigned int tx_pending;
	u32 txdes0_edotr_mask;
	spinlock_t tx_lock;

	/* Scratch page to use when rx skb alloc fails */
	void *rx_scratch;
	dma_addr_t rx_scratch_dma;

	/* Component structures */
	struct net_device *netdev;
	struct device *dev;
	struct ncsi_dev *ndev;
	struct napi_struct napi;
	struct work_struct reset_task;
	struct mii_bus *mii_bus;

	/* Link management */
	int cur_speed;
	int cur_duplex;
	bool use_ncsi;

	/* Misc */
	bool need_mac_restart;
};

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

static int ftgmac100_reset_mac(struct ftgmac100 *priv, u32 maccr)
{
	struct net_device *netdev = priv->netdev;
	int i;

	/* NOTE: reset clears all registers */
	iowrite32(maccr, priv->base + FTGMAC100_OFFSET_MACCR);
	iowrite32(maccr | FTGMAC100_MACCR_SW_RST,
		  priv->base + FTGMAC100_OFFSET_MACCR);
	for (i = 0; i < 50; i++) {
		unsigned int maccr;

		maccr = ioread32(priv->base + FTGMAC100_OFFSET_MACCR);
		if (!(maccr & FTGMAC100_MACCR_SW_RST))
			return 0;

		udelay(1);
	}

	netdev_err(netdev, "Hardware reset failed\n");
	return -EIO;
}

static int ftgmac100_reset_and_config_mac(struct ftgmac100 *priv)
{
	u32 maccr = 0;

	switch (priv->cur_speed) {
	case SPEED_10:
	case 0: /* no link */
		break;

	case SPEED_100:
		maccr |= FTGMAC100_MACCR_FAST_MODE;
		break;

	case SPEED_1000:
		maccr |= FTGMAC100_MACCR_GIGA_MODE;
		break;
	default:
		netdev_err(priv->netdev, "Unknown speed %d !\n",
			   priv->cur_speed);
		break;
	}

	/* (Re)initialize the queue pointers */
	priv->rx_pointer = 0;
	priv->tx_clean_pointer = 0;
	priv->tx_pointer = 0;
	priv->tx_pending = 0;

	/* The doc says reset twice with 10us interval */
	if (ftgmac100_reset_mac(priv, maccr))
		return -EIO;
	usleep_range(10, 1000);
	return ftgmac100_reset_mac(priv, maccr);
}

static void ftgmac100_set_mac(struct ftgmac100 *priv, const unsigned char *mac)
{
	unsigned int maddr = mac[0] << 8 | mac[1];
	unsigned int laddr = mac[2] << 24 | mac[3] << 16 | mac[4] << 8 | mac[5];

	iowrite32(maddr, priv->base + FTGMAC100_OFFSET_MAC_MADR);
	iowrite32(laddr, priv->base + FTGMAC100_OFFSET_MAC_LADR);
}

static void ftgmac100_setup_mac(struct ftgmac100 *priv)
{
	u8 mac[ETH_ALEN];
	unsigned int m;
	unsigned int l;
	void *addr;

	addr = device_get_mac_address(priv->dev, mac, ETH_ALEN);
	if (addr) {
		ether_addr_copy(priv->netdev->dev_addr, mac);
		dev_info(priv->dev, "Read MAC address %pM from device tree\n",
			 mac);
		return;
	}

	m = ioread32(priv->base + FTGMAC100_OFFSET_MAC_MADR);
	l = ioread32(priv->base + FTGMAC100_OFFSET_MAC_LADR);

	mac[0] = (m >> 8) & 0xff;
	mac[1] = m & 0xff;
	mac[2] = (l >> 24) & 0xff;
	mac[3] = (l >> 16) & 0xff;
	mac[4] = (l >> 8) & 0xff;
	mac[5] = l & 0xff;

	if (is_valid_ether_addr(mac)) {
		ether_addr_copy(priv->netdev->dev_addr, mac);
		dev_info(priv->dev, "Read MAC address %pM from chip\n", mac);
	} else {
		eth_hw_addr_random(priv->netdev);
		dev_info(priv->dev, "Generated random MAC address %pM\n",
			 priv->netdev->dev_addr);
	}
}

static int ftgmac100_set_mac_addr(struct net_device *dev, void *p)
{
	int ret;

	ret = eth_prepare_mac_addr_change(dev, p);
	if (ret < 0)
		return ret;

	eth_commit_mac_addr_change(dev, p);
	ftgmac100_set_mac(netdev_priv(dev), dev->dev_addr);

	return 0;
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

static void ftgmac100_start_hw(struct ftgmac100 *priv)
{
	u32 maccr = ioread32(priv->base + FTGMAC100_OFFSET_MACCR);

	/* Keep the original GMAC and FAST bits */
	maccr &= (FTGMAC100_MACCR_FAST_MODE | FTGMAC100_MACCR_GIGA_MODE);

	/* Add all the main enable bits */
	maccr |= FTGMAC100_MACCR_TXDMA_EN	|
		 FTGMAC100_MACCR_RXDMA_EN	|
		 FTGMAC100_MACCR_TXMAC_EN	|
		 FTGMAC100_MACCR_RXMAC_EN	|
		 FTGMAC100_MACCR_CRC_APD	|
		 FTGMAC100_MACCR_PHY_LINK_LEVEL	|
		 FTGMAC100_MACCR_RX_RUNT	|
		 FTGMAC100_MACCR_RX_BROADPKT;

	/* Add other bits as needed */
	if (priv->cur_duplex == DUPLEX_FULL)
		maccr |= FTGMAC100_MACCR_FULLDUP;

	/* Hit the HW */
	iowrite32(maccr, priv->base + FTGMAC100_OFFSET_MACCR);
}

static void ftgmac100_stop_hw(struct ftgmac100 *priv)
{
	iowrite32(0, priv->base + FTGMAC100_OFFSET_MACCR);
}

static int ftgmac100_alloc_rx_buf(struct ftgmac100 *priv, unsigned int entry,
				  struct ftgmac100_rxdes *rxdes, gfp_t gfp)
{
	struct net_device *netdev = priv->netdev;
	struct sk_buff *skb;
	dma_addr_t map;
	int err;

	skb = netdev_alloc_skb_ip_align(netdev, RX_BUF_SIZE);
	if (unlikely(!skb)) {
		if (net_ratelimit())
			netdev_warn(netdev, "failed to allocate rx skb\n");
		err = -ENOMEM;
		map = priv->rx_scratch_dma;
	} else {
		map = dma_map_single(priv->dev, skb->data, RX_BUF_SIZE,
				     DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(priv->dev, map))) {
			if (net_ratelimit())
				netdev_err(netdev, "failed to map rx page\n");
			dev_kfree_skb_any(skb);
			map = priv->rx_scratch_dma;
			skb = NULL;
			err = -ENOMEM;
		}
	}

	/* Store skb */
	priv->rx_skbs[entry] = skb;

	/* Store DMA address into RX desc */
	rxdes->rxdes3 = cpu_to_le32(map);

	/* Ensure the above is ordered vs clearing the OWN bit */
	dma_wmb();

	/* Clean status (which resets own bit) */
	if (entry == (RX_QUEUE_ENTRIES - 1))
		rxdes->rxdes0 = cpu_to_le32(priv->rxdes0_edorr_mask);
	else
		rxdes->rxdes0 = 0;

	return 0;
}

static int ftgmac100_next_rx_pointer(int pointer)
{
	return (pointer + 1) & (RX_QUEUE_ENTRIES - 1);
}

static void ftgmac100_rx_packet_error(struct ftgmac100 *priv, u32 status)
{
	struct net_device *netdev = priv->netdev;

	if (status & FTGMAC100_RXDES0_RX_ERR)
		netdev->stats.rx_errors++;

	if (status & FTGMAC100_RXDES0_CRC_ERR)
		netdev->stats.rx_crc_errors++;

	if (status & (FTGMAC100_RXDES0_FTL |
		      FTGMAC100_RXDES0_RUNT |
		      FTGMAC100_RXDES0_RX_ODD_NB))
		netdev->stats.rx_length_errors++;
}

static bool ftgmac100_rx_packet(struct ftgmac100 *priv, int *processed)
{
	struct net_device *netdev = priv->netdev;
	struct ftgmac100_rxdes *rxdes;
	struct sk_buff *skb;
	unsigned int pointer, size;
	u32 status, csum_vlan;
	dma_addr_t map;

	/* Grab next RX descriptor */
	pointer = priv->rx_pointer;
	rxdes = &priv->descs->rxdes[pointer];

	/* Grab descriptor status */
	status = le32_to_cpu(rxdes->rxdes0);

	/* Do we have a packet ? */
	if (!(status & FTGMAC100_RXDES0_RXPKT_RDY))
		return false;

	/* Order subsequent reads with the test for the ready bit */
	dma_rmb();

	/* We don't cope with fragmented RX packets */
	if (unlikely(!(status & FTGMAC100_RXDES0_FRS) ||
		     !(status & FTGMAC100_RXDES0_LRS)))
		goto drop;

	/* Grab received size and csum vlan field in the descriptor */
	size = status & FTGMAC100_RXDES0_VDBC;
	csum_vlan = le32_to_cpu(rxdes->rxdes1);

	/* Any error (other than csum offload) flagged ? */
	if (unlikely(status & RXDES0_ANY_ERROR)) {
		/* Correct for incorrect flagging of runt packets
		 * with vlan tags... Just accept a runt packet that
		 * has been flagged as vlan and whose size is at
		 * least 60 bytes.
		 */
		if ((status & FTGMAC100_RXDES0_RUNT) &&
		    (csum_vlan & FTGMAC100_RXDES1_VLANTAG_AVAIL) &&
		    (size >= 60))
			status &= ~FTGMAC100_RXDES0_RUNT;

		/* Any error still in there ? */
		if (status & RXDES0_ANY_ERROR) {
			ftgmac100_rx_packet_error(priv, status);
			goto drop;
		}
	}

	/* If the packet had no skb (failed to allocate earlier)
	 * then try to allocate one and skip
	 */
	skb = priv->rx_skbs[pointer];
	if (!unlikely(skb)) {
		ftgmac100_alloc_rx_buf(priv, pointer, rxdes, GFP_ATOMIC);
		goto drop;
	}

	if (unlikely(status & FTGMAC100_RXDES0_MULTICAST))
		netdev->stats.multicast++;

	/* If the HW found checksum errors, bounce it to software.
	 *
	 * If we didn't, we need to see if the packet was recognized
	 * by HW as one of the supported checksummed protocols before
	 * we accept the HW test results.
	 */
	if (netdev->features & NETIF_F_RXCSUM) {
		u32 err_bits = FTGMAC100_RXDES1_TCP_CHKSUM_ERR |
			FTGMAC100_RXDES1_UDP_CHKSUM_ERR |
			FTGMAC100_RXDES1_IP_CHKSUM_ERR;
		if ((csum_vlan & err_bits) ||
		    !(csum_vlan & FTGMAC100_RXDES1_PROT_MASK))
			skb->ip_summed = CHECKSUM_NONE;
		else
			skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	/* Transfer received size to skb */
	skb_put(skb, size);

	/* Tear down DMA mapping, do necessary cache management */
	map = le32_to_cpu(rxdes->rxdes3);

#if defined(CONFIG_ARM) && !defined(CONFIG_ARM_DMA_USE_IOMMU)
	/* When we don't have an iommu, we can save cycles by not
	 * invalidating the cache for the part of the packet that
	 * wasn't received.
	 */
	dma_unmap_single(priv->dev, map, size, DMA_FROM_DEVICE);
#else
	dma_unmap_single(priv->dev, map, RX_BUF_SIZE, DMA_FROM_DEVICE);
#endif


	/* Resplenish rx ring */
	ftgmac100_alloc_rx_buf(priv, pointer, rxdes, GFP_ATOMIC);
	priv->rx_pointer = ftgmac100_next_rx_pointer(pointer);

	skb->protocol = eth_type_trans(skb, netdev);

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += size;

	/* push packet to protocol stack */
	if (skb->ip_summed == CHECKSUM_NONE)
		netif_receive_skb(skb);
	else
		napi_gro_receive(&priv->napi, skb);

	(*processed)++;
	return true;

 drop:
	/* Clean rxdes0 (which resets own bit) */
	rxdes->rxdes0 = cpu_to_le32(status & priv->rxdes0_edorr_mask);
	priv->rx_pointer = ftgmac100_next_rx_pointer(pointer);
	netdev->stats.rx_dropped++;
	return true;
}

static void ftgmac100_txdes_reset(const struct ftgmac100 *priv,
				  struct ftgmac100_txdes *txdes)
{
	/* clear all except end of ring bit */
	txdes->txdes0 &= cpu_to_le32(priv->txdes0_edotr_mask);
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

static void ftgmac100_txdes_set_end_of_ring(const struct ftgmac100 *priv,
					    struct ftgmac100_txdes *txdes)
{
	txdes->txdes0 |= cpu_to_le32(priv->txdes0_edotr_mask);
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

	ftgmac100_txdes_reset(priv, txdes);

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

static void ftgmac100_free_buffers(struct ftgmac100 *priv)
{
	int i;

	/* Free all RX buffers */
	for (i = 0; i < RX_QUEUE_ENTRIES; i++) {
		struct ftgmac100_rxdes *rxdes = &priv->descs->rxdes[i];
		struct sk_buff *skb = priv->rx_skbs[i];
		dma_addr_t map = le32_to_cpu(rxdes->rxdes3);

		if (!skb)
			continue;

		priv->rx_skbs[i] = NULL;
		dma_unmap_single(priv->dev, map, RX_BUF_SIZE, DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
	}

	/* Free all TX buffers */
	for (i = 0; i < TX_QUEUE_ENTRIES; i++) {
		struct ftgmac100_txdes *txdes = &priv->descs->txdes[i];
		struct sk_buff *skb = ftgmac100_txdes_get_skb(txdes);
		dma_addr_t map = ftgmac100_txdes_get_dma_addr(txdes);

		if (!skb)
			continue;

		dma_unmap_single(priv->dev, map, skb_headlen(skb), DMA_TO_DEVICE);
		kfree_skb(skb);
	}
}

static void ftgmac100_free_rings(struct ftgmac100 *priv)
{
	/* Free descriptors */
	if (priv->descs)
		dma_free_coherent(priv->dev, sizeof(struct ftgmac100_descs),
				  priv->descs, priv->descs_dma_addr);

	/* Free scratch packet buffer */
	if (priv->rx_scratch)
		dma_free_coherent(priv->dev, RX_BUF_SIZE,
				  priv->rx_scratch, priv->rx_scratch_dma);
}

static int ftgmac100_alloc_rings(struct ftgmac100 *priv)
{
	/* Allocate descriptors */
	priv->descs = dma_zalloc_coherent(priv->dev,
					  sizeof(struct ftgmac100_descs),
					  &priv->descs_dma_addr, GFP_KERNEL);
	if (!priv->descs)
		return -ENOMEM;

	/* Allocate scratch packet buffer */
	priv->rx_scratch = dma_alloc_coherent(priv->dev,
					      RX_BUF_SIZE,
					      &priv->rx_scratch_dma,
					      GFP_KERNEL);
	if (!priv->rx_scratch)
		return -ENOMEM;

	return 0;
}

static void ftgmac100_init_rings(struct ftgmac100 *priv)
{
	struct ftgmac100_rxdes *rxdes;
	int i;

	/* Initialize RX ring */
	for (i = 0; i < RX_QUEUE_ENTRIES; i++) {
		rxdes = &priv->descs->rxdes[i];
		rxdes->rxdes0 = 0;
		rxdes->rxdes3 = cpu_to_le32(priv->rx_scratch_dma);
	}
	/* Mark the end of the ring */
	rxdes->rxdes0 |= cpu_to_le32(priv->rxdes0_edorr_mask);

	/* Initialize TX ring */
	for (i = 0; i < TX_QUEUE_ENTRIES; i++)
		priv->descs->txdes[i].txdes0 = 0;
	ftgmac100_txdes_set_end_of_ring(priv, &priv->descs->txdes[i -1]);
}

static int ftgmac100_alloc_rx_buffers(struct ftgmac100 *priv)
{
	int i;

	for (i = 0; i < RX_QUEUE_ENTRIES; i++) {
		struct ftgmac100_rxdes *rxdes = &priv->descs->rxdes[i];

		if (ftgmac100_alloc_rx_buf(priv, i, rxdes, GFP_KERNEL))
			return -ENOMEM;
	}
	return 0;
}

static void ftgmac100_adjust_link(struct net_device *netdev)
{
	struct ftgmac100 *priv = netdev_priv(netdev);
	struct phy_device *phydev = netdev->phydev;
	int new_speed;

	/* We store "no link" as speed 0 */
	if (!phydev->link)
		new_speed = 0;
	else
		new_speed = phydev->speed;

	if (phydev->speed == priv->cur_speed &&
	    phydev->duplex == priv->cur_duplex)
		return;

	/* Print status if we have a link or we had one and just lost it,
	 * don't print otherwise.
	 */
	if (new_speed || priv->cur_speed)
		phy_print_status(phydev);

	priv->cur_speed = new_speed;
	priv->cur_duplex = phydev->duplex;

	/* Link is down, do nothing else */
	if (!new_speed)
		return;

	/* Disable all interrupts */
	iowrite32(0, priv->base + FTGMAC100_OFFSET_IER);

	/* Reset the adapter asynchronously */
	schedule_work(&priv->reset_task);
}

static int ftgmac100_mii_probe(struct ftgmac100 *priv)
{
	struct net_device *netdev = priv->netdev;
	struct phy_device *phydev;

	phydev = phy_find_first(priv->mii_bus);
	if (!phydev) {
		netdev_info(netdev, "%s: no PHY found\n", netdev->name);
		return -ENODEV;
	}

	phydev = phy_connect(netdev, phydev_name(phydev),
			     &ftgmac100_adjust_link, PHY_INTERFACE_MODE_GMII);

	if (IS_ERR(phydev)) {
		netdev_err(netdev, "%s: Could not attach to PHY\n", netdev->name);
		return PTR_ERR(phydev);
	}

	return 0;
}

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

static void ftgmac100_get_drvinfo(struct net_device *netdev,
				  struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev_name(&netdev->dev), sizeof(info->bus_info));
}

static const struct ethtool_ops ftgmac100_ethtool_ops = {
	.get_drvinfo		= ftgmac100_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
};

static irqreturn_t ftgmac100_interrupt(int irq, void *dev_id)
{
	struct net_device *netdev = dev_id;
	struct ftgmac100 *priv = netdev_priv(netdev);
	unsigned int status, new_mask = FTGMAC100_INT_BAD;

	/* Fetch and clear interrupt bits, process abnormal ones */
	status = ioread32(priv->base + FTGMAC100_OFFSET_ISR);
	iowrite32(status, priv->base + FTGMAC100_OFFSET_ISR);
	if (unlikely(status & FTGMAC100_INT_BAD)) {

		/* RX buffer unavailable */
		if (status & FTGMAC100_INT_NO_RXBUF)
			netdev->stats.rx_over_errors++;

		/* received packet lost due to RX FIFO full */
		if (status & FTGMAC100_INT_RPKT_LOST)
			netdev->stats.rx_fifo_errors++;

		/* sent packet lost due to excessive TX collision */
		if (status & FTGMAC100_INT_XPKT_LOST)
			netdev->stats.tx_fifo_errors++;

		/* AHB error -> Reset the chip */
		if (status & FTGMAC100_INT_AHB_ERR) {
			if (net_ratelimit())
				netdev_warn(netdev,
					   "AHB bus error ! Resetting chip.\n");
			iowrite32(0, priv->base + FTGMAC100_OFFSET_IER);
			schedule_work(&priv->reset_task);
			return IRQ_HANDLED;
		}

		/* We may need to restart the MAC after such errors, delay
		 * this until after we have freed some Rx buffers though
		 */
		priv->need_mac_restart = true;

		/* Disable those errors until we restart */
		new_mask &= ~status;
	}

	/* Only enable "bad" interrupts while NAPI is on */
	iowrite32(new_mask, priv->base + FTGMAC100_OFFSET_IER);

	/* Schedule NAPI bh */
	napi_schedule_irqoff(&priv->napi);

	return IRQ_HANDLED;
}

static bool ftgmac100_check_rx(struct ftgmac100 *priv)
{
	struct ftgmac100_rxdes *rxdes = &priv->descs->rxdes[priv->rx_pointer];

	/* Do we have a packet ? */
	return !!(rxdes->rxdes0 & cpu_to_le32(FTGMAC100_RXDES0_RXPKT_RDY));
}

static int ftgmac100_poll(struct napi_struct *napi, int budget)
{
	struct ftgmac100 *priv = container_of(napi, struct ftgmac100, napi);
	bool more, completed = true;
	int rx = 0;

	ftgmac100_tx_complete(priv);

	do {
		more = ftgmac100_rx_packet(priv, &rx);
	} while (more && rx < budget);

	if (more && rx == budget)
		completed = false;


	/* The interrupt is telling us to kick the MAC back to life
	 * after an RX overflow
	 */
	if (unlikely(priv->need_mac_restart)) {
		ftgmac100_start_hw(priv);

		/* Re-enable "bad" interrupts */
		iowrite32(FTGMAC100_INT_BAD,
			  priv->base + FTGMAC100_OFFSET_IER);
	}

	/* Keep NAPI going if we have still packets to reclaim */
	if (priv->tx_pending)
		return budget;

	if (completed) {
		/* We are about to re-enable all interrupts. However
		 * the HW has been latching RX/TX packet interrupts while
		 * they were masked. So we clear them first, then we need
		 * to re-check if there's something to process
		 */
		iowrite32(FTGMAC100_INT_RXTX,
			  priv->base + FTGMAC100_OFFSET_ISR);
		if (ftgmac100_check_rx(priv) || priv->tx_pending)
			return budget;

		/* deschedule NAPI */
		napi_complete(napi);

		/* enable all interrupts */
		iowrite32(FTGMAC100_INT_ALL,
			  priv->base + FTGMAC100_OFFSET_IER);
	}

	return rx;
}

static int ftgmac100_init_all(struct ftgmac100 *priv, bool ignore_alloc_err)
{
	int err = 0;

	/* Re-init descriptors (adjust queue sizes) */
	ftgmac100_init_rings(priv);

	/* Realloc rx descriptors */
	err = ftgmac100_alloc_rx_buffers(priv);
	if (err && !ignore_alloc_err)
		return err;

	/* Reinit and restart HW */
	ftgmac100_init_hw(priv);
	ftgmac100_start_hw(priv);

	/* Re-enable the device */
	napi_enable(&priv->napi);
	netif_start_queue(priv->netdev);

	/* Enable all interrupts */
	iowrite32(FTGMAC100_INT_ALL, priv->base + FTGMAC100_OFFSET_IER);

	return err;
}

static void ftgmac100_reset_task(struct work_struct *work)
{
	struct ftgmac100 *priv = container_of(work, struct ftgmac100,
					      reset_task);
	struct net_device *netdev = priv->netdev;
	int err;

	netdev_dbg(netdev, "Resetting NIC...\n");

	/* Lock the world */
	rtnl_lock();
	if (netdev->phydev)
		mutex_lock(&netdev->phydev->lock);
	if (priv->mii_bus)
		mutex_lock(&priv->mii_bus->mdio_lock);


	/* Check if the interface is still up */
	if (!netif_running(netdev))
		goto bail;

	/* Stop the network stack */
	netif_trans_update(netdev);
	napi_disable(&priv->napi);
	netif_tx_disable(netdev);

	/* Stop and reset the MAC */
	ftgmac100_stop_hw(priv);
	err = ftgmac100_reset_and_config_mac(priv);
	if (err) {
		/* Not much we can do ... it might come back... */
		netdev_err(netdev, "attempting to continue...\n");
	}

	/* Free all rx and tx buffers */
	ftgmac100_free_buffers(priv);

	/* Setup everything again and restart chip */
	ftgmac100_init_all(priv, true);

	netdev_dbg(netdev, "Reset done !\n");
 bail:
	if (priv->mii_bus)
		mutex_unlock(&priv->mii_bus->mdio_lock);
	if (netdev->phydev)
		mutex_unlock(&netdev->phydev->lock);
	rtnl_unlock();
}

static int ftgmac100_open(struct net_device *netdev)
{
	struct ftgmac100 *priv = netdev_priv(netdev);
	int err;

	/* Allocate ring buffers  */
	err = ftgmac100_alloc_rings(priv);
	if (err) {
		netdev_err(netdev, "Failed to allocate descriptors\n");
		return err;
	}

	/* When using NC-SI we force the speed to 100Mbit/s full duplex,
	 *
	 * Otherwise we leave it set to 0 (no link), the link
	 * message from the PHY layer will handle setting it up to
	 * something else if needed.
	 */
	if (priv->use_ncsi) {
		priv->cur_duplex = DUPLEX_FULL;
		priv->cur_speed = SPEED_100;
	} else {
		priv->cur_duplex = 0;
		priv->cur_speed = 0;
	}

	/* Reset the hardware */
	err = ftgmac100_reset_and_config_mac(priv);
	if (err)
		goto err_hw;

	/* Initialize NAPI */
	netif_napi_add(netdev, &priv->napi, ftgmac100_poll, 64);

	/* Grab our interrupt */
	err = request_irq(netdev->irq, ftgmac100_interrupt, 0, netdev->name, netdev);
	if (err) {
		netdev_err(netdev, "failed to request irq %d\n", netdev->irq);
		goto err_irq;
	}

	/* Start things up */
	err = ftgmac100_init_all(priv, false);
	if (err) {
		netdev_err(netdev, "Failed to allocate packet buffers\n");
		goto err_alloc;
	}

	if (netdev->phydev) {
		/* If we have a PHY, start polling */
		phy_start(netdev->phydev);
	} else if (priv->use_ncsi) {
		/* If using NC-SI, set our carrier on and start the stack */
		netif_carrier_on(netdev);

		/* Start the NCSI device */
		err = ncsi_start_dev(priv->ndev);
		if (err)
			goto err_ncsi;
	}

	return 0;

 err_ncsi:
	napi_disable(&priv->napi);
	netif_stop_queue(netdev);
 err_alloc:
	ftgmac100_free_buffers(priv);
	free_irq(netdev->irq, netdev);
 err_irq:
	netif_napi_del(&priv->napi);
 err_hw:
	iowrite32(0, priv->base + FTGMAC100_OFFSET_IER);
	ftgmac100_free_rings(priv);
	return err;
}

static int ftgmac100_stop(struct net_device *netdev)
{
	struct ftgmac100 *priv = netdev_priv(netdev);

	/* Note about the reset task: We are called with the rtnl lock
	 * held, so we are synchronized against the core of the reset
	 * task. We must not try to synchronously cancel it otherwise
	 * we can deadlock. But since it will test for netif_running()
	 * which has already been cleared by the net core, we don't
	 * anything special to do.
	 */

	/* disable all interrupts */
	iowrite32(0, priv->base + FTGMAC100_OFFSET_IER);

	netif_stop_queue(netdev);
	napi_disable(&priv->napi);
	netif_napi_del(&priv->napi);
	if (netdev->phydev)
		phy_stop(netdev->phydev);
	else if (priv->use_ncsi)
		ncsi_stop_dev(priv->ndev);

	ftgmac100_stop_hw(priv);
	free_irq(netdev->irq, netdev);
	ftgmac100_free_buffers(priv);
	ftgmac100_free_rings(priv);

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
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	map = dma_map_single(priv->dev, skb->data, skb_headlen(skb), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(priv->dev, map))) {
		/* drop packet */
		if (net_ratelimit())
			netdev_err(netdev, "map socket buffer failed\n");

		netdev->stats.tx_dropped++;
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	return ftgmac100_xmit(priv, skb, map);
}

/* optional */
static int ftgmac100_do_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	if (!netdev->phydev)
		return -ENXIO;

	return phy_mii_ioctl(netdev->phydev, ifr, cmd);
}

static const struct net_device_ops ftgmac100_netdev_ops = {
	.ndo_open		= ftgmac100_open,
	.ndo_stop		= ftgmac100_stop,
	.ndo_start_xmit		= ftgmac100_hard_start_xmit,
	.ndo_set_mac_address	= ftgmac100_set_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= ftgmac100_do_ioctl,
};

static int ftgmac100_setup_mdio(struct net_device *netdev)
{
	struct ftgmac100 *priv = netdev_priv(netdev);
	struct platform_device *pdev = to_platform_device(priv->dev);
	int i, err = 0;
	u32 reg;

	/* initialize mdio bus */
	priv->mii_bus = mdiobus_alloc();
	if (!priv->mii_bus)
		return -EIO;

	if (of_machine_is_compatible("aspeed,ast2400") ||
	    of_machine_is_compatible("aspeed,ast2500")) {
		/* This driver supports the old MDIO interface */
		reg = ioread32(priv->base + FTGMAC100_OFFSET_REVR);
		reg &= ~FTGMAC100_REVR_NEW_MDIO_INTERFACE;
		iowrite32(reg, priv->base + FTGMAC100_OFFSET_REVR);
	};

	priv->mii_bus->name = "ftgmac100_mdio";
	snprintf(priv->mii_bus->id, MII_BUS_ID_SIZE, "%s-%d",
		 pdev->name, pdev->id);
	priv->mii_bus->priv = priv->netdev;
	priv->mii_bus->read = ftgmac100_mdiobus_read;
	priv->mii_bus->write = ftgmac100_mdiobus_write;

	for (i = 0; i < PHY_MAX_ADDR; i++)
		priv->mii_bus->irq[i] = PHY_POLL;

	err = mdiobus_register(priv->mii_bus);
	if (err) {
		dev_err(priv->dev, "Cannot register MDIO bus!\n");
		goto err_register_mdiobus;
	}

	err = ftgmac100_mii_probe(priv);
	if (err) {
		dev_err(priv->dev, "MII Probe failed!\n");
		goto err_mii_probe;
	}

	return 0;

err_mii_probe:
	mdiobus_unregister(priv->mii_bus);
err_register_mdiobus:
	mdiobus_free(priv->mii_bus);
	return err;
}

static void ftgmac100_destroy_mdio(struct net_device *netdev)
{
	struct ftgmac100 *priv = netdev_priv(netdev);

	if (!netdev->phydev)
		return;

	phy_disconnect(netdev->phydev);
	mdiobus_unregister(priv->mii_bus);
	mdiobus_free(priv->mii_bus);
}

static void ftgmac100_ncsi_handler(struct ncsi_dev *nd)
{
	if (unlikely(nd->state != ncsi_dev_state_functional))
		return;

	netdev_info(nd->dev, "NCSI interface %s\n",
		    nd->link_up ? "up" : "down");
}

static int ftgmac100_probe(struct platform_device *pdev)
{
	struct resource *res;
	int irq;
	struct net_device *netdev;
	struct ftgmac100 *priv;
	int err = 0;

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

	netdev->ethtool_ops = &ftgmac100_ethtool_ops;
	netdev->netdev_ops = &ftgmac100_netdev_ops;

	platform_set_drvdata(pdev, netdev);

	/* setup private data */
	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->dev = &pdev->dev;
	INIT_WORK(&priv->reset_task, ftgmac100_reset_task);

	spin_lock_init(&priv->tx_lock);

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

	netdev->irq = irq;

	/* MAC address from chip or random one */
	ftgmac100_setup_mac(priv);

	if (of_machine_is_compatible("aspeed,ast2400") ||
	    of_machine_is_compatible("aspeed,ast2500")) {
		priv->rxdes0_edorr_mask = BIT(30);
		priv->txdes0_edotr_mask = BIT(30);
	} else {
		priv->rxdes0_edorr_mask = BIT(15);
		priv->txdes0_edotr_mask = BIT(15);
	}

	if (pdev->dev.of_node &&
	    of_get_property(pdev->dev.of_node, "use-ncsi", NULL)) {
		if (!IS_ENABLED(CONFIG_NET_NCSI)) {
			dev_err(&pdev->dev, "NCSI stack not enabled\n");
			goto err_ncsi_dev;
		}

		dev_info(&pdev->dev, "Using NCSI interface\n");
		priv->use_ncsi = true;
		priv->ndev = ncsi_register_dev(netdev, ftgmac100_ncsi_handler);
		if (!priv->ndev)
			goto err_ncsi_dev;
	} else {
		priv->use_ncsi = false;
		err = ftgmac100_setup_mdio(netdev);
		if (err)
			goto err_setup_mdio;
	}

	/* We have to disable on-chip IP checksum functionality
	 * when NCSI is enabled on the interface. It doesn't work
	 * in that case.
	 */
	netdev->features = NETIF_F_RXCSUM | NETIF_F_IP_CSUM | NETIF_F_GRO;
	if (priv->use_ncsi &&
	    of_get_property(pdev->dev.of_node, "no-hw-checksum", NULL))
		netdev->features &= ~NETIF_F_IP_CSUM;


	/* register network device */
	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register netdev\n");
		goto err_register_netdev;
	}

	netdev_info(netdev, "irq %d, mapped at %p\n", netdev->irq, priv->base);

	return 0;

err_ncsi_dev:
err_register_netdev:
	ftgmac100_destroy_mdio(netdev);
err_setup_mdio:
	iounmap(priv->base);
err_ioremap:
	release_resource(priv->res);
err_req_mem:
	netif_napi_del(&priv->napi);
	free_netdev(netdev);
err_alloc_etherdev:
	return err;
}

static int ftgmac100_remove(struct platform_device *pdev)
{
	struct net_device *netdev;
	struct ftgmac100 *priv;

	netdev = platform_get_drvdata(pdev);
	priv = netdev_priv(netdev);

	unregister_netdev(netdev);

	/* There's a small chance the reset task will have been re-queued,
	 * during stop, make sure it's gone before we free the structure.
	 */
	cancel_work_sync(&priv->reset_task);

	ftgmac100_destroy_mdio(netdev);

	iounmap(priv->base);
	release_resource(priv->res);

	netif_napi_del(&priv->napi);
	free_netdev(netdev);
	return 0;
}

static const struct of_device_id ftgmac100_of_match[] = {
	{ .compatible = "faraday,ftgmac100" },
	{ }
};
MODULE_DEVICE_TABLE(of, ftgmac100_of_match);

static struct platform_driver ftgmac100_driver = {
	.probe	= ftgmac100_probe,
	.remove	= ftgmac100_remove,
	.driver	= {
		.name		= DRV_NAME,
		.of_match_table	= ftgmac100_of_match,
	},
};
module_platform_driver(ftgmac100_driver);

MODULE_AUTHOR("Po-Yu Chuang <ratbert@faraday-tech.com>");
MODULE_DESCRIPTION("FTGMAC100 driver");
MODULE_LICENSE("GPL");
