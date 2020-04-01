// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Faraday FTGMAC100 Gigabit Ethernet
 *
 * (C) Copyright 2009-2011 Faraday Technology
 * Po-Yu Chuang <ratbert@faraday-tech.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/crc32.h>
#include <linux/if_vlan.h>
#include <linux/of_net.h>
#include <net/ip.h>
#include <net/ncsi.h>

#include "ftgmac100.h"

#define DRV_NAME	"ftgmac100"

/* Arbitrary values, I am not sure the HW has limits */
#define MAX_RX_QUEUE_ENTRIES	1024
#define MAX_TX_QUEUE_ENTRIES	1024
#define MIN_RX_QUEUE_ENTRIES	32
#define MIN_TX_QUEUE_ENTRIES	32

/* Defaults */
#define DEF_RX_QUEUE_ENTRIES	128
#define DEF_TX_QUEUE_ENTRIES	128

#define MAX_PKT_SIZE		1536
#define RX_BUF_SIZE		MAX_PKT_SIZE	/* must be smaller than 0x3fff */

/* Min number of tx ring entries before stopping queue */
#define TX_THRESHOLD		(MAX_SKB_FRAGS + 1)

#define FTGMAC_100MHZ		100000000
#define FTGMAC_25MHZ		25000000

struct ftgmac100 {
	/* Registers */
	struct resource *res;
	void __iomem *base;

	/* Rx ring */
	unsigned int rx_q_entries;
	struct ftgmac100_rxdes *rxdes;
	dma_addr_t rxdes_dma;
	struct sk_buff **rx_skbs;
	unsigned int rx_pointer;
	u32 rxdes0_edorr_mask;

	/* Tx ring */
	unsigned int tx_q_entries;
	struct ftgmac100_txdes *txdes;
	dma_addr_t txdes_dma;
	struct sk_buff **tx_skbs;
	unsigned int tx_clean_pointer;
	unsigned int tx_pointer;
	u32 txdes0_edotr_mask;

	/* Used to signal the reset task of ring change request */
	unsigned int new_rx_q_entries;
	unsigned int new_tx_q_entries;

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
	struct clk *clk;

	/* AST2500/AST2600 RMII ref clock gate */
	struct clk *rclk;

	/* Link management */
	int cur_speed;
	int cur_duplex;
	bool use_ncsi;

	/* Multicast filter settings */
	u32 maht0;
	u32 maht1;

	/* Flow control settings */
	bool tx_pause;
	bool rx_pause;
	bool aneg_pause;

	/* Misc */
	bool need_mac_restart;
	bool is_aspeed;
};

static int ftgmac100_reset_mac(struct ftgmac100 *priv, u32 maccr)
{
	struct net_device *netdev = priv->netdev;
	int i;

	/* NOTE: reset clears all registers */
	iowrite32(maccr, priv->base + FTGMAC100_OFFSET_MACCR);
	iowrite32(maccr | FTGMAC100_MACCR_SW_RST,
		  priv->base + FTGMAC100_OFFSET_MACCR);
	for (i = 0; i < 200; i++) {
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

	/* The doc says reset twice with 10us interval */
	if (ftgmac100_reset_mac(priv, maccr))
		return -EIO;
	usleep_range(10, 1000);
	return ftgmac100_reset_mac(priv, maccr);
}

static void ftgmac100_write_mac_addr(struct ftgmac100 *priv, const u8 *mac)
{
	unsigned int maddr = mac[0] << 8 | mac[1];
	unsigned int laddr = mac[2] << 24 | mac[3] << 16 | mac[4] << 8 | mac[5];

	iowrite32(maddr, priv->base + FTGMAC100_OFFSET_MAC_MADR);
	iowrite32(laddr, priv->base + FTGMAC100_OFFSET_MAC_LADR);
}

static void ftgmac100_initial_mac(struct ftgmac100 *priv)
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
	ftgmac100_write_mac_addr(netdev_priv(dev), dev->dev_addr);

	return 0;
}

static void ftgmac100_config_pause(struct ftgmac100 *priv)
{
	u32 fcr = FTGMAC100_FCR_PAUSE_TIME(16);

	/* Throttle tx queue when receiving pause frames */
	if (priv->rx_pause)
		fcr |= FTGMAC100_FCR_FC_EN;

	/* Enables sending pause frames when the RX queue is past a
	 * certain threshold.
	 */
	if (priv->tx_pause)
		fcr |= FTGMAC100_FCR_FCTHR_EN;

	iowrite32(fcr, priv->base + FTGMAC100_OFFSET_FCR);
}

