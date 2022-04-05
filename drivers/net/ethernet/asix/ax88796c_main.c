// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010 ASIX Electronics Corporation
 * Copyright (c) 2020 Samsung Electronics Co., Ltd.
 *
 * ASIX AX88796C SPI Fast Ethernet Linux driver
 */

#define pr_fmt(fmt)	"ax88796c: " fmt

#include "ax88796c_main.h"
#include "ax88796c_ioctl.h"

#include <linux/bitmap.h>
#include <linux/etherdevice.h>
#include <linux/iopoll.h>
#include <linux/lockdep.h>
#include <linux/mdio.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/skbuff.h>
#include <linux/spi/spi.h>

static int comp = IS_ENABLED(CONFIG_SPI_AX88796C_COMPRESSION);
static int msg_enable = NETIF_MSG_PROBE |
			NETIF_MSG_LINK |
			NETIF_MSG_RX_ERR |
			NETIF_MSG_TX_ERR;

static const char *no_regs_list = "80018001,e1918001,8001a001,fc0d0000";
unsigned long ax88796c_no_regs_mask[AX88796C_REGDUMP_LEN / (sizeof(unsigned long) * 8)];

module_param(msg_enable, int, 0444);
MODULE_PARM_DESC(msg_enable, "Message mask (see linux/netdevice.h for bitmap)");

static int ax88796c_soft_reset(struct ax88796c_device *ax_local)
{
	u16 temp;
	int ret;

	lockdep_assert_held(&ax_local->spi_lock);

	AX_WRITE(&ax_local->ax_spi, PSR_RESET, P0_PSR);
	AX_WRITE(&ax_local->ax_spi, PSR_RESET_CLR, P0_PSR);

	ret = read_poll_timeout(AX_READ, ret,
				(ret & PSR_DEV_READY),
				0, jiffies_to_usecs(160 * HZ / 1000), false,
				&ax_local->ax_spi, P0_PSR);
	if (ret)
		return ret;

	temp = AX_READ(&ax_local->ax_spi, P4_SPICR);
	if (ax_local->priv_flags & AX_CAP_COMP) {
		AX_WRITE(&ax_local->ax_spi,
			 (temp | SPICR_RCEN | SPICR_QCEN), P4_SPICR);
		ax_local->ax_spi.comp = 1;
	} else {
		AX_WRITE(&ax_local->ax_spi,
			 (temp & ~(SPICR_RCEN | SPICR_QCEN)), P4_SPICR);
		ax_local->ax_spi.comp = 0;
	}

	return 0;
}

static int ax88796c_reload_eeprom(struct ax88796c_device *ax_local)
{
	int ret;

	lockdep_assert_held(&ax_local->spi_lock);

	AX_WRITE(&ax_local->ax_spi, EECR_RELOAD, P3_EECR);

	ret = read_poll_timeout(AX_READ, ret,
				(ret & PSR_DEV_READY),
				0, jiffies_to_usecs(2 * HZ / 1000), false,
				&ax_local->ax_spi, P0_PSR);
	if (ret) {
		dev_err(&ax_local->spi->dev,
			"timeout waiting for reload eeprom\n");
		return ret;
	}

	return 0;
}

static void ax88796c_set_hw_multicast(struct net_device *ndev)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);
	int mc_count = netdev_mc_count(ndev);
	u16 rx_ctl = RXCR_AB;

	lockdep_assert_held(&ax_local->spi_lock);

	memset(ax_local->multi_filter, 0, AX_MCAST_FILTER_SIZE);

	if (ndev->flags & IFF_PROMISC) {
		rx_ctl |= RXCR_PRO;

	} else if (ndev->flags & IFF_ALLMULTI || mc_count > AX_MAX_MCAST) {
		rx_ctl |= RXCR_AMALL;

	} else if (mc_count == 0) {
		/* just broadcast and directed */
	} else {
		u32 crc_bits;
		int i;
		struct netdev_hw_addr *ha;

		netdev_for_each_mc_addr(ha, ndev) {
			crc_bits = ether_crc(ETH_ALEN, ha->addr);
			ax_local->multi_filter[crc_bits >> 29] |=
						(1 << ((crc_bits >> 26) & 7));
		}

		for (i = 0; i < 4; i++) {
			AX_WRITE(&ax_local->ax_spi,
				 ((ax_local->multi_filter[i * 2 + 1] << 8) |
				  ax_local->multi_filter[i * 2]), P3_MFAR(i));
		}
	}

	AX_WRITE(&ax_local->ax_spi, rx_ctl, P2_RXCR);
}

static void ax88796c_set_mac_addr(struct net_device *ndev)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);

	lockdep_assert_held(&ax_local->spi_lock);

	AX_WRITE(&ax_local->ax_spi, ((u16)(ndev->dev_addr[4] << 8) |
			(u16)ndev->dev_addr[5]), P3_MACASR0);
	AX_WRITE(&ax_local->ax_spi, ((u16)(ndev->dev_addr[2] << 8) |
			(u16)ndev->dev_addr[3]), P3_MACASR1);
	AX_WRITE(&ax_local->ax_spi, ((u16)(ndev->dev_addr[0] << 8) |
			(u16)ndev->dev_addr[1]), P3_MACASR2);
}