static void ftgmac100_init_hw(struct ftgmac100 *priv)
{
	u32 reg, rfifo_sz, tfifo_sz;

	/* Clear stale interrupts */
	reg = ioread32(priv->base + FTGMAC100_OFFSET_ISR);
	iowrite32(reg, priv->base + FTGMAC100_OFFSET_ISR);

	/* Setup RX ring buffer base */
	iowrite32(priv->rxdes_dma, priv->base + FTGMAC100_OFFSET_RXR_BADR);

	/* Setup TX ring buffer base */
	iowrite32(priv->txdes_dma, priv->base + FTGMAC100_OFFSET_NPTXR_BADR);

	/* Configure RX buffer size */
	iowrite32(FTGMAC100_RBSR_SIZE(RX_BUF_SIZE),
		  priv->base + FTGMAC100_OFFSET_RBSR);

	/* Set RX descriptor autopoll */
	iowrite32(FTGMAC100_APTC_RXPOLL_CNT(1),
		  priv->base + FTGMAC100_OFFSET_APTC);

	/* Write MAC address */
	ftgmac100_write_mac_addr(priv, priv->netdev->dev_addr);

	/* Write multicast filter */
	iowrite32(priv->maht0, priv->base + FTGMAC100_OFFSET_MAHT0);
	iowrite32(priv->maht1, priv->base + FTGMAC100_OFFSET_MAHT1);

	/* Configure descriptor sizes and increase burst sizes according
	 * to values in Aspeed SDK. The FIFO arbitration is enabled and
	 * the thresholds set based on the recommended values in the
	 * AST2400 specification.
	 */
	iowrite32(FTGMAC100_DBLAC_RXDES_SIZE(2) |   /* 2*8 bytes RX descs */
		  FTGMAC100_DBLAC_TXDES_SIZE(2) |   /* 2*8 bytes TX descs */
		  FTGMAC100_DBLAC_RXBURST_SIZE(3) | /* 512 bytes max RX bursts */
		  FTGMAC100_DBLAC_TXBURST_SIZE(3) | /* 512 bytes max TX bursts */
		  FTGMAC100_DBLAC_RX_THR_EN |       /* Enable fifo threshold arb */
		  FTGMAC100_DBLAC_RXFIFO_HTHR(6) |  /* 6/8 of FIFO high threshold */
		  FTGMAC100_DBLAC_RXFIFO_LTHR(2),   /* 2/8 of FIFO low threshold */
		  priv->base + FTGMAC100_OFFSET_DBLAC);

	/* Interrupt mitigation configured for 1 interrupt/packet. HW interrupt
	 * mitigation doesn't seem to provide any benefit with NAPI so leave
	 * it at that.
	 */
	iowrite32(FTGMAC100_ITC_RXINT_THR(1) |
		  FTGMAC100_ITC_TXINT_THR(1),
		  priv->base + FTGMAC100_OFFSET_ITC);

	/* Configure FIFO sizes in the TPAFCR register */
	reg = ioread32(priv->base + FTGMAC100_OFFSET_FEAR);
	rfifo_sz = reg & 0x00000007;
	tfifo_sz = (reg >> 3) & 0x00000007;
	reg = ioread32(priv->base + FTGMAC100_OFFSET_TPAFCR);
	reg &= ~0x3f000000;
	reg |= (tfifo_sz << 27);
	reg |= (rfifo_sz << 24);
	iowrite32(reg, priv->base + FTGMAC100_OFFSET_TPAFCR);
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
	if (priv->netdev->flags & IFF_PROMISC)
		maccr |= FTGMAC100_MACCR_RX_ALL;
	if (priv->netdev->flags & IFF_ALLMULTI)
		maccr |= FTGMAC100_MACCR_RX_MULTIPKT;
	else if (netdev_mc_count(priv->netdev))
		maccr |= FTGMAC100_MACCR_HT_MULTI_EN;

	/* Vlan filtering enabled */
	if (priv->netdev->features & NETIF_F_HW_VLAN_CTAG_RX)
		maccr |= FTGMAC100_MACCR_RM_VLAN;

	/* Hit the HW */
	iowrite32(maccr, priv->base + FTGMAC100_OFFSET_MACCR);
}

static void ftgmac100_stop_hw(struct ftgmac100 *priv)
{
	iowrite32(0, priv->base + FTGMAC100_OFFSET_MACCR);
}

static void ftgmac100_calc_mc_hash(struct ftgmac100 *priv)
{
	struct netdev_hw_addr *ha;

	priv->maht1 = 0;
	priv->maht0 = 0;
	netdev_for_each_mc_addr(ha, priv->netdev) {
		u32 crc_val = ether_crc_le(ETH_ALEN, ha->addr);

		crc_val = (~(crc_val >> 2)) & 0x3f;
		if (crc_val >= 32)
			priv->maht1 |= 1ul << (crc_val - 32);
		else
			priv->maht0 |= 1ul << (crc_val);
	}
}

static void ftgmac100_set_rx_mode(struct net_device *netdev)
{
	struct ftgmac100 *priv = netdev_priv(netdev);

	/* Setup the hash filter */
	ftgmac100_calc_mc_hash(priv);

	/* Interface down ? that's all there is to do */
	if (!netif_running(netdev))
		return;

	/* Update the HW */
	iowrite32(priv->maht0, priv->base + FTGMAC100_OFFSET_MAHT0);
	iowrite32(priv->maht1, priv->base + FTGMAC100_OFFSET_MAHT1);

	/* Reconfigure MACCR */
	ftgmac100_start_hw(priv);
}

static int ftgmac100_alloc_rx_buf(struct ftgmac100 *priv, unsigned int entry,
				  struct ftgmac100_rxdes *rxdes, gfp_t gfp)
{
	struct net_device *netdev = priv->netdev;
	struct sk_buff *skb;
	dma_addr_t map;
	int err = 0;

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
	if (entry == (priv->rx_q_entries - 1))
		rxdes->rxdes0 = cpu_to_le32(priv->rxdes0_edorr_mask);
	else
		rxdes->rxdes0 = 0;

	return err;
}

static unsigned int ftgmac100_next_rx_pointer(struct ftgmac100 *priv,
					      unsigned int pointer)
{
	return (pointer + 1) & (priv->rx_q_entries - 1);
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
	rxdes = &priv->rxdes[pointer];

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

	/* Extract vlan tag */
	if ((netdev->features & NETIF_F_HW_VLAN_CTAG_RX) &&
	    (csum_vlan & FTGMAC100_RXDES1_VLANTAG_AVAIL))
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       csum_vlan & 0xffff);

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
	priv->rx_pointer = ftgmac100_next_rx_pointer(priv, pointer);

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
	priv->rx_pointer = ftgmac100_next_rx_pointer(priv, pointer);
	netdev->stats.rx_dropped++;
	return true;
}

static u32 ftgmac100_base_tx_ctlstat(struct ftgmac100 *priv,
				     unsigned int index)
{
	if (index == (priv->tx_q_entries - 1))
		return priv->txdes0_edotr_mask;
	else
		return 0;
}

static unsigned int ftgmac100_next_tx_pointer(struct ftgmac100 *priv,
					      unsigned int pointer)
{
	return (pointer + 1) & (priv->tx_q_entries - 1);
}

static u32 ftgmac100_tx_buf_avail(struct ftgmac100 *priv)
{
	/* Returns the number of available slots in the TX queue
	 *
	 * This always leaves one free slot so we don't have to
	 * worry about empty vs. full, and this simplifies the
	 * test for ftgmac100_tx_buf_cleanable() below
	 */
	return (priv->tx_clean_pointer - priv->tx_pointer - 1) &
		(priv->tx_q_entries - 1);
}

static bool ftgmac100_tx_buf_cleanable(struct ftgmac100 *priv)
{
	return priv->tx_pointer != priv->tx_clean_pointer;
}

static void ftgmac100_free_tx_packet(struct ftgmac100 *priv,
				     unsigned int pointer,
				     struct sk_buff *skb,
				     struct ftgmac100_txdes *txdes,
				     u32 ctl_stat)
{
	dma_addr_t map = le32_to_cpu(txdes->txdes3);
	size_t len;

	if (ctl_stat & FTGMAC100_TXDES0_FTS) {
		len = skb_headlen(skb);
		dma_unmap_single(priv->dev, map, len, DMA_TO_DEVICE);
	} else {
		len = FTGMAC100_TXDES0_TXBUF_SIZE(ctl_stat);
		dma_unmap_page(priv->dev, map, len, DMA_TO_DEVICE);
	}

	/* Free SKB on last segment */
	if (ctl_stat & FTGMAC100_TXDES0_LTS)
		dev_kfree_skb(skb);
	priv->tx_skbs[pointer] = NULL;
}

static bool ftgmac100_tx_complete_packet(struct ftgmac100 *priv)
{
	struct net_device *netdev = priv->netdev;
	struct ftgmac100_txdes *txdes;
	struct sk_buff *skb;
	unsigned int pointer;
	u32 ctl_stat;

	pointer = priv->tx_clean_pointer;
	txdes = &priv->txdes[pointer];

	ctl_stat = le32_to_cpu(txdes->txdes0);
	if (ctl_stat & FTGMAC100_TXDES0_TXDMA_OWN)
		return false;

	skb = priv->tx_skbs[pointer];
	netdev->stats.tx_packets++;
	netdev->stats.tx_bytes += skb->len;
	ftgmac100_free_tx_packet(priv, pointer, skb, txdes, ctl_stat);
	txdes->txdes0 = cpu_to_le32(ctl_stat & priv->txdes0_edotr_mask);

	priv->tx_clean_pointer = ftgmac100_next_tx_pointer(priv, pointer);

	return true;
}

static void ftgmac100_tx_complete(struct ftgmac100 *priv)
{
	struct net_device *netdev = priv->netdev;

	/* Process all completed packets */
	while (ftgmac100_tx_buf_cleanable(priv) &&
	       ftgmac100_tx_complete_packet(priv))
		;

	/* Restart queue if needed */
	smp_mb();
	if (unlikely(netif_queue_stopped(netdev) &&
		     ftgmac100_tx_buf_avail(priv) >= TX_THRESHOLD)) {
		struct netdev_queue *txq;

		txq = netdev_get_tx_queue(netdev, 0);
		__netif_tx_lock(txq, smp_processor_id());
		if (netif_queue_stopped(netdev) &&
		    ftgmac100_tx_buf_avail(priv) >= TX_THRESHOLD)
			netif_wake_queue(netdev);
		__netif_tx_unlock(txq);
	}
}

static bool ftgmac100_prep_tx_csum(struct sk_buff *skb, u32 *csum_vlan)
{
	if (skb->protocol == cpu_to_be16(ETH_P_IP)) {
		u8 ip_proto = ip_hdr(skb)->protocol;

		*csum_vlan |= FTGMAC100_TXDES1_IP_CHKSUM;
		switch(ip_proto) {
		case IPPROTO_TCP:
			*csum_vlan |= FTGMAC100_TXDES1_TCP_CHKSUM;
			return true;
		case IPPROTO_UDP:
			*csum_vlan |= FTGMAC100_TXDES1_UDP_CHKSUM;
			return true;
		case IPPROTO_IP:
			return true;
		}
	}
	return skb_checksum_help(skb) == 0;
}

static netdev_tx_t ftgmac100_hard_start_xmit(struct sk_buff *skb,
					     struct net_device *netdev)
{
	struct ftgmac100 *priv = netdev_priv(netdev);
	struct ftgmac100_txdes *txdes, *first;
	unsigned int pointer, nfrags, len, i, j;
	u32 f_ctl_stat, ctl_stat, csum_vlan;
	dma_addr_t map;