static void ax88796c_load_mac_addr(struct net_device *ndev)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);
	u8 addr[ETH_ALEN];
	u16 temp;

	lockdep_assert_held(&ax_local->spi_lock);

	/* Try the device tree first */
	if (!platform_get_ethdev_address(&ax_local->spi->dev, ndev) &&
	    is_valid_ether_addr(ndev->dev_addr)) {
		if (netif_msg_probe(ax_local))
			dev_info(&ax_local->spi->dev,
				 "MAC address read from device tree\n");
		return;
	}

	/* Read the MAC address from AX88796C */
	temp = AX_READ(&ax_local->ax_spi, P3_MACASR0);
	addr[5] = (u8)temp;
	addr[4] = (u8)(temp >> 8);

	temp = AX_READ(&ax_local->ax_spi, P3_MACASR1);
	addr[3] = (u8)temp;
	addr[2] = (u8)(temp >> 8);

	temp = AX_READ(&ax_local->ax_spi, P3_MACASR2);
	addr[1] = (u8)temp;
	addr[0] = (u8)(temp >> 8);

	if (is_valid_ether_addr(addr)) {
		eth_hw_addr_set(ndev, addr);
		if (netif_msg_probe(ax_local))
			dev_info(&ax_local->spi->dev,
				 "MAC address read from ASIX chip\n");
		return;
	}

	/* Use random address if none found */
	if (netif_msg_probe(ax_local))
		dev_info(&ax_local->spi->dev, "Use random MAC address\n");
	eth_hw_addr_random(ndev);
}

static void ax88796c_proc_tx_hdr(struct tx_pkt_info *info, u8 ip_summed)
{
	u16 pkt_len_bar = (~info->pkt_len & TX_HDR_SOP_PKTLENBAR);

	/* Prepare SOP header */
	info->sop.flags_len = info->pkt_len |
		((ip_summed == CHECKSUM_NONE) ||
		 (ip_summed == CHECKSUM_UNNECESSARY) ? TX_HDR_SOP_DICF : 0);

	info->sop.seq_lenbar = ((info->seq_num << 11) & TX_HDR_SOP_SEQNUM)
				| pkt_len_bar;
	cpu_to_be16s(&info->sop.flags_len);
	cpu_to_be16s(&info->sop.seq_lenbar);

	/* Prepare Segment header */
	info->seg.flags_seqnum_seglen = TX_HDR_SEG_FS | TX_HDR_SEG_LS
						| info->pkt_len;

	info->seg.eo_so_seglenbar = pkt_len_bar;

	cpu_to_be16s(&info->seg.flags_seqnum_seglen);
	cpu_to_be16s(&info->seg.eo_so_seglenbar);

	/* Prepare EOP header */
	info->eop.seq_len = ((info->seq_num << 11) &
			     TX_HDR_EOP_SEQNUM) | info->pkt_len;
	info->eop.seqbar_lenbar = ((~info->seq_num << 11) &
				   TX_HDR_EOP_SEQNUMBAR) | pkt_len_bar;

	cpu_to_be16s(&info->eop.seq_len);
	cpu_to_be16s(&info->eop.seqbar_lenbar);
}

static int
ax88796c_check_free_pages(struct ax88796c_device *ax_local, u8 need_pages)
{
	u8 free_pages;
	u16 tmp;

	lockdep_assert_held(&ax_local->spi_lock);

	free_pages = AX_READ(&ax_local->ax_spi, P0_TFBFCR) & TX_FREEBUF_MASK;
	if (free_pages < need_pages) {
		/* schedule free page interrupt */
		tmp = AX_READ(&ax_local->ax_spi, P0_TFBFCR)
				& TFBFCR_SCHE_FREE_PAGE;
		AX_WRITE(&ax_local->ax_spi, tmp | TFBFCR_TX_PAGE_SET |
				TFBFCR_SET_FREE_PAGE(need_pages),
				P0_TFBFCR);
		return -ENOMEM;
	}

	return 0;
}

static struct sk_buff *
ax88796c_tx_fixup(struct net_device *ndev, struct sk_buff_head *q)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);
	u8 spi_len = ax_local->ax_spi.comp ? 1 : 4;
	struct sk_buff *skb;
	struct tx_pkt_info info;
	struct skb_data *entry;
	u16 pkt_len;
	u8 padlen, seq_num;
	u8 need_pages;
	int headroom;
	int tailroom;

	if (skb_queue_empty(q))
		return NULL;

	skb = skb_peek(q);
	pkt_len = skb->len;
	need_pages = (pkt_len + TX_OVERHEAD + 127) >> 7;
	if (ax88796c_check_free_pages(ax_local, need_pages) != 0)
		return NULL;

	headroom = skb_headroom(skb);
	tailroom = skb_tailroom(skb);
	padlen = round_up(pkt_len, 4) - pkt_len;
	seq_num = ++ax_local->seq_num & 0x1F;

	info.pkt_len = pkt_len;

	if (skb_cloned(skb) ||
	    (headroom < (TX_OVERHEAD + spi_len)) ||
	    (tailroom < (padlen + TX_EOP_SIZE))) {
		size_t h = max((TX_OVERHEAD + spi_len) - headroom, 0);
		size_t t = max((padlen + TX_EOP_SIZE) - tailroom, 0);

		if (pskb_expand_head(skb, h, t, GFP_KERNEL))
			return NULL;
	}

	info.seq_num = seq_num;
	ax88796c_proc_tx_hdr(&info, skb->ip_summed);

	/* SOP and SEG header */
	memcpy(skb_push(skb, TX_OVERHEAD), &info.sop, TX_OVERHEAD);

	/* Write SPI TXQ header */
	memcpy(skb_push(skb, spi_len), ax88796c_tx_cmd_buf, spi_len);

	/* Make 32-bit alignment */
	skb_put(skb, padlen);

	/* EOP header */
	memcpy(skb_put(skb, TX_EOP_SIZE), &info.eop, TX_EOP_SIZE);

	skb_unlink(skb, q);

	entry = (struct skb_data *)skb->cb;
	memset(entry, 0, sizeof(*entry));
	entry->len = pkt_len;

	if (netif_msg_pktdata(ax_local)) {
		char pfx[IFNAMSIZ + 7];

		snprintf(pfx, sizeof(pfx), "%s:     ", ndev->name);

		netdev_info(ndev, "TX packet len %d, total len %d, seq %d\n",
			    pkt_len, skb->len, seq_num);

		netdev_info(ndev, "  SPI Header:\n");
		print_hex_dump(KERN_INFO, pfx, DUMP_PREFIX_OFFSET, 16, 1,
			       skb->data, 4, 0);

		netdev_info(ndev, "  TX SOP:\n");
		print_hex_dump(KERN_INFO, pfx, DUMP_PREFIX_OFFSET, 16, 1,
			       skb->data + 4, TX_OVERHEAD, 0);

		netdev_info(ndev, "  TX packet:\n");
		print_hex_dump(KERN_INFO, pfx, DUMP_PREFIX_OFFSET, 16, 1,
			       skb->data + 4 + TX_OVERHEAD,
			       skb->len - TX_EOP_SIZE - 4 - TX_OVERHEAD, 0);

		netdev_info(ndev, "  TX EOP:\n");
		print_hex_dump(KERN_INFO, pfx, DUMP_PREFIX_OFFSET, 16, 1,
			       skb->data + skb->len - 4, 4, 0);
	}

	return skb;
}

static int ax88796c_hard_xmit(struct ax88796c_device *ax_local)
{
	struct ax88796c_pcpu_stats *stats;
	struct sk_buff *tx_skb;
	struct skb_data *entry;
	unsigned long flags;

	lockdep_assert_held(&ax_local->spi_lock);

	stats = this_cpu_ptr(ax_local->stats);
	tx_skb = ax88796c_tx_fixup(ax_local->ndev, &ax_local->tx_wait_q);

	if (!tx_skb) {
		this_cpu_inc(ax_local->stats->tx_dropped);
		return 0;
	}
	entry = (struct skb_data *)tx_skb->cb;

	AX_WRITE(&ax_local->ax_spi,
		 (TSNR_TXB_START | TSNR_PKT_CNT(1)), P0_TSNR);

	axspi_write_txq(&ax_local->ax_spi, tx_skb->data, tx_skb->len);

	if (((AX_READ(&ax_local->ax_spi, P0_TSNR) & TXNR_TXB_IDLE) == 0) ||
	    ((ISR_TXERR & AX_READ(&ax_local->ax_spi, P0_ISR)) != 0)) {
		/* Ack tx error int */
		AX_WRITE(&ax_local->ax_spi, ISR_TXERR, P0_ISR);

		this_cpu_inc(ax_local->stats->tx_dropped);

		if (net_ratelimit())
			netif_err(ax_local, tx_err, ax_local->ndev,
				  "TX FIFO error, re-initialize the TX bridge\n");

		/* Reinitial tx bridge */
		AX_WRITE(&ax_local->ax_spi, TXNR_TXB_REINIT |
			AX_READ(&ax_local->ax_spi, P0_TSNR), P0_TSNR);
		ax_local->seq_num = 0;
	} else {
		flags = u64_stats_update_begin_irqsave(&stats->syncp);
		u64_stats_inc(&stats->tx_packets);
		u64_stats_add(&stats->tx_bytes, entry->len);
		u64_stats_update_end_irqrestore(&stats->syncp, flags);
	}

	entry->state = tx_done;
	dev_kfree_skb(tx_skb);

	return 1;
}

static int
ax88796c_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);

	skb_queue_tail(&ax_local->tx_wait_q, skb);
	if (skb_queue_len(&ax_local->tx_wait_q) > TX_QUEUE_HIGH_WATER)
		netif_stop_queue(ndev);

	set_bit(EVENT_TX, &ax_local->flags);
	schedule_work(&ax_local->ax_work);

	return NETDEV_TX_OK;
}

static void
ax88796c_skb_return(struct ax88796c_device *ax_local,
		    struct sk_buff *skb, struct rx_header *rxhdr)
{
	struct net_device *ndev = ax_local->ndev;
	struct ax88796c_pcpu_stats *stats;
	unsigned long flags;
	int status;

	stats = this_cpu_ptr(ax_local->stats);

	do {
		if (!(ndev->features & NETIF_F_RXCSUM))
			break;

		/* checksum error bit is set */
		if ((rxhdr->flags & RX_HDR3_L3_ERR) ||
		    (rxhdr->flags & RX_HDR3_L4_ERR))
			break;

		/* Other types may be indicated by more than one bit. */
		if ((rxhdr->flags & RX_HDR3_L4_TYPE_TCP) ||
		    (rxhdr->flags & RX_HDR3_L4_TYPE_UDP))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
	} while (0);

	flags = u64_stats_update_begin_irqsave(&stats->syncp);
	u64_stats_inc(&stats->rx_packets);
	u64_stats_add(&stats->rx_bytes, skb->len);
	u64_stats_update_end_irqrestore(&stats->syncp, flags);