	/* The HW doesn't pad small frames */
	if (eth_skb_pad(skb)) {
		netdev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	/* Reject oversize packets */
	if (unlikely(skb->len > MAX_PKT_SIZE)) {
		if (net_ratelimit())
			netdev_dbg(netdev, "tx packet too big\n");
		goto drop;
	}

	/* Do we have a limit on #fragments ? I yet have to get a reply
	 * from Aspeed. If there's one I haven't hit it.
	 */
	nfrags = skb_shinfo(skb)->nr_frags;

	/* Setup HW checksumming */
	csum_vlan = 0;
	if (skb->ip_summed == CHECKSUM_PARTIAL &&
	    !ftgmac100_prep_tx_csum(skb, &csum_vlan))
		goto drop;

	/* Add VLAN tag */
	if (skb_vlan_tag_present(skb)) {
		csum_vlan |= FTGMAC100_TXDES1_INS_VLANTAG;
		csum_vlan |= skb_vlan_tag_get(skb) & 0xffff;
	}

	/* Get header len */
	len = skb_headlen(skb);

	/* Map the packet head */
	map = dma_map_single(priv->dev, skb->data, len, DMA_TO_DEVICE);
	if (dma_mapping_error(priv->dev, map)) {
		if (net_ratelimit())
			netdev_err(netdev, "map tx packet head failed\n");
		goto drop;
	}

	/* Grab the next free tx descriptor */
	pointer = priv->tx_pointer;
	txdes = first = &priv->txdes[pointer];

	/* Setup it up with the packet head. Don't write the head to the
	 * ring just yet
	 */
	priv->tx_skbs[pointer] = skb;
	f_ctl_stat = ftgmac100_base_tx_ctlstat(priv, pointer);
	f_ctl_stat |= FTGMAC100_TXDES0_TXDMA_OWN;
	f_ctl_stat |= FTGMAC100_TXDES0_TXBUF_SIZE(len);
	f_ctl_stat |= FTGMAC100_TXDES0_FTS;
	if (nfrags == 0)
		f_ctl_stat |= FTGMAC100_TXDES0_LTS;
	txdes->txdes3 = cpu_to_le32(map);
	txdes->txdes1 = cpu_to_le32(csum_vlan);

	/* Next descriptor */
	pointer = ftgmac100_next_tx_pointer(priv, pointer);

	/* Add the fragments */
	for (i = 0; i < nfrags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		len = skb_frag_size(frag);

		/* Map it */
		map = skb_frag_dma_map(priv->dev, frag, 0, len,
				       DMA_TO_DEVICE);
		if (dma_mapping_error(priv->dev, map))
			goto dma_err;

		/* Setup descriptor */
		priv->tx_skbs[pointer] = skb;
		txdes = &priv->txdes[pointer];
		ctl_stat = ftgmac100_base_tx_ctlstat(priv, pointer);
		ctl_stat |= FTGMAC100_TXDES0_TXDMA_OWN;
		ctl_stat |= FTGMAC100_TXDES0_TXBUF_SIZE(len);
		if (i == (nfrags - 1))
			ctl_stat |= FTGMAC100_TXDES0_LTS;
		txdes->txdes0 = cpu_to_le32(ctl_stat);
		txdes->txdes1 = 0;
		txdes->txdes3 = cpu_to_le32(map);

		/* Next one */
		pointer = ftgmac100_next_tx_pointer(priv, pointer);
	}

	/* Order the previous packet and descriptor udpates
	 * before setting the OWN bit on the first descriptor.
	 */
	dma_wmb();
	first->txdes0 = cpu_to_le32(f_ctl_stat);

	/* Update next TX pointer */
	priv->tx_pointer = pointer;

	/* If there isn't enough room for all the fragments of a new packet
	 * in the TX ring, stop the queue. The sequence below is race free
	 * vs. a concurrent restart in ftgmac100_poll()
	 */
	if (unlikely(ftgmac100_tx_buf_avail(priv) < TX_THRESHOLD)) {
		netif_stop_queue(netdev);
		/* Order the queue stop with the test below */
		smp_mb();
		if (ftgmac100_tx_buf_avail(priv) >= TX_THRESHOLD)
			netif_wake_queue(netdev);
	}

	/* Poke transmitter to read the updated TX descriptors */
	iowrite32(1, priv->base + FTGMAC100_OFFSET_NPTXPD);

	return NETDEV_TX_OK;

 dma_err:
	if (net_ratelimit())
		netdev_err(netdev, "map tx fragment failed\n");

	/* Free head */
	pointer = priv->tx_pointer;
	ftgmac100_free_tx_packet(priv, pointer, skb, first, f_ctl_stat);
	first->txdes0 = cpu_to_le32(f_ctl_stat & priv->txdes0_edotr_mask);

	/* Then all fragments */
	for (j = 0; j < i; j++) {
		pointer = ftgmac100_next_tx_pointer(priv, pointer);
		txdes = &priv->txdes[pointer];
		ctl_stat = le32_to_cpu(txdes->txdes0);
		ftgmac100_free_tx_packet(priv, pointer, skb, txdes, ctl_stat);
		txdes->txdes0 = cpu_to_le32(ctl_stat & priv->txdes0_edotr_mask);
	}

	/* This cannot be reached if we successfully mapped the
	 * last fragment, so we know ftgmac100_free_tx_packet()
	 * hasn't freed the skb yet.
	 */
 drop:
	/* Drop the packet */
	dev_kfree_skb_any(skb);
	netdev->stats.tx_dropped++;

	return NETDEV_TX_OK;
}

static void ftgmac100_free_buffers(struct ftgmac100 *priv)
{
	int i;

	/* Free all RX buffers */
	for (i = 0; i < priv->rx_q_entries; i++) {
		struct ftgmac100_rxdes *rxdes = &priv->rxdes[i];
		struct sk_buff *skb = priv->rx_skbs[i];
		dma_addr_t map = le32_to_cpu(rxdes->rxdes3);

		if (!skb)
			continue;

		priv->rx_skbs[i] = NULL;
		dma_unmap_single(priv->dev, map, RX_BUF_SIZE, DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
	}

	/* Free all TX buffers */
	for (i = 0; i < priv->tx_q_entries; i++) {
		struct ftgmac100_txdes *txdes = &priv->txdes[i];
		struct sk_buff *skb = priv->tx_skbs[i];

		if (!skb)
			continue;
		ftgmac100_free_tx_packet(priv, i, skb, txdes,
					 le32_to_cpu(txdes->txdes0));
	}
}

static void ftgmac100_free_rings(struct ftgmac100 *priv)
{
	/* Free skb arrays */
	kfree(priv->rx_skbs);
	kfree(priv->tx_skbs);

	/* Free descriptors */
	if (priv->rxdes)
		dma_free_coherent(priv->dev, MAX_RX_QUEUE_ENTRIES *
				  sizeof(struct ftgmac100_rxdes),
				  priv->rxdes, priv->rxdes_dma);
	priv->rxdes = NULL;

	if (priv->txdes)
		dma_free_coherent(priv->dev, MAX_TX_QUEUE_ENTRIES *
				  sizeof(struct ftgmac100_txdes),
				  priv->txdes, priv->txdes_dma);
	priv->txdes = NULL;

	/* Free scratch packet buffer */
	if (priv->rx_scratch)
		dma_free_coherent(priv->dev, RX_BUF_SIZE,
				  priv->rx_scratch, priv->rx_scratch_dma);
}

static int ftgmac100_alloc_rings(struct ftgmac100 *priv)
{
	/* Allocate skb arrays */
	priv->rx_skbs = kcalloc(MAX_RX_QUEUE_ENTRIES, sizeof(void *),
				GFP_KERNEL);
	if (!priv->rx_skbs)
		return -ENOMEM;
	priv->tx_skbs = kcalloc(MAX_TX_QUEUE_ENTRIES, sizeof(void *),
				GFP_KERNEL);
	if (!priv->tx_skbs)
		return -ENOMEM;

	/* Allocate descriptors */
	priv->rxdes = dma_alloc_coherent(priv->dev,
					 MAX_RX_QUEUE_ENTRIES * sizeof(struct ftgmac100_rxdes),
					 &priv->rxdes_dma, GFP_KERNEL);
	if (!priv->rxdes)
		return -ENOMEM;
	priv->txdes = dma_alloc_coherent(priv->dev,
					 MAX_TX_QUEUE_ENTRIES * sizeof(struct ftgmac100_txdes),
					 &priv->txdes_dma, GFP_KERNEL);
	if (!priv->txdes)
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
	struct ftgmac100_rxdes *rxdes = NULL;
	struct ftgmac100_txdes *txdes = NULL;
	int i;

	/* Update entries counts */
	priv->rx_q_entries = priv->new_rx_q_entries;
	priv->tx_q_entries = priv->new_tx_q_entries;

	if (WARN_ON(priv->rx_q_entries < MIN_RX_QUEUE_ENTRIES))
		return;

	/* Initialize RX ring */
	for (i = 0; i < priv->rx_q_entries; i++) {
		rxdes = &priv->rxdes[i];
		rxdes->rxdes0 = 0;
		rxdes->rxdes3 = cpu_to_le32(priv->rx_scratch_dma);
	}
	/* Mark the end of the ring */
	rxdes->rxdes0 |= cpu_to_le32(priv->rxdes0_edorr_mask);

	if (WARN_ON(priv->tx_q_entries < MIN_RX_QUEUE_ENTRIES))
		return;

	/* Initialize TX ring */
	for (i = 0; i < priv->tx_q_entries; i++) {
		txdes = &priv->txdes[i];
		txdes->txdes0 = 0;
	}
	txdes->txdes0 |= cpu_to_le32(priv->txdes0_edotr_mask);
}

static int ftgmac100_alloc_rx_buffers(struct ftgmac100 *priv)
{
	int i;

	for (i = 0; i < priv->rx_q_entries; i++) {
		struct ftgmac100_rxdes *rxdes = &priv->rxdes[i];

		if (ftgmac100_alloc_rx_buf(priv, i, rxdes, GFP_KERNEL))
			return -ENOMEM;
	}
	return 0;
}

static void ftgmac100_adjust_link(struct net_device *netdev)
{
	struct ftgmac100 *priv = netdev_priv(netdev);
	struct phy_device *phydev = netdev->phydev;
	bool tx_pause, rx_pause;
	int new_speed;

	/* We store "no link" as speed 0 */
	if (!phydev->link)
		new_speed = 0;
	else
		new_speed = phydev->speed;

	/* Grab pause settings from PHY if configured to do so */
	if (priv->aneg_pause) {
		rx_pause = tx_pause = phydev->pause;
		if (phydev->asym_pause)
			tx_pause = !rx_pause;
	} else {
		rx_pause = priv->rx_pause;
		tx_pause = priv->tx_pause;
	}

	/* Link hasn't changed, do nothing */
	if (phydev->speed == priv->cur_speed &&
	    phydev->duplex == priv->cur_duplex &&
	    rx_pause == priv->rx_pause &&
	    tx_pause == priv->tx_pause)
		return;

	/* Print status if we have a link or we had one and just lost it,
	 * don't print otherwise.
	 */
	if (new_speed || priv->cur_speed)
		phy_print_status(phydev);

	priv->cur_speed = new_speed;
	priv->cur_duplex = phydev->duplex;
	priv->rx_pause = rx_pause;
	priv->tx_pause = tx_pause;

	/* Link is down, do nothing else */
	if (!new_speed)
		return;

	/* Disable all interrupts */
	iowrite32(0, priv->base + FTGMAC100_OFFSET_IER);

	/* Reset the adapter asynchronously */
	schedule_work(&priv->reset_task);
}

static int ftgmac100_mii_probe(struct ftgmac100 *priv, phy_interface_t intf)
{
	struct net_device *netdev = priv->netdev;
	struct phy_device *phydev;

	phydev = phy_find_first(priv->mii_bus);
	if (!phydev) {
		netdev_info(netdev, "%s: no PHY found\n", netdev->name);
		return -ENODEV;
	}

	phydev = phy_connect(netdev, phydev_name(phydev),
			     &ftgmac100_adjust_link, intf);

	if (IS_ERR(phydev)) {
		netdev_err(netdev, "%s: Could not attach to PHY\n", netdev->name);
		return PTR_ERR(phydev);
	}

	/* Indicate that we support PAUSE frames (see comment in
	 * Documentation/networking/phy.rst)
	 */
	phy_support_asym_pause(phydev);

	/* Display what we found */
	phy_attached_info(phydev);

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
	strlcpy(info->bus_info, dev_name(&netdev->dev), sizeof(info->bus_info));
}

static void ftgmac100_get_ringparam(struct net_device *netdev,
				    struct ethtool_ringparam *ering)
{
	struct ftgmac100 *priv = netdev_priv(netdev);

	memset(ering, 0, sizeof(*ering));
	ering->rx_max_pending = MAX_RX_QUEUE_ENTRIES;
	ering->tx_max_pending = MAX_TX_QUEUE_ENTRIES;
	ering->rx_pending = priv->rx_q_entries;
	ering->tx_pending = priv->tx_q_entries;
}

static int ftgmac100_set_ringparam(struct net_device *netdev,
				   struct ethtool_ringparam *ering)
{
	struct ftgmac100 *priv = netdev_priv(netdev);

	if (ering->rx_pending > MAX_RX_QUEUE_ENTRIES ||
	    ering->tx_pending > MAX_TX_QUEUE_ENTRIES ||
	    ering->rx_pending < MIN_RX_QUEUE_ENTRIES ||
	    ering->tx_pending < MIN_TX_QUEUE_ENTRIES ||
	    !is_power_of_2(ering->rx_pending) ||
	    !is_power_of_2(ering->tx_pending))
		return -EINVAL;

	priv->new_rx_q_entries = ering->rx_pending;
	priv->new_tx_q_entries = ering->tx_pending;
	if (netif_running(netdev))
		schedule_work(&priv->reset_task);

	return 0;
}

static void ftgmac100_get_pauseparam(struct net_device *netdev,
				     struct ethtool_pauseparam *pause)
{
	struct ftgmac100 *priv = netdev_priv(netdev);

	pause->autoneg = priv->aneg_pause;
	pause->tx_pause = priv->tx_pause;
	pause->rx_pause = priv->rx_pause;
}

static int ftgmac100_set_pauseparam(struct net_device *netdev,
				    struct ethtool_pauseparam *pause)
{
	struct ftgmac100 *priv = netdev_priv(netdev);
	struct phy_device *phydev = netdev->phydev;

	priv->aneg_pause = pause->autoneg;
	priv->tx_pause = pause->tx_pause;
	priv->rx_pause = pause->rx_pause;

	if (phydev)
		phy_set_asym_pause(phydev, pause->rx_pause, pause->tx_pause);

	if (netif_running(netdev)) {
		if (!(phydev && priv->aneg_pause))
			ftgmac100_config_pause(priv);
	}

	return 0;
}

static const struct ethtool_ops ftgmac100_ethtool_ops = {
	.get_drvinfo		= ftgmac100_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.nway_reset		= phy_ethtool_nway_reset,
	.get_ringparam		= ftgmac100_get_ringparam,
	.set_ringparam		= ftgmac100_set_ringparam,
	.get_pauseparam		= ftgmac100_get_pauseparam,
	.set_pauseparam		= ftgmac100_set_pauseparam,
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
	struct ftgmac100_rxdes *rxdes = &priv->rxdes[priv->rx_pointer];

	/* Do we have a packet ? */
	return !!(rxdes->rxdes0 & cpu_to_le32(FTGMAC100_RXDES0_RXPKT_RDY));
}

static int ftgmac100_poll(struct napi_struct *napi, int budget)
{
	struct ftgmac100 *priv = container_of(napi, struct ftgmac100, napi);
	int work_done = 0;
	bool more;

	/* Handle TX completions */
	if (ftgmac100_tx_buf_cleanable(priv))
		ftgmac100_tx_complete(priv);

	/* Handle RX packets */
	do {
		more = ftgmac100_rx_packet(priv, &work_done);
	} while (more && work_done < budget);


	/* The interrupt is telling us to kick the MAC back to life
	 * after an RX overflow
	 */
	if (unlikely(priv->need_mac_restart)) {
		ftgmac100_start_hw(priv);

		/* Re-enable "bad" interrupts */
		iowrite32(FTGMAC100_INT_BAD,
			  priv->base + FTGMAC100_OFFSET_IER);
	}

	/* As long as we are waiting for transmit packets to be
	 * completed we keep NAPI going
	 */
	if (ftgmac100_tx_buf_cleanable(priv))
		work_done = budget;

	if (work_done < budget) {
		/* We are about to re-enable all interrupts. However
		 * the HW has been latching RX/TX packet interrupts while
		 * they were masked. So we clear them first, then we need
		 * to re-check if there's something to process
		 */
		iowrite32(FTGMAC100_INT_RXTX,
			  priv->base + FTGMAC100_OFFSET_ISR);

		/* Push the above (and provides a barrier vs. subsequent
		 * reads of the descriptor).
		 */
		ioread32(priv->base + FTGMAC100_OFFSET_ISR);

		/* Check RX and TX descriptors for more work to do */
		if (ftgmac100_check_rx(priv) ||
		    ftgmac100_tx_buf_cleanable(priv))
			return budget;

		/* deschedule NAPI */
		napi_complete(napi);

		/* enable all interrupts */
		iowrite32(FTGMAC100_INT_ALL,
			  priv->base + FTGMAC100_OFFSET_IER);
	}

	return work_done;
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
	ftgmac100_config_pause(priv);
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

static void ftgmac100_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct ftgmac100 *priv = netdev_priv(netdev);

	/* Disable all interrupts */
	iowrite32(0, priv->base + FTGMAC100_OFFSET_IER);

	/* Do the reset outside of interrupt context */
	schedule_work(&priv->reset_task);
}

static int ftgmac100_set_features(struct net_device *netdev,
				  netdev_features_t features)
{
	struct ftgmac100 *priv = netdev_priv(netdev);
	netdev_features_t changed = netdev->features ^ features;

	if (!netif_running(netdev))
		return 0;

	/* Update the vlan filtering bit */
	if (changed & NETIF_F_HW_VLAN_CTAG_RX) {
		u32 maccr;

		maccr = ioread32(priv->base + FTGMAC100_OFFSET_MACCR);
		if (priv->netdev->features & NETIF_F_HW_VLAN_CTAG_RX)
			maccr |= FTGMAC100_MACCR_RM_VLAN;
		else
			maccr &= ~FTGMAC100_MACCR_RM_VLAN;
		iowrite32(maccr, priv->base + FTGMAC100_OFFSET_MACCR);
	}

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void ftgmac100_poll_controller(struct net_device *netdev)
{
	unsigned long flags;

	local_irq_save(flags);
	ftgmac100_interrupt(netdev->irq, netdev);
	local_irq_restore(flags);
}
#endif

static const struct net_device_ops ftgmac100_netdev_ops = {
	.ndo_open		= ftgmac100_open,
	.ndo_stop		= ftgmac100_stop,
	.ndo_start_xmit		= ftgmac100_hard_start_xmit,
	.ndo_set_mac_address	= ftgmac100_set_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= phy_do_ioctl,
	.ndo_tx_timeout		= ftgmac100_tx_timeout,
	.ndo_set_rx_mode	= ftgmac100_set_rx_mode,
	.ndo_set_features	= ftgmac100_set_features,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= ftgmac100_poll_controller,
#endif
	.ndo_vlan_rx_add_vid	= ncsi_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= ncsi_vlan_rx_kill_vid,
};

static int ftgmac100_setup_mdio(struct net_device *netdev)
{
	struct ftgmac100 *priv = netdev_priv(netdev);
	struct platform_device *pdev = to_platform_device(priv->dev);
	phy_interface_t phy_intf = PHY_INTERFACE_MODE_RGMII;
	struct device_node *np = pdev->dev.of_node;
	int i, err = 0;
	u32 reg;

	/* initialize mdio bus */
	priv->mii_bus = mdiobus_alloc();
	if (!priv->mii_bus)
		return -EIO;

	if (of_device_is_compatible(np, "aspeed,ast2400-mac") ||
	    of_device_is_compatible(np, "aspeed,ast2500-mac")) {
		/* The AST2600 has a separate MDIO controller */

		/* For the AST2400 and AST2500 this driver only supports the
		 * old MDIO interface
		 */
		reg = ioread32(priv->base + FTGMAC100_OFFSET_REVR);
		reg &= ~FTGMAC100_REVR_NEW_MDIO_INTERFACE;
		iowrite32(reg, priv->base + FTGMAC100_OFFSET_REVR);
	}

	/* Get PHY mode from device-tree */
	if (np) {
		/* Default to RGMII. It's a gigabit part after all */
		err = of_get_phy_mode(np, &phy_intf);
		if (err)
			phy_intf = PHY_INTERFACE_MODE_RGMII;

		/* Aspeed only supports these. I don't know about other IP
		 * block vendors so I'm going to just let them through for
		 * now. Note that this is only a warning if for some obscure
		 * reason the DT really means to lie about it or it's a newer
		 * part we don't know about.
		 *
		 * On the Aspeed SoC there are additionally straps and SCU
		 * control bits that could tell us what the interface is
		 * (or allow us to configure it while the IP block is held
		 * in reset). For now I chose to keep this driver away from
		 * those SoC specific bits and assume the device-tree is
		 * right and the SCU has been configured properly by pinmux
		 * or the firmware.
		 */
		if (priv->is_aspeed &&
		    phy_intf != PHY_INTERFACE_MODE_RMII &&
		    phy_intf != PHY_INTERFACE_MODE_RGMII &&
		    phy_intf != PHY_INTERFACE_MODE_RGMII_ID &&
		    phy_intf != PHY_INTERFACE_MODE_RGMII_RXID &&
		    phy_intf != PHY_INTERFACE_MODE_RGMII_TXID) {
			netdev_warn(netdev,
				   "Unsupported PHY mode %s !\n",
				   phy_modes(phy_intf));
		}
	}

	priv->mii_bus->name = "ftgmac100_mdio";
	snprintf(priv->mii_bus->id, MII_BUS_ID_SIZE, "%s-%d",
		 pdev->name, pdev->id);
	priv->mii_bus->parent = priv->dev;
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

	err = ftgmac100_mii_probe(priv, phy_intf);
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

	netdev_dbg(nd->dev, "NCSI interface %s\n",
		   nd->link_up ? "up" : "down");
}

static int ftgmac100_setup_clk(struct ftgmac100 *priv)
{
	struct clk *clk;
	int rc;

	clk = devm_clk_get(priv->dev, NULL /* MACCLK */);
	if (IS_ERR(clk))
		return PTR_ERR(clk);
	priv->clk = clk;
	rc = clk_prepare_enable(priv->clk);
	if (rc)
		return rc;

	/* Aspeed specifies a 100MHz clock is required for up to
	 * 1000Mbit link speeds. As NCSI is limited to 100Mbit, 25MHz
	 * is sufficient
	 */
	rc = clk_set_rate(priv->clk, priv->use_ncsi ? FTGMAC_25MHZ :
			  FTGMAC_100MHZ);
	if (rc)
		goto cleanup_clk;

	/* RCLK is for RMII, typically used for NCSI. Optional because its not
	 * necessary if it's the AST2400 MAC, or the MAC is configured for
	 * RGMII, or the controller is not an ASPEED-based controller.
	 */
	priv->rclk = devm_clk_get_optional(priv->dev, "RCLK");
	rc = clk_prepare_enable(priv->rclk);
	if (!rc)
		return 0;

cleanup_clk:
	clk_disable_unprepare(priv->clk);

	return rc;
}

static int ftgmac100_probe(struct platform_device *pdev)
{
	struct resource *res;
	int irq;
	struct net_device *netdev;
	struct ftgmac100 *priv;
	struct device_node *np;
	int err = 0;

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
	netdev->watchdog_timeo = 5 * HZ;

	platform_set_drvdata(pdev, netdev);

	/* setup private data */
	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->dev = &pdev->dev;
	INIT_WORK(&priv->reset_task, ftgmac100_reset_task);

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

	/* Enable pause */
	priv->tx_pause = true;
	priv->rx_pause = true;
	priv->aneg_pause = true;

	/* MAC address from chip or random one */
	ftgmac100_initial_mac(priv);

	np = pdev->dev.of_node;
	if (np && (of_device_is_compatible(np, "aspeed,ast2400-mac") ||
		   of_device_is_compatible(np, "aspeed,ast2500-mac") ||
		   of_device_is_compatible(np, "aspeed,ast2600-mac"))) {
		priv->rxdes0_edorr_mask = BIT(30);
		priv->txdes0_edotr_mask = BIT(30);
		priv->is_aspeed = true;
	} else {
		priv->rxdes0_edorr_mask = BIT(15);
		priv->txdes0_edotr_mask = BIT(15);
	}

	if (np && of_get_property(np, "use-ncsi", NULL)) {
		if (!IS_ENABLED(CONFIG_NET_NCSI)) {
			dev_err(&pdev->dev, "NCSI stack not enabled\n");
			goto err_ncsi_dev;
		}

		dev_info(&pdev->dev, "Using NCSI interface\n");
		priv->use_ncsi = true;
		priv->ndev = ncsi_register_dev(netdev, ftgmac100_ncsi_handler);
		if (!priv->ndev)
			goto err_ncsi_dev;
	} else if (np && of_get_property(np, "phy-handle", NULL)) {
		struct phy_device *phy;

		phy = of_phy_get_and_connect(priv->netdev, np,
					     &ftgmac100_adjust_link);
		if (!phy) {
			dev_err(&pdev->dev, "Failed to connect to phy\n");
			goto err_setup_mdio;
		}

		/* Indicate that we support PAUSE frames (see comment in
		 * Documentation/networking/phy.rst)
		 */
		phy_support_asym_pause(phy);

		/* Display what we found */
		phy_attached_info(phy);
	} else if (np && !of_get_child_by_name(np, "mdio")) {
		/* Support legacy ASPEED devicetree descriptions that decribe a
		 * MAC with an embedded MDIO controller but have no "mdio"
		 * child node. Automatically scan the MDIO bus for available
		 * PHYs.
		 */
		priv->use_ncsi = false;
		err = ftgmac100_setup_mdio(netdev);
		if (err)
			goto err_setup_mdio;
	}

	if (priv->is_aspeed) {
		err = ftgmac100_setup_clk(priv);
		if (err)
			goto err_ncsi_dev;
	}

	/* Default ring sizes */
	priv->rx_q_entries = priv->new_rx_q_entries = DEF_RX_QUEUE_ENTRIES;
	priv->tx_q_entries = priv->new_tx_q_entries = DEF_TX_QUEUE_ENTRIES;

	/* Base feature set */
	netdev->hw_features = NETIF_F_RXCSUM | NETIF_F_HW_CSUM |
		NETIF_F_GRO | NETIF_F_SG | NETIF_F_HW_VLAN_CTAG_RX |
		NETIF_F_HW_VLAN_CTAG_TX;

	if (priv->use_ncsi)
		netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	/* AST2400  doesn't have working HW checksum generation */
	if (np && (of_device_is_compatible(np, "aspeed,ast2400-mac")))
		netdev->hw_features &= ~NETIF_F_HW_CSUM;
	if (np && of_get_property(np, "no-hw-checksum", NULL))
		netdev->hw_features &= ~(NETIF_F_HW_CSUM | NETIF_F_RXCSUM);
	netdev->features |= netdev->hw_features;

	/* register network device */
	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register netdev\n");
		goto err_register_netdev;
	}

	netdev_info(netdev, "irq %d, mapped at %p\n", netdev->irq, priv->base);

	return 0;

err_register_netdev:
	clk_disable_unprepare(priv->rclk);
	clk_disable_unprepare(priv->clk);
err_ncsi_dev:
	ftgmac100_destroy_mdio(netdev);
err_setup_mdio:
	iounmap(priv->base);
err_ioremap:
	release_resource(priv->res);
err_req_mem:
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

	clk_disable_unprepare(priv->rclk);
	clk_disable_unprepare(priv->clk);

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