	skb->dev = ndev;
	skb->protocol = eth_type_trans(skb, ax_local->ndev);

	netif_info(ax_local, rx_status, ndev, "< rx, len %zu, type 0x%x\n",
		   skb->len + sizeof(struct ethhdr), skb->protocol);

	status = netif_rx(skb);
	if (status != NET_RX_SUCCESS && net_ratelimit())
		netif_info(ax_local, rx_err, ndev,
			   "netif_rx status %d\n", status);
}

static void
ax88796c_rx_fixup(struct ax88796c_device *ax_local, struct sk_buff *rx_skb)
{
	struct rx_header *rxhdr = (struct rx_header *)rx_skb->data;
	struct net_device *ndev = ax_local->ndev;
	u16 len;

	be16_to_cpus(&rxhdr->flags_len);
	be16_to_cpus(&rxhdr->seq_lenbar);
	be16_to_cpus(&rxhdr->flags);

	if ((rxhdr->flags_len & RX_HDR1_PKT_LEN) !=
			 (~rxhdr->seq_lenbar & 0x7FF)) {
		netif_err(ax_local, rx_err, ndev, "Header error\n");

		this_cpu_inc(ax_local->stats->rx_frame_errors);
		kfree_skb(rx_skb);
		return;
	}

	if ((rxhdr->flags_len & RX_HDR1_MII_ERR) ||
	    (rxhdr->flags_len & RX_HDR1_CRC_ERR)) {
		netif_err(ax_local, rx_err, ndev, "CRC or MII error\n");

		this_cpu_inc(ax_local->stats->rx_crc_errors);
		kfree_skb(rx_skb);
		return;
	}

	len = rxhdr->flags_len & RX_HDR1_PKT_LEN;
	if (netif_msg_pktdata(ax_local)) {
		char pfx[IFNAMSIZ + 7];

		snprintf(pfx, sizeof(pfx), "%s:     ", ndev->name);
		netdev_info(ndev, "RX data, total len %d, packet len %d\n",
			    rx_skb->len, len);

		netdev_info(ndev, "  Dump RX packet header:");
		print_hex_dump(KERN_INFO, pfx, DUMP_PREFIX_OFFSET, 16, 1,
			       rx_skb->data, sizeof(*rxhdr), 0);

		netdev_info(ndev, "  Dump RX packet:");
		print_hex_dump(KERN_INFO, pfx, DUMP_PREFIX_OFFSET, 16, 1,
			       rx_skb->data + sizeof(*rxhdr), len, 0);
	}

	skb_pull(rx_skb, sizeof(*rxhdr));
	pskb_trim(rx_skb, len);

	ax88796c_skb_return(ax_local, rx_skb, rxhdr);
}

static int ax88796c_receive(struct net_device *ndev)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);
	struct skb_data *entry;
	u16 w_count, pkt_len;
	struct sk_buff *skb;
	u8 pkt_cnt;

	lockdep_assert_held(&ax_local->spi_lock);

	/* check rx packet and total word count */
	AX_WRITE(&ax_local->ax_spi, AX_READ(&ax_local->ax_spi, P0_RTWCR)
		  | RTWCR_RX_LATCH, P0_RTWCR);

	pkt_cnt = AX_READ(&ax_local->ax_spi, P0_RXBCR2) & RXBCR2_PKT_MASK;
	if (!pkt_cnt)
		return 0;

	pkt_len = AX_READ(&ax_local->ax_spi, P0_RCPHR) & 0x7FF;

	w_count = round_up(pkt_len + 6, 4) >> 1;

	skb = netdev_alloc_skb(ndev, w_count * 2);
	if (!skb) {
		AX_WRITE(&ax_local->ax_spi, RXBCR1_RXB_DISCARD, P0_RXBCR1);
		this_cpu_inc(ax_local->stats->rx_dropped);
		return 0;
	}
	entry = (struct skb_data *)skb->cb;

	AX_WRITE(&ax_local->ax_spi, RXBCR1_RXB_START | w_count, P0_RXBCR1);

	axspi_read_rxq(&ax_local->ax_spi,
		       skb_put(skb, w_count * 2), skb->len);

	/* Check if rx bridge is idle */
	if ((AX_READ(&ax_local->ax_spi, P0_RXBCR2) & RXBCR2_RXB_IDLE) == 0) {
		if (net_ratelimit())
			netif_err(ax_local, rx_err, ndev,
				  "Rx Bridge is not idle\n");
		AX_WRITE(&ax_local->ax_spi, RXBCR2_RXB_REINIT, P0_RXBCR2);

		entry->state = rx_err;
	} else {
		entry->state = rx_done;
	}

	AX_WRITE(&ax_local->ax_spi, ISR_RXPKT, P0_ISR);

	ax88796c_rx_fixup(ax_local, skb);

	return 1;
}

static int ax88796c_process_isr(struct ax88796c_device *ax_local)
{
	struct net_device *ndev = ax_local->ndev;
	int todo = 0;
	u16 isr;

	lockdep_assert_held(&ax_local->spi_lock);

	isr = AX_READ(&ax_local->ax_spi, P0_ISR);
	AX_WRITE(&ax_local->ax_spi, isr, P0_ISR);

	netif_dbg(ax_local, intr, ndev, "  ISR 0x%04x\n", isr);

	if (isr & ISR_TXERR) {
		netif_dbg(ax_local, intr, ndev, "  TXERR interrupt\n");
		AX_WRITE(&ax_local->ax_spi, TXNR_TXB_REINIT, P0_TSNR);
		ax_local->seq_num = 0x1f;
	}

	if (isr & ISR_TXPAGES) {
		netif_dbg(ax_local, intr, ndev, "  TXPAGES interrupt\n");
		set_bit(EVENT_TX, &ax_local->flags);
	}

	if (isr & ISR_LINK) {
		netif_dbg(ax_local, intr, ndev, "  Link change interrupt\n");
		phy_mac_interrupt(ax_local->ndev->phydev);
	}

	if (isr & ISR_RXPKT) {
		netif_dbg(ax_local, intr, ndev, "  RX interrupt\n");
		todo = ax88796c_receive(ax_local->ndev);
	}

	return todo;
}

static irqreturn_t ax88796c_interrupt(int irq, void *dev_instance)
{
	struct ax88796c_device *ax_local;
	struct net_device *ndev;

	ndev = dev_instance;
	if (!ndev) {
		pr_err("irq %d for unknown device.\n", irq);
		return IRQ_RETVAL(0);
	}
	ax_local = to_ax88796c_device(ndev);

	disable_irq_nosync(irq);

	netif_dbg(ax_local, intr, ndev, "Interrupt occurred\n");

	set_bit(EVENT_INTR, &ax_local->flags);
	schedule_work(&ax_local->ax_work);

	return IRQ_HANDLED;
}

static void ax88796c_work(struct work_struct *work)
{
	struct ax88796c_device *ax_local =
			container_of(work, struct ax88796c_device, ax_work);

	mutex_lock(&ax_local->spi_lock);

	if (test_bit(EVENT_SET_MULTI, &ax_local->flags)) {
		ax88796c_set_hw_multicast(ax_local->ndev);
		clear_bit(EVENT_SET_MULTI, &ax_local->flags);
	}

	if (test_bit(EVENT_INTR, &ax_local->flags)) {
		AX_WRITE(&ax_local->ax_spi, IMR_MASKALL, P0_IMR);

		while (ax88796c_process_isr(ax_local))
			/* nothing */;

		clear_bit(EVENT_INTR, &ax_local->flags);

		AX_WRITE(&ax_local->ax_spi, IMR_DEFAULT, P0_IMR);

		enable_irq(ax_local->ndev->irq);
	}

	if (test_bit(EVENT_TX, &ax_local->flags)) {
		while (skb_queue_len(&ax_local->tx_wait_q)) {
			if (!ax88796c_hard_xmit(ax_local))
				break;
		}

		clear_bit(EVENT_TX, &ax_local->flags);

		if (netif_queue_stopped(ax_local->ndev) &&
		    (skb_queue_len(&ax_local->tx_wait_q) < TX_QUEUE_LOW_WATER))
			netif_wake_queue(ax_local->ndev);
	}

	mutex_unlock(&ax_local->spi_lock);
}

static void ax88796c_get_stats64(struct net_device *ndev,
				 struct rtnl_link_stats64 *stats)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);
	u32 rx_frame_errors = 0, rx_crc_errors = 0;
	u32 rx_dropped = 0, tx_dropped = 0;
	unsigned int start;
	int cpu;

	for_each_possible_cpu(cpu) {
		struct ax88796c_pcpu_stats *s;
		u64 rx_packets, rx_bytes;
		u64 tx_packets, tx_bytes;

		s = per_cpu_ptr(ax_local->stats, cpu);

		do {
			start = u64_stats_fetch_begin_irq(&s->syncp);
			rx_packets = u64_stats_read(&s->rx_packets);
			rx_bytes   = u64_stats_read(&s->rx_bytes);
			tx_packets = u64_stats_read(&s->tx_packets);
			tx_bytes   = u64_stats_read(&s->tx_bytes);
		} while (u64_stats_fetch_retry_irq(&s->syncp, start));

		stats->rx_packets += rx_packets;
		stats->rx_bytes   += rx_bytes;
		stats->tx_packets += tx_packets;
		stats->tx_bytes   += tx_bytes;

		rx_dropped      += s->rx_dropped;
		tx_dropped      += s->tx_dropped;
		rx_frame_errors += s->rx_frame_errors;
		rx_crc_errors   += s->rx_crc_errors;
	}

	stats->rx_dropped = rx_dropped;
	stats->tx_dropped = tx_dropped;
	stats->rx_frame_errors = rx_frame_errors;
	stats->rx_crc_errors = rx_crc_errors;
}

static void ax88796c_set_mac(struct  ax88796c_device *ax_local)
{
	u16 maccr;

	maccr = (ax_local->link) ? MACCR_RXEN : 0;

	switch (ax_local->speed) {
	case SPEED_100:
		maccr |= MACCR_SPEED_100;
		break;
	case SPEED_10:
	case SPEED_UNKNOWN:
		break;
	default:
		return;
	}

	switch (ax_local->duplex) {
	case DUPLEX_FULL:
		maccr |= MACCR_SPEED_100;
		break;
	case DUPLEX_HALF:
	case DUPLEX_UNKNOWN:
		break;
	default:
		return;
	}

	if (ax_local->flowctrl & AX_FC_ANEG &&
	    ax_local->phydev->autoneg) {
		maccr |= ax_local->pause ? MACCR_RXFC_ENABLE : 0;
		maccr |= !ax_local->pause != !ax_local->asym_pause ?
			MACCR_TXFC_ENABLE : 0;
	} else {
		maccr |= (ax_local->flowctrl & AX_FC_RX) ? MACCR_RXFC_ENABLE : 0;
		maccr |= (ax_local->flowctrl & AX_FC_TX) ? MACCR_TXFC_ENABLE : 0;
	}

	mutex_lock(&ax_local->spi_lock);

	maccr |= AX_READ(&ax_local->ax_spi, P0_MACCR) &
		~(MACCR_DUPLEX_FULL | MACCR_SPEED_100 |
		  MACCR_TXFC_ENABLE | MACCR_RXFC_ENABLE);
	AX_WRITE(&ax_local->ax_spi, maccr, P0_MACCR);

	mutex_unlock(&ax_local->spi_lock);
}

static void ax88796c_handle_link_change(struct net_device *ndev)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);
	struct phy_device *phydev = ndev->phydev;
	bool update = false;

	if (phydev->link && (ax_local->speed != phydev->speed ||
			     ax_local->duplex != phydev->duplex ||
			     ax_local->pause != phydev->pause ||
			     ax_local->asym_pause != phydev->asym_pause)) {
		ax_local->speed = phydev->speed;
		ax_local->duplex = phydev->duplex;
		ax_local->pause = phydev->pause;
		ax_local->asym_pause = phydev->asym_pause;
		update = true;
	}

	if (phydev->link != ax_local->link) {
		if (!phydev->link) {
			ax_local->speed = SPEED_UNKNOWN;
			ax_local->duplex = DUPLEX_UNKNOWN;
		}

		ax_local->link = phydev->link;
		update = true;
	}

	if (update)
		ax88796c_set_mac(ax_local);

	if (net_ratelimit())
		phy_print_status(ndev->phydev);
}

static void ax88796c_set_csums(struct ax88796c_device *ax_local)
{
	struct net_device *ndev = ax_local->ndev;

	lockdep_assert_held(&ax_local->spi_lock);

	if (ndev->features & NETIF_F_RXCSUM) {
		AX_WRITE(&ax_local->ax_spi, COERCR0_DEFAULT, P4_COERCR0);
		AX_WRITE(&ax_local->ax_spi, COERCR1_DEFAULT, P4_COERCR1);
	} else {
		AX_WRITE(&ax_local->ax_spi, 0, P4_COERCR0);
		AX_WRITE(&ax_local->ax_spi, 0, P4_COERCR1);
	}

	if (ndev->features & NETIF_F_HW_CSUM) {
		AX_WRITE(&ax_local->ax_spi, COETCR0_DEFAULT, P4_COETCR0);
		AX_WRITE(&ax_local->ax_spi, COETCR1_TXPPPE, P4_COETCR1);
	} else {
		AX_WRITE(&ax_local->ax_spi, 0, P4_COETCR0);
		AX_WRITE(&ax_local->ax_spi, 0, P4_COETCR1);
	}
}

static int
ax88796c_open(struct net_device *ndev)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);
	unsigned long irq_flag = 0;
	int fc = AX_FC_NONE;
	int ret;
	u16 t;

	ret = request_irq(ndev->irq, ax88796c_interrupt,
			  irq_flag, ndev->name, ndev);
	if (ret) {
		netdev_err(ndev, "unable to get IRQ %d (errno=%d).\n",
			   ndev->irq, ret);
		return ret;
	}

	mutex_lock(&ax_local->spi_lock);

	ret = ax88796c_soft_reset(ax_local);
	if (ret < 0) {
		free_irq(ndev->irq, ndev);
		mutex_unlock(&ax_local->spi_lock);
		return ret;
	}
	ax_local->seq_num = 0x1f;

	ax88796c_set_mac_addr(ndev);
	ax88796c_set_csums(ax_local);

	/* Disable stuffing packet */
	t = AX_READ(&ax_local->ax_spi, P1_RXBSPCR);
	t &= ~RXBSPCR_STUF_ENABLE;
	AX_WRITE(&ax_local->ax_spi, t, P1_RXBSPCR);

	/* Enable RX packet process */
	AX_WRITE(&ax_local->ax_spi, RPPER_RXEN, P1_RPPER);

	t = AX_READ(&ax_local->ax_spi, P0_FER);
	t |= FER_RXEN | FER_TXEN | FER_BSWAP | FER_IRQ_PULL;
	AX_WRITE(&ax_local->ax_spi, t, P0_FER);

	/* Setup LED mode */
	AX_WRITE(&ax_local->ax_spi,
		 (LCR_LED0_EN | LCR_LED0_DUPLEX | LCR_LED1_EN |
		 LCR_LED1_100MODE), P2_LCR0);
	AX_WRITE(&ax_local->ax_spi,
		 (AX_READ(&ax_local->ax_spi, P2_LCR1) & LCR_LED2_MASK) |
		 LCR_LED2_EN | LCR_LED2_LINK, P2_LCR1);

	/* Disable PHY auto-polling */
	AX_WRITE(&ax_local->ax_spi, PCR_PHYID(AX88796C_PHY_ID), P2_PCR);

	/* Enable MAC interrupts */
	AX_WRITE(&ax_local->ax_spi, IMR_DEFAULT, P0_IMR);

	mutex_unlock(&ax_local->spi_lock);

	/* Setup flow-control configuration */
	phy_support_asym_pause(ax_local->phydev);

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT,
			      ax_local->phydev->advertising) ||
	    linkmode_test_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
			      ax_local->phydev->advertising))
		fc |= AX_FC_ANEG;

	fc |= linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT,
				ax_local->phydev->advertising) ? AX_FC_RX : 0;
	fc |= (linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT,
				 ax_local->phydev->advertising) !=
	       linkmode_test_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
				 ax_local->phydev->advertising)) ? AX_FC_TX : 0;
	ax_local->flowctrl = fc;

	phy_start(ax_local->ndev->phydev);

	netif_start_queue(ndev);

	spi_message_init(&ax_local->ax_spi.rx_msg);

	return 0;
}

static int
ax88796c_close(struct net_device *ndev)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);

	phy_stop(ndev->phydev);

	/* We lock the mutex early not only to protect the device
	 * against concurrent access, but also avoid waking up the
	 * queue in ax88796c_work(). phy_stop() needs to be called
	 * before because it locks the mutex to access SPI.
	 */
	mutex_lock(&ax_local->spi_lock);

	netif_stop_queue(ndev);

	/* No more work can be scheduled now. Make any pending work,
	 * including one already waiting for the mutex to be unlocked,
	 * NOP.
	 */
	netif_dbg(ax_local, ifdown, ndev, "clearing bits\n");
	clear_bit(EVENT_SET_MULTI, &ax_local->flags);
	clear_bit(EVENT_INTR, &ax_local->flags);
	clear_bit(EVENT_TX, &ax_local->flags);

	/* Disable MAC interrupts */
	AX_WRITE(&ax_local->ax_spi, IMR_MASKALL, P0_IMR);
	__skb_queue_purge(&ax_local->tx_wait_q);
	ax88796c_soft_reset(ax_local);

	mutex_unlock(&ax_local->spi_lock);

	cancel_work_sync(&ax_local->ax_work);

	free_irq(ndev->irq, ndev);

	return 0;
}

static int
ax88796c_set_features(struct net_device *ndev, netdev_features_t features)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);
	netdev_features_t changed = features ^ ndev->features;

	if (!(changed & (NETIF_F_RXCSUM | NETIF_F_HW_CSUM)))
		return 0;

	ndev->features = features;

	if (changed & (NETIF_F_RXCSUM | NETIF_F_HW_CSUM))
		ax88796c_set_csums(ax_local);

	return 0;
}

static const struct net_device_ops ax88796c_netdev_ops = {
	.ndo_open		= ax88796c_open,
	.ndo_stop		= ax88796c_close,
	.ndo_start_xmit		= ax88796c_start_xmit,
	.ndo_get_stats64	= ax88796c_get_stats64,
	.ndo_eth_ioctl		= ax88796c_ioctl,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_set_features	= ax88796c_set_features,
};

static int ax88796c_hard_reset(struct ax88796c_device *ax_local)
{
	struct device *dev = (struct device *)&ax_local->spi->dev;
	struct gpio_desc *reset_gpio;

	/* reset info */
	reset_gpio = gpiod_get(dev, "reset", 0);
	if (IS_ERR(reset_gpio)) {
		dev_err(dev, "Could not get 'reset' GPIO: %ld", PTR_ERR(reset_gpio));
		return PTR_ERR(reset_gpio);
	}

	/* set reset */
	gpiod_direction_output(reset_gpio, 1);
	msleep(100);
	gpiod_direction_output(reset_gpio, 0);
	gpiod_put(reset_gpio);
	msleep(20);

	return 0;
}

static int ax88796c_probe(struct spi_device *spi)
{
	char phy_id[MII_BUS_ID_SIZE + 3];
	struct ax88796c_device *ax_local;
	struct net_device *ndev;
	u16 temp;
	int ret;

	ndev = devm_alloc_etherdev(&spi->dev, sizeof(*ax_local));
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, &spi->dev);

	ax_local = to_ax88796c_device(ndev);

	dev_set_drvdata(&spi->dev, ax_local);
	ax_local->spi = spi;
	ax_local->ax_spi.spi = spi;

	ax_local->stats =
		devm_netdev_alloc_pcpu_stats(&spi->dev,
					     struct ax88796c_pcpu_stats);
	if (!ax_local->stats)
		return -ENOMEM;

	ax_local->ndev = ndev;
	ax_local->priv_flags |= comp ? AX_CAP_COMP : 0;
	ax_local->msg_enable = msg_enable;
	mutex_init(&ax_local->spi_lock);

	ax_local->mdiobus = devm_mdiobus_alloc(&spi->dev);
	if (!ax_local->mdiobus)
		return -ENOMEM;

	ax_local->mdiobus->priv = ax_local;
	ax_local->mdiobus->read = ax88796c_mdio_read;
	ax_local->mdiobus->write = ax88796c_mdio_write;
	ax_local->mdiobus->name = "ax88976c-mdiobus";
	ax_local->mdiobus->phy_mask = (u32)~BIT(AX88796C_PHY_ID);
	ax_local->mdiobus->parent = &spi->dev;

	snprintf(ax_local->mdiobus->id, MII_BUS_ID_SIZE,
		 "ax88796c-%s.%u", dev_name(&spi->dev), spi->chip_select);

	ret = devm_mdiobus_register(&spi->dev, ax_local->mdiobus);
	if (ret < 0) {
		dev_err(&spi->dev, "Could not register MDIO bus\n");
		return ret;
	}

	if (netif_msg_probe(ax_local)) {
		dev_info(&spi->dev, "AX88796C-SPI Configuration:\n");
		dev_info(&spi->dev, "    Compression : %s\n",
			 ax_local->priv_flags & AX_CAP_COMP ? "ON" : "OFF");
	}

	ndev->irq = spi->irq;
	ndev->netdev_ops = &ax88796c_netdev_ops;
	ndev->ethtool_ops = &ax88796c_ethtool_ops;
	ndev->hw_features |= NETIF_F_HW_CSUM | NETIF_F_RXCSUM;
	ndev->features |= NETIF_F_HW_CSUM | NETIF_F_RXCSUM;
	ndev->needed_headroom = TX_OVERHEAD;
	ndev->needed_tailroom = TX_EOP_SIZE;

	mutex_lock(&ax_local->spi_lock);

	/* ax88796c gpio reset */
	ax88796c_hard_reset(ax_local);

	/* Reset AX88796C */
	ret = ax88796c_soft_reset(ax_local);
	if (ret < 0) {
		ret = -ENODEV;
		mutex_unlock(&ax_local->spi_lock);
		goto err;
	}
	/* Check board revision */
	temp = AX_READ(&ax_local->ax_spi, P2_CRIR);
	if ((temp & 0xF) != 0x0) {
		dev_err(&spi->dev, "spi read failed: %d\n", temp);
		ret = -ENODEV;
		mutex_unlock(&ax_local->spi_lock);
		goto err;
	}

	/*Reload EEPROM*/
	ax88796c_reload_eeprom(ax_local);

	ax88796c_load_mac_addr(ndev);

	if (netif_msg_probe(ax_local))
		dev_info(&spi->dev,
			 "irq %d, MAC addr %02X:%02X:%02X:%02X:%02X:%02X\n",
			 ndev->irq,
			 ndev->dev_addr[0], ndev->dev_addr[1],
			 ndev->dev_addr[2], ndev->dev_addr[3],
			 ndev->dev_addr[4], ndev->dev_addr[5]);

	/* Disable power saving */
	AX_WRITE(&ax_local->ax_spi, (AX_READ(&ax_local->ax_spi, P0_PSCR)
				     & PSCR_PS_MASK) | PSCR_PS_D0, P0_PSCR);

	mutex_unlock(&ax_local->spi_lock);

	INIT_WORK(&ax_local->ax_work, ax88796c_work);

	skb_queue_head_init(&ax_local->tx_wait_q);

	snprintf(phy_id, MII_BUS_ID_SIZE + 3, PHY_ID_FMT,
		 ax_local->mdiobus->id, AX88796C_PHY_ID);
	ax_local->phydev = phy_connect(ax_local->ndev, phy_id,
				       ax88796c_handle_link_change,
				       PHY_INTERFACE_MODE_MII);
	if (IS_ERR(ax_local->phydev)) {
		ret = PTR_ERR(ax_local->phydev);
		goto err;
	}
	ax_local->phydev->irq = PHY_POLL;

	ret = devm_register_netdev(&spi->dev, ndev);
	if (ret) {
		dev_err(&spi->dev, "failed to register a network device\n");
		goto err_phy_dis;
	}

	netif_info(ax_local, probe, ndev, "%s %s registered\n",
		   dev_driver_string(&spi->dev),
		   dev_name(&spi->dev));
	phy_attached_info(ax_local->phydev);

	return 0;

err_phy_dis:
	phy_disconnect(ax_local->phydev);
err:
	return ret;
}

static void ax88796c_remove(struct spi_device *spi)
{
	struct ax88796c_device *ax_local = dev_get_drvdata(&spi->dev);
	struct net_device *ndev = ax_local->ndev;

	phy_disconnect(ndev->phydev);

	netif_info(ax_local, probe, ndev, "removing network device %s %s\n",
		   dev_driver_string(&spi->dev),
		   dev_name(&spi->dev));
}

#ifdef CONFIG_OF
static const struct of_device_id ax88796c_dt_ids[] = {
	{ .compatible = "asix,ax88796c" },
	{},
};
MODULE_DEVICE_TABLE(of, ax88796c_dt_ids);
#endif

static const struct spi_device_id asix_id[] = {
	{ "ax88796c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, asix_id);

static struct spi_driver ax88796c_spi_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(ax88796c_dt_ids),
	},
	.probe = ax88796c_probe,
	.remove = ax88796c_remove,
	.id_table = asix_id,
};

static __init int ax88796c_spi_init(void)
{
	int ret;

	bitmap_zero(ax88796c_no_regs_mask, AX88796C_REGDUMP_LEN);
	ret = bitmap_parse(no_regs_list, 35,
			   ax88796c_no_regs_mask, AX88796C_REGDUMP_LEN);
	if (ret) {
		bitmap_fill(ax88796c_no_regs_mask, AX88796C_REGDUMP_LEN);
		pr_err("Invalid bitmap description, masking all registers\n");
	}

	return spi_register_driver(&ax88796c_spi_driver);
}

static __exit void ax88796c_spi_exit(void)
{
	spi_unregister_driver(&ax88796c_spi_driver);
}

module_init(ax88796c_spi_init);
module_exit(ax88796c_spi_exit);

MODULE_AUTHOR("Łukasz Stelmach <l.stelmach@samsung.com>");
MODULE_DESCRIPTION("ASIX AX88796C SPI Ethernet driver");
MODULE_LICENSE("GPL");
