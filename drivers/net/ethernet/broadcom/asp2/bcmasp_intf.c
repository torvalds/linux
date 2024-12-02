// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt)			"bcmasp_intf: " fmt

#include <asm/byteorder.h>
#include <linux/brcmphy.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/ptp_classify.h>
#include <linux/platform_device.h>
#include <net/ip.h>
#include <net/ipv6.h>

#include "bcmasp.h"
#include "bcmasp_intf_defs.h"

static int incr_ring(int index, int ring_count)
{
	index++;
	if (index == ring_count)
		return 0;

	return index;
}

/* Points to last byte of descriptor */
static dma_addr_t incr_last_byte(dma_addr_t addr, dma_addr_t beg,
				 int ring_count)
{
	dma_addr_t end = beg + (ring_count * DESC_SIZE);

	addr += DESC_SIZE;
	if (addr > end)
		return beg + DESC_SIZE - 1;

	return addr;
}

/* Points to first byte of descriptor */
static dma_addr_t incr_first_byte(dma_addr_t addr, dma_addr_t beg,
				  int ring_count)
{
	dma_addr_t end = beg + (ring_count * DESC_SIZE);

	addr += DESC_SIZE;
	if (addr >= end)
		return beg;

	return addr;
}

static void bcmasp_enable_tx(struct bcmasp_intf *intf, int en)
{
	if (en) {
		tx_spb_ctrl_wl(intf, TX_SPB_CTRL_ENABLE_EN, TX_SPB_CTRL_ENABLE);
		tx_epkt_core_wl(intf, (TX_EPKT_C_CFG_MISC_EN |
				TX_EPKT_C_CFG_MISC_PT |
				(intf->port << TX_EPKT_C_CFG_MISC_PS_SHIFT)),
				TX_EPKT_C_CFG_MISC);
	} else {
		tx_spb_ctrl_wl(intf, 0x0, TX_SPB_CTRL_ENABLE);
		tx_epkt_core_wl(intf, 0x0, TX_EPKT_C_CFG_MISC);
	}
}

static void bcmasp_enable_rx(struct bcmasp_intf *intf, int en)
{
	if (en)
		rx_edpkt_cfg_wl(intf, RX_EDPKT_CFG_ENABLE_EN,
				RX_EDPKT_CFG_ENABLE);
	else
		rx_edpkt_cfg_wl(intf, 0x0, RX_EDPKT_CFG_ENABLE);
}

static void bcmasp_set_rx_mode(struct net_device *dev)
{
	unsigned char mask[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct bcmasp_intf *intf = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	int ret;

	spin_lock_bh(&intf->parent->mda_lock);

	bcmasp_disable_all_filters(intf);

	if (dev->flags & IFF_PROMISC)
		goto set_promisc;

	bcmasp_set_promisc(intf, 0);

	bcmasp_set_broad(intf, 1);

	bcmasp_set_oaddr(intf, dev->dev_addr, 1);

	if (dev->flags & IFF_ALLMULTI) {
		bcmasp_set_allmulti(intf, 1);
	} else {
		bcmasp_set_allmulti(intf, 0);

		netdev_for_each_mc_addr(ha, dev) {
			ret = bcmasp_set_en_mda_filter(intf, ha->addr, mask);
			if (ret) {
				intf->mib.mc_filters_full_cnt++;
				goto set_promisc;
			}
		}
	}

	netdev_for_each_uc_addr(ha, dev) {
		ret = bcmasp_set_en_mda_filter(intf, ha->addr, mask);
		if (ret) {
			intf->mib.uc_filters_full_cnt++;
			goto set_promisc;
		}
	}

	spin_unlock_bh(&intf->parent->mda_lock);
	return;

set_promisc:
	bcmasp_set_promisc(intf, 1);
	intf->mib.promisc_filters_cnt++;

	/* disable all filters used by this port */
	bcmasp_disable_all_filters(intf);

	spin_unlock_bh(&intf->parent->mda_lock);
}

static void bcmasp_clean_txcb(struct bcmasp_intf *intf, int index)
{
	struct bcmasp_tx_cb *txcb = &intf->tx_cbs[index];

	txcb->skb = NULL;
	dma_unmap_addr_set(txcb, dma_addr, 0);
	dma_unmap_len_set(txcb, dma_len, 0);
	txcb->last = false;
}

static int tx_spb_ring_full(struct bcmasp_intf *intf, int cnt)
{
	int next_index, i;

	/* Check if we have enough room for cnt descriptors */
	for (i = 0; i < cnt; i++) {
		next_index = incr_ring(intf->tx_spb_index, DESC_RING_COUNT);
		if (next_index == intf->tx_spb_clean_index)
			return 1;
	}

	return 0;
}

static struct sk_buff *bcmasp_csum_offload(struct net_device *dev,
					   struct sk_buff *skb,
					   bool *csum_hw)
{
	struct bcmasp_intf *intf = netdev_priv(dev);
	u32 header = 0, header2 = 0, epkt = 0;
	struct bcmasp_pkt_offload *offload;
	unsigned int header_cnt = 0;
	u8 ip_proto;
	int ret;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return skb;

	ret = skb_cow_head(skb, sizeof(*offload));
	if (ret < 0) {
		intf->mib.tx_realloc_offload_failed++;
		goto help;
	}

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		header |= PKT_OFFLOAD_HDR_SIZE_2((ip_hdrlen(skb) >> 8) & 0xf);
		header2 |= PKT_OFFLOAD_HDR2_SIZE_2(ip_hdrlen(skb) & 0xff);
		epkt |= PKT_OFFLOAD_EPKT_IP(0) | PKT_OFFLOAD_EPKT_CSUM_L2;
		ip_proto = ip_hdr(skb)->protocol;
		header_cnt += 2;
		break;
	case htons(ETH_P_IPV6):
		header |= PKT_OFFLOAD_HDR_SIZE_2((IP6_HLEN >> 8) & 0xf);
		header2 |= PKT_OFFLOAD_HDR2_SIZE_2(IP6_HLEN & 0xff);
		epkt |= PKT_OFFLOAD_EPKT_IP(1) | PKT_OFFLOAD_EPKT_CSUM_L2;
		ip_proto = ipv6_hdr(skb)->nexthdr;
		header_cnt += 2;
		break;
	default:
		goto help;
	}

	switch (ip_proto) {
	case IPPROTO_TCP:
		header2 |= PKT_OFFLOAD_HDR2_SIZE_3(tcp_hdrlen(skb));
		epkt |= PKT_OFFLOAD_EPKT_TP(0) | PKT_OFFLOAD_EPKT_CSUM_L3;
		header_cnt++;
		break;
	case IPPROTO_UDP:
		header2 |= PKT_OFFLOAD_HDR2_SIZE_3(UDP_HLEN);
		epkt |= PKT_OFFLOAD_EPKT_TP(1) | PKT_OFFLOAD_EPKT_CSUM_L3;
		header_cnt++;
		break;
	default:
		goto help;
	}

	offload = (struct bcmasp_pkt_offload *)skb_push(skb, sizeof(*offload));

	header |= PKT_OFFLOAD_HDR_OP | PKT_OFFLOAD_HDR_COUNT(header_cnt) |
		  PKT_OFFLOAD_HDR_SIZE_1(ETH_HLEN);
	epkt |= PKT_OFFLOAD_EPKT_OP;

	offload->nop = htonl(PKT_OFFLOAD_NOP);
	offload->header = htonl(header);
	offload->header2 = htonl(header2);
	offload->epkt = htonl(epkt);
	offload->end = htonl(PKT_OFFLOAD_END_OP);
	*csum_hw = true;

	return skb;

help:
	skb_checksum_help(skb);

	return skb;
}

static unsigned long bcmasp_rx_edpkt_dma_rq(struct bcmasp_intf *intf)
{
	return rx_edpkt_dma_rq(intf, RX_EDPKT_DMA_VALID);
}

static void bcmasp_rx_edpkt_cfg_wq(struct bcmasp_intf *intf, dma_addr_t addr)
{
	rx_edpkt_cfg_wq(intf, addr, RX_EDPKT_RING_BUFFER_READ);
}

static void bcmasp_rx_edpkt_dma_wq(struct bcmasp_intf *intf, dma_addr_t addr)
{
	rx_edpkt_dma_wq(intf, addr, RX_EDPKT_DMA_READ);
}

static unsigned long bcmasp_tx_spb_dma_rq(struct bcmasp_intf *intf)
{
	return tx_spb_dma_rq(intf, TX_SPB_DMA_READ);
}

static void bcmasp_tx_spb_dma_wq(struct bcmasp_intf *intf, dma_addr_t addr)
{
	tx_spb_dma_wq(intf, addr, TX_SPB_DMA_VALID);
}

static const struct bcmasp_intf_ops bcmasp_intf_ops = {
	.rx_desc_read = bcmasp_rx_edpkt_dma_rq,
	.rx_buffer_write = bcmasp_rx_edpkt_cfg_wq,
	.rx_desc_write = bcmasp_rx_edpkt_dma_wq,
	.tx_read = bcmasp_tx_spb_dma_rq,
	.tx_write = bcmasp_tx_spb_dma_wq,
};

static netdev_tx_t bcmasp_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bcmasp_intf *intf = netdev_priv(dev);
	unsigned int total_bytes, size;
	int spb_index, nr_frags, i, j;
	struct bcmasp_tx_cb *txcb;
	dma_addr_t mapping, valid;
	struct bcmasp_desc *desc;
	bool csum_hw = false;
	struct device *kdev;
	skb_frag_t *frag;

	kdev = &intf->parent->pdev->dev;

	nr_frags = skb_shinfo(skb)->nr_frags;

	if (tx_spb_ring_full(intf, nr_frags + 1)) {
		netif_stop_queue(dev);
		if (net_ratelimit())
			netdev_err(dev, "Tx Ring Full!\n");
		return NETDEV_TX_BUSY;
	}

	/* Save skb len before adding csum offload header */
	total_bytes = skb->len;
	skb = bcmasp_csum_offload(dev, skb, &csum_hw);
	if (!skb)
		return NETDEV_TX_OK;

	spb_index = intf->tx_spb_index;
	valid = intf->tx_spb_dma_valid;
	for (i = 0; i <= nr_frags; i++) {
		if (!i) {
			size = skb_headlen(skb);
			if (!nr_frags && size < (ETH_ZLEN + ETH_FCS_LEN)) {
				if (skb_put_padto(skb, ETH_ZLEN + ETH_FCS_LEN))
					return NETDEV_TX_OK;
				size = skb->len;
			}
			mapping = dma_map_single(kdev, skb->data, size,
						 DMA_TO_DEVICE);
		} else {
			frag = &skb_shinfo(skb)->frags[i - 1];
			size = skb_frag_size(frag);
			mapping = skb_frag_dma_map(kdev, frag, 0, size,
						   DMA_TO_DEVICE);
		}

		if (dma_mapping_error(kdev, mapping)) {
			intf->mib.tx_dma_failed++;
			spb_index = intf->tx_spb_index;
			for (j = 0; j < i; j++) {
				bcmasp_clean_txcb(intf, spb_index);
				spb_index = incr_ring(spb_index,
						      DESC_RING_COUNT);
			}
			/* Rewind so we do not have a hole */
			spb_index = intf->tx_spb_index;
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
		}

		txcb = &intf->tx_cbs[spb_index];
		desc = &intf->tx_spb_cpu[spb_index];
		memset(desc, 0, sizeof(*desc));
		txcb->skb = skb;
		txcb->bytes_sent = total_bytes;
		dma_unmap_addr_set(txcb, dma_addr, mapping);
		dma_unmap_len_set(txcb, dma_len, size);
		if (!i) {
			desc->flags |= DESC_SOF;
			if (csum_hw)
				desc->flags |= DESC_EPKT_CMD;
		}

		if (i == nr_frags) {
			desc->flags |= DESC_EOF;
			txcb->last = true;
		}

		desc->buf = mapping;
		desc->size = size;
		desc->flags |= DESC_INT_EN;

		netif_dbg(intf, tx_queued, dev,
			  "%s dma_buf=%pad dma_len=0x%x flags=0x%x index=0x%x\n",
			  __func__, &mapping, desc->size, desc->flags,
			  spb_index);

		spb_index = incr_ring(spb_index, DESC_RING_COUNT);
		valid = incr_last_byte(valid, intf->tx_spb_dma_addr,
				       DESC_RING_COUNT);
	}

	/* Ensure all descriptors have been written to DRAM for the
	 * hardware to see up-to-date contents.
	 */
	wmb();

	intf->tx_spb_index = spb_index;
	intf->tx_spb_dma_valid = valid;

	skb_tx_timestamp(skb);

	bcmasp_intf_tx_write(intf, intf->tx_spb_dma_valid);

	if (tx_spb_ring_full(intf, MAX_SKB_FRAGS + 1))
		netif_stop_queue(dev);

	return NETDEV_TX_OK;
}

static void bcmasp_netif_start(struct net_device *dev)
{
	struct bcmasp_intf *intf = netdev_priv(dev);

	bcmasp_set_rx_mode(dev);
	napi_enable(&intf->tx_napi);
	napi_enable(&intf->rx_napi);

	bcmasp_enable_rx_irq(intf, 1);
	bcmasp_enable_tx_irq(intf, 1);
	bcmasp_enable_phy_irq(intf, 1);

	phy_start(dev->phydev);
}

static void umac_reset(struct bcmasp_intf *intf)
{
	umac_wl(intf, 0x0, UMC_CMD);
	umac_wl(intf, UMC_CMD_SW_RESET, UMC_CMD);
	usleep_range(10, 100);
	/* We hold the umac in reset and bring it out of
	 * reset when phy link is up.
	 */
}

static void umac_set_hw_addr(struct bcmasp_intf *intf,
			     const unsigned char *addr)
{
	u32 mac0 = (addr[0] << 24) | (addr[1] << 16) | (addr[2] << 8) |
		    addr[3];
	u32 mac1 = (addr[4] << 8) | addr[5];

	umac_wl(intf, mac0, UMC_MAC0);
	umac_wl(intf, mac1, UMC_MAC1);
}

static void umac_enable_set(struct bcmasp_intf *intf, u32 mask,
			    unsigned int enable)
{
	u32 reg;

	reg = umac_rl(intf, UMC_CMD);
	if (reg & UMC_CMD_SW_RESET)
		return;
	if (enable)
		reg |= mask;
	else
		reg &= ~mask;
	umac_wl(intf, reg, UMC_CMD);

	/* UniMAC stops on a packet boundary, wait for a full-sized packet
	 * to be processed (1 msec).
	 */
	if (enable == 0)
		usleep_range(1000, 2000);
}

static void umac_init(struct bcmasp_intf *intf)
{
	umac_wl(intf, 0x800, UMC_FRM_LEN);
	umac_wl(intf, 0xffff, UMC_PAUSE_CNTRL);
	umac_wl(intf, 0x800, UMC_RX_MAX_PKT_SZ);
}

static int bcmasp_tx_reclaim(struct bcmasp_intf *intf)
{
	struct bcmasp_intf_stats64 *stats = &intf->stats64;
	struct device *kdev = &intf->parent->pdev->dev;
	unsigned long read, released = 0;
	struct bcmasp_tx_cb *txcb;
	struct bcmasp_desc *desc;
	dma_addr_t mapping;

	read = bcmasp_intf_tx_read(intf);
	while (intf->tx_spb_dma_read != read) {
		txcb = &intf->tx_cbs[intf->tx_spb_clean_index];
		mapping = dma_unmap_addr(txcb, dma_addr);

		dma_unmap_single(kdev, mapping,
				 dma_unmap_len(txcb, dma_len),
				 DMA_TO_DEVICE);

		if (txcb->last) {
			dev_consume_skb_any(txcb->skb);

			u64_stats_update_begin(&stats->syncp);
			u64_stats_inc(&stats->tx_packets);
			u64_stats_add(&stats->tx_bytes, txcb->bytes_sent);
			u64_stats_update_end(&stats->syncp);
		}

		desc = &intf->tx_spb_cpu[intf->tx_spb_clean_index];

		netif_dbg(intf, tx_done, intf->ndev,
			  "%s dma_buf=%pad dma_len=0x%x flags=0x%x c_index=0x%x\n",
			  __func__, &mapping, desc->size, desc->flags,
			  intf->tx_spb_clean_index);

		bcmasp_clean_txcb(intf, intf->tx_spb_clean_index);
		released++;

		intf->tx_spb_clean_index = incr_ring(intf->tx_spb_clean_index,
						     DESC_RING_COUNT);
		intf->tx_spb_dma_read = incr_first_byte(intf->tx_spb_dma_read,
							intf->tx_spb_dma_addr,
							DESC_RING_COUNT);
	}

	return released;
}

static int bcmasp_tx_poll(struct napi_struct *napi, int budget)
{
	struct bcmasp_intf *intf =
		container_of(napi, struct bcmasp_intf, tx_napi);
	int released = 0;

	released = bcmasp_tx_reclaim(intf);

	napi_complete(&intf->tx_napi);

	bcmasp_enable_tx_irq(intf, 1);

	if (released)
		netif_wake_queue(intf->ndev);

	return 0;
}

static int bcmasp_rx_poll(struct napi_struct *napi, int budget)
{
	struct bcmasp_intf *intf =
		container_of(napi, struct bcmasp_intf, rx_napi);
	struct bcmasp_intf_stats64 *stats = &intf->stats64;
	struct device *kdev = &intf->parent->pdev->dev;
	unsigned long processed = 0;
	struct bcmasp_desc *desc;
	struct sk_buff *skb;
	dma_addr_t valid;
	void *data;
	u64 flags;
	u32 len;

	valid = bcmasp_intf_rx_desc_read(intf) + 1;
	if (valid == intf->rx_edpkt_dma_addr + DESC_RING_SIZE)
		valid = intf->rx_edpkt_dma_addr;

	while ((processed < budget) && (valid != intf->rx_edpkt_dma_read)) {
		desc = &intf->rx_edpkt_cpu[intf->rx_edpkt_index];

		/* Ensure that descriptor has been fully written to DRAM by
		 * hardware before reading by the CPU
		 */
		rmb();

		/* Calculate virt addr by offsetting from physical addr */
		data = intf->rx_ring_cpu +
			(DESC_ADDR(desc->buf) - intf->rx_ring_dma);

		flags = DESC_FLAGS(desc->buf);
		if (unlikely(flags & (DESC_CRC_ERR | DESC_RX_SYM_ERR))) {
			if (net_ratelimit()) {
				netif_err(intf, rx_status, intf->ndev,
					  "flags=0x%llx\n", flags);
			}

			u64_stats_update_begin(&stats->syncp);
			if (flags & DESC_CRC_ERR)
				u64_stats_inc(&stats->rx_crc_errs);
			if (flags & DESC_RX_SYM_ERR)
				u64_stats_inc(&stats->rx_sym_errs);
			u64_stats_update_end(&stats->syncp);

			goto next;
		}

		dma_sync_single_for_cpu(kdev, DESC_ADDR(desc->buf), desc->size,
					DMA_FROM_DEVICE);

		len = desc->size;

		skb = napi_alloc_skb(napi, len);
		if (!skb) {
			u64_stats_update_begin(&stats->syncp);
			u64_stats_inc(&stats->rx_dropped);
			u64_stats_update_end(&stats->syncp);
			intf->mib.alloc_rx_skb_failed++;

			goto next;
		}

		skb_put(skb, len);
		memcpy(skb->data, data, len);

		skb_pull(skb, 2);
		len -= 2;
		if (likely(intf->crc_fwd)) {
			skb_trim(skb, len - ETH_FCS_LEN);
			len -= ETH_FCS_LEN;
		}

		if ((intf->ndev->features & NETIF_F_RXCSUM) &&
		    (desc->buf & DESC_CHKSUM))
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		skb->protocol = eth_type_trans(skb, intf->ndev);

		napi_gro_receive(napi, skb);

		u64_stats_update_begin(&stats->syncp);
		u64_stats_inc(&stats->rx_packets);
		u64_stats_add(&stats->rx_bytes, len);
		u64_stats_update_end(&stats->syncp);

next:
		bcmasp_intf_rx_buffer_write(intf, (DESC_ADDR(desc->buf) +
					    desc->size));

		processed++;
		intf->rx_edpkt_dma_read =
			incr_first_byte(intf->rx_edpkt_dma_read,
					intf->rx_edpkt_dma_addr,
					DESC_RING_COUNT);
		intf->rx_edpkt_index = incr_ring(intf->rx_edpkt_index,
						 DESC_RING_COUNT);
	}

	bcmasp_intf_rx_desc_write(intf, intf->rx_edpkt_dma_read);

	if (processed < budget) {
		napi_complete_done(&intf->rx_napi, processed);
		bcmasp_enable_rx_irq(intf, 1);
	}

	return processed;
}

static void bcmasp_adj_link(struct net_device *dev)
{
	struct bcmasp_intf *intf = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;
	u32 cmd_bits = 0, reg;
	int changed = 0;
	bool active;

	if (intf->old_link != phydev->link) {
		changed = 1;
		intf->old_link = phydev->link;
	}

	if (intf->old_duplex != phydev->duplex) {
		changed = 1;
		intf->old_duplex = phydev->duplex;
	}

	switch (phydev->speed) {
	case SPEED_2500:
		cmd_bits = UMC_CMD_SPEED_2500;
		break;
	case SPEED_1000:
		cmd_bits = UMC_CMD_SPEED_1000;
		break;
	case SPEED_100:
		cmd_bits = UMC_CMD_SPEED_100;
		break;
	case SPEED_10:
		cmd_bits = UMC_CMD_SPEED_10;
		break;
	default:
		break;
	}
	cmd_bits <<= UMC_CMD_SPEED_SHIFT;

	if (phydev->duplex == DUPLEX_HALF)
		cmd_bits |= UMC_CMD_HD_EN;

	if (intf->old_pause != phydev->pause) {
		changed = 1;
		intf->old_pause = phydev->pause;
	}

	if (!phydev->pause)
		cmd_bits |= UMC_CMD_RX_PAUSE_IGNORE | UMC_CMD_TX_PAUSE_IGNORE;

	if (!changed)
		return;

	if (phydev->link) {
		reg = umac_rl(intf, UMC_CMD);
		reg &= ~((UMC_CMD_SPEED_MASK << UMC_CMD_SPEED_SHIFT) |
			UMC_CMD_HD_EN | UMC_CMD_RX_PAUSE_IGNORE |
			UMC_CMD_TX_PAUSE_IGNORE);
		reg |= cmd_bits;
		if (reg & UMC_CMD_SW_RESET) {
			reg &= ~UMC_CMD_SW_RESET;
			umac_wl(intf, reg, UMC_CMD);
			udelay(2);
			reg |= UMC_CMD_TX_EN | UMC_CMD_RX_EN | UMC_CMD_PROMISC;
		}
		umac_wl(intf, reg, UMC_CMD);

		active = phy_init_eee(phydev, 0) >= 0;
		bcmasp_eee_enable_set(intf, active);
	}

	reg = rgmii_rl(intf, RGMII_OOB_CNTRL);
	if (phydev->link)
		reg |= RGMII_LINK;
	else
		reg &= ~RGMII_LINK;
	rgmii_wl(intf, reg, RGMII_OOB_CNTRL);

	if (changed)
		phy_print_status(phydev);
}

static int bcmasp_alloc_buffers(struct bcmasp_intf *intf)
{
	struct device *kdev = &intf->parent->pdev->dev;
	struct page *buffer_pg;

	/* Alloc RX */
	intf->rx_buf_order = get_order(RING_BUFFER_SIZE);
	buffer_pg = alloc_pages(GFP_KERNEL, intf->rx_buf_order);
	if (!buffer_pg)
		return -ENOMEM;

	intf->rx_ring_cpu = page_to_virt(buffer_pg);
	intf->rx_ring_dma = dma_map_page(kdev, buffer_pg, 0, RING_BUFFER_SIZE,
					 DMA_FROM_DEVICE);
	if (dma_mapping_error(kdev, intf->rx_ring_dma))
		goto free_rx_buffer;

	intf->rx_edpkt_cpu = dma_alloc_coherent(kdev, DESC_RING_SIZE,
						&intf->rx_edpkt_dma_addr, GFP_KERNEL);
	if (!intf->rx_edpkt_cpu)
		goto free_rx_buffer_dma;

	/* Alloc TX */
	intf->tx_spb_cpu = dma_alloc_coherent(kdev, DESC_RING_SIZE,
					      &intf->tx_spb_dma_addr, GFP_KERNEL);
	if (!intf->tx_spb_cpu)
		goto free_rx_edpkt_dma;

	intf->tx_cbs = kcalloc(DESC_RING_COUNT, sizeof(struct bcmasp_tx_cb),
			       GFP_KERNEL);
	if (!intf->tx_cbs)
		goto free_tx_spb_dma;

	return 0;

free_tx_spb_dma:
	dma_free_coherent(kdev, DESC_RING_SIZE, intf->tx_spb_cpu,
			  intf->tx_spb_dma_addr);
free_rx_edpkt_dma:
	dma_free_coherent(kdev, DESC_RING_SIZE, intf->rx_edpkt_cpu,
			  intf->rx_edpkt_dma_addr);
free_rx_buffer_dma:
	dma_unmap_page(kdev, intf->rx_ring_dma, RING_BUFFER_SIZE,
		       DMA_FROM_DEVICE);
free_rx_buffer:
	__free_pages(buffer_pg, intf->rx_buf_order);

	return -ENOMEM;
}

static void bcmasp_reclaim_free_buffers(struct bcmasp_intf *intf)
{
	struct device *kdev = &intf->parent->pdev->dev;

	/* RX buffers */
	dma_free_coherent(kdev, DESC_RING_SIZE, intf->rx_edpkt_cpu,
			  intf->rx_edpkt_dma_addr);
	dma_unmap_page(kdev, intf->rx_ring_dma, RING_BUFFER_SIZE,
		       DMA_FROM_DEVICE);
	__free_pages(virt_to_page(intf->rx_ring_cpu), intf->rx_buf_order);

	/* TX buffers */
	dma_free_coherent(kdev, DESC_RING_SIZE, intf->tx_spb_cpu,
			  intf->tx_spb_dma_addr);
	kfree(intf->tx_cbs);
}

static void bcmasp_init_rx(struct bcmasp_intf *intf)
{
	/* Restart from index 0 */
	intf->rx_ring_dma_valid = intf->rx_ring_dma + RING_BUFFER_SIZE - 1;
	intf->rx_edpkt_dma_valid = intf->rx_edpkt_dma_addr + (DESC_RING_SIZE - 1);
	intf->rx_edpkt_dma_read = intf->rx_edpkt_dma_addr;
	intf->rx_edpkt_index = 0;

	/* Make sure channels are disabled */
	rx_edpkt_cfg_wl(intf, 0x0, RX_EDPKT_CFG_ENABLE);

	/* Rx SPB */
	rx_edpkt_cfg_wq(intf, intf->rx_ring_dma, RX_EDPKT_RING_BUFFER_READ);
	rx_edpkt_cfg_wq(intf, intf->rx_ring_dma, RX_EDPKT_RING_BUFFER_WRITE);
	rx_edpkt_cfg_wq(intf, intf->rx_ring_dma, RX_EDPKT_RING_BUFFER_BASE);
	rx_edpkt_cfg_wq(intf, intf->rx_ring_dma_valid,
			RX_EDPKT_RING_BUFFER_END);
	rx_edpkt_cfg_wq(intf, intf->rx_ring_dma_valid,
			RX_EDPKT_RING_BUFFER_VALID);

	/* EDPKT */
	rx_edpkt_cfg_wl(intf, (RX_EDPKT_CFG_CFG0_RBUF_4K <<
			RX_EDPKT_CFG_CFG0_DBUF_SHIFT) |
		       (RX_EDPKT_CFG_CFG0_64_ALN <<
			RX_EDPKT_CFG_CFG0_BALN_SHIFT) |
		       (RX_EDPKT_CFG_CFG0_EFRM_STUF),
			RX_EDPKT_CFG_CFG0);
	rx_edpkt_dma_wq(intf, intf->rx_edpkt_dma_addr, RX_EDPKT_DMA_WRITE);
	rx_edpkt_dma_wq(intf, intf->rx_edpkt_dma_addr, RX_EDPKT_DMA_READ);
	rx_edpkt_dma_wq(intf, intf->rx_edpkt_dma_addr, RX_EDPKT_DMA_BASE);
	rx_edpkt_dma_wq(intf, intf->rx_edpkt_dma_valid, RX_EDPKT_DMA_END);
	rx_edpkt_dma_wq(intf, intf->rx_edpkt_dma_valid, RX_EDPKT_DMA_VALID);

	umac2fb_wl(intf, UMAC2FB_CFG_DEFAULT_EN | ((intf->channel + 11) <<
		   UMAC2FB_CFG_CHID_SHIFT) | (0xd << UMAC2FB_CFG_OK_SEND_SHIFT),
		   UMAC2FB_CFG);
}


static void bcmasp_init_tx(struct bcmasp_intf *intf)
{
	/* Restart from index 0 */
	intf->tx_spb_dma_valid = intf->tx_spb_dma_addr + DESC_RING_SIZE - 1;
	intf->tx_spb_dma_read = intf->tx_spb_dma_addr;
	intf->tx_spb_index = 0;
	intf->tx_spb_clean_index = 0;
	memset(intf->tx_cbs, 0, sizeof(struct bcmasp_tx_cb) * DESC_RING_COUNT);

	/* Make sure channels are disabled */
	tx_spb_ctrl_wl(intf, 0x0, TX_SPB_CTRL_ENABLE);
	tx_epkt_core_wl(intf, 0x0, TX_EPKT_C_CFG_MISC);

	/* Tx SPB */
	tx_spb_ctrl_wl(intf, ((intf->channel + 8) << TX_SPB_CTRL_XF_BID_SHIFT),
		       TX_SPB_CTRL_XF_CTRL2);
	tx_pause_ctrl_wl(intf, (1 << (intf->channel + 8)), TX_PAUSE_MAP_VECTOR);
	tx_spb_top_wl(intf, 0x1e, TX_SPB_TOP_BLKOUT);
	tx_spb_top_wl(intf, 0x0, TX_SPB_TOP_SPRE_BW_CTRL);

	tx_spb_dma_wq(intf, intf->tx_spb_dma_addr, TX_SPB_DMA_READ);
	tx_spb_dma_wq(intf, intf->tx_spb_dma_addr, TX_SPB_DMA_BASE);
	tx_spb_dma_wq(intf, intf->tx_spb_dma_valid, TX_SPB_DMA_END);
	tx_spb_dma_wq(intf, intf->tx_spb_dma_valid, TX_SPB_DMA_VALID);
}

static void bcmasp_ephy_enable_set(struct bcmasp_intf *intf, bool enable)
{
	u32 mask = RGMII_EPHY_CFG_IDDQ_BIAS | RGMII_EPHY_CFG_EXT_PWRDOWN |
		   RGMII_EPHY_CFG_IDDQ_GLOBAL;
	u32 reg;

	reg = rgmii_rl(intf, RGMII_EPHY_CNTRL);
	if (enable) {
		reg &= ~RGMII_EPHY_CK25_DIS;
		rgmii_wl(intf, reg, RGMII_EPHY_CNTRL);
		mdelay(1);

		reg &= ~mask;
		reg |= RGMII_EPHY_RESET;
		rgmii_wl(intf, reg, RGMII_EPHY_CNTRL);
		mdelay(1);

		reg &= ~RGMII_EPHY_RESET;
	} else {
		reg |= mask | RGMII_EPHY_RESET;
		rgmii_wl(intf, reg, RGMII_EPHY_CNTRL);
		mdelay(1);
		reg |= RGMII_EPHY_CK25_DIS;
	}
	rgmii_wl(intf, reg, RGMII_EPHY_CNTRL);
	mdelay(1);

	/* Set or clear the LED control override to avoid lighting up LEDs
	 * while the EPHY is powered off and drawing unnecessary current.
	 */
	reg = rgmii_rl(intf, RGMII_SYS_LED_CNTRL);
	if (enable)
		reg &= ~RGMII_SYS_LED_CNTRL_LINK_OVRD;
	else
		reg |= RGMII_SYS_LED_CNTRL_LINK_OVRD;
	rgmii_wl(intf, reg, RGMII_SYS_LED_CNTRL);
}

static void bcmasp_rgmii_mode_en_set(struct bcmasp_intf *intf, bool enable)
{
	u32 reg;

	reg = rgmii_rl(intf, RGMII_OOB_CNTRL);
	reg &= ~RGMII_OOB_DIS;
	if (enable)
		reg |= RGMII_MODE_EN;
	else
		reg &= ~RGMII_MODE_EN;
	rgmii_wl(intf, reg, RGMII_OOB_CNTRL);
}

static void bcmasp_netif_deinit(struct net_device *dev)
{
	struct bcmasp_intf *intf = netdev_priv(dev);
	u32 reg, timeout = 1000;

	napi_disable(&intf->tx_napi);

	bcmasp_enable_tx(intf, 0);

	/* Flush any TX packets in the pipe */
	tx_spb_dma_wl(intf, TX_SPB_DMA_FIFO_FLUSH, TX_SPB_DMA_FIFO_CTRL);
	do {
		reg = tx_spb_dma_rl(intf, TX_SPB_DMA_FIFO_STATUS);
		if (!(reg & TX_SPB_DMA_FIFO_FLUSH))
			break;
		usleep_range(1000, 2000);
	} while (timeout-- > 0);
	tx_spb_dma_wl(intf, 0x0, TX_SPB_DMA_FIFO_CTRL);

	bcmasp_tx_reclaim(intf);

	umac_enable_set(intf, UMC_CMD_TX_EN, 0);

	phy_stop(dev->phydev);

	umac_enable_set(intf, UMC_CMD_RX_EN, 0);

	bcmasp_flush_rx_port(intf);
	usleep_range(1000, 2000);
	bcmasp_enable_rx(intf, 0);

	napi_disable(&intf->rx_napi);

	/* Disable interrupts */
	bcmasp_enable_tx_irq(intf, 0);
	bcmasp_enable_rx_irq(intf, 0);
	bcmasp_enable_phy_irq(intf, 0);

	netif_napi_del(&intf->tx_napi);
	netif_napi_del(&intf->rx_napi);
}

static int bcmasp_stop(struct net_device *dev)
{
	struct bcmasp_intf *intf = netdev_priv(dev);

	netif_dbg(intf, ifdown, dev, "bcmasp stop\n");

	/* Stop tx from updating HW */
	netif_tx_disable(dev);

	bcmasp_netif_deinit(dev);

	bcmasp_reclaim_free_buffers(intf);

	phy_disconnect(dev->phydev);

	/* Disable internal EPHY or external PHY */
	if (intf->internal_phy)
		bcmasp_ephy_enable_set(intf, false);
	else
		bcmasp_rgmii_mode_en_set(intf, false);

	/* Disable the interface clocks */
	bcmasp_core_clock_set_intf(intf, false);

	clk_disable_unprepare(intf->parent->clk);

	return 0;
}

static void bcmasp_configure_port(struct bcmasp_intf *intf)
{
	u32 reg, id_mode_dis = 0;

	reg = rgmii_rl(intf, RGMII_PORT_CNTRL);
	reg &= ~RGMII_PORT_MODE_MASK;

	switch (intf->phy_interface) {
	case PHY_INTERFACE_MODE_RGMII:
		/* RGMII_NO_ID: TXC transitions at the same time as TXD
		 *		(requires PCB or receiver-side delay)
		 * RGMII:	Add 2ns delay on TXC (90 degree shift)
		 *
		 * ID is implicitly disabled for 100Mbps (RG)MII operation.
		 */
		id_mode_dis = RGMII_ID_MODE_DIS;
		fallthrough;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		reg |= RGMII_PORT_MODE_EXT_GPHY;
		break;
	case PHY_INTERFACE_MODE_MII:
		reg |= RGMII_PORT_MODE_EXT_EPHY;
		break;
	default:
		break;
	}

	if (intf->internal_phy)
		reg |= RGMII_PORT_MODE_EPHY;

	rgmii_wl(intf, reg, RGMII_PORT_CNTRL);

	reg = rgmii_rl(intf, RGMII_OOB_CNTRL);
	reg &= ~RGMII_ID_MODE_DIS;
	reg |= id_mode_dis;
	rgmii_wl(intf, reg, RGMII_OOB_CNTRL);
}

static int bcmasp_netif_init(struct net_device *dev, bool phy_connect)
{
	struct bcmasp_intf *intf = netdev_priv(dev);
	phy_interface_t phy_iface = intf->phy_interface;
	u32 phy_flags = PHY_BRCM_AUTO_PWRDWN_ENABLE |
			PHY_BRCM_DIS_TXCRXC_NOENRGY |
			PHY_BRCM_IDDQ_SUSPEND;
	struct phy_device *phydev = NULL;
	int ret;

	/* Always enable interface clocks */
	bcmasp_core_clock_set_intf(intf, true);

	/* Enable internal PHY or external PHY before any MAC activity */
	if (intf->internal_phy)
		bcmasp_ephy_enable_set(intf, true);
	else
		bcmasp_rgmii_mode_en_set(intf, true);
	bcmasp_configure_port(intf);

	/* This is an ugly quirk but we have not been correctly
	 * interpreting the phy_interface values and we have done that
	 * across different drivers, so at least we are consistent in
	 * our mistakes.
	 *
	 * When the Generic PHY driver is in use either the PHY has
	 * been strapped or programmed correctly by the boot loader so
	 * we should stick to our incorrect interpretation since we
	 * have validated it.
	 *
	 * Now when a dedicated PHY driver is in use, we need to
	 * reverse the meaning of the phy_interface_mode values to
	 * something that the PHY driver will interpret and act on such
	 * that we have two mistakes canceling themselves so to speak.
	 * We only do this for the two modes that GENET driver
	 * officially supports on Broadcom STB chips:
	 * PHY_INTERFACE_MODE_RGMII and PHY_INTERFACE_MODE_RGMII_TXID.
	 * Other modes are not *officially* supported with the boot
	 * loader and the scripted environment generating Device Tree
	 * blobs for those platforms.
	 *
	 * Note that internal PHY and fixed-link configurations are not
	 * affected because they use different phy_interface_t values
	 * or the Generic PHY driver.
	 */
	switch (phy_iface) {
	case PHY_INTERFACE_MODE_RGMII:
		phy_iface = PHY_INTERFACE_MODE_RGMII_ID;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		phy_iface = PHY_INTERFACE_MODE_RGMII_RXID;
		break;
	default:
		break;
	}

	if (phy_connect) {
		phydev = of_phy_connect(dev, intf->phy_dn,
					bcmasp_adj_link, phy_flags,
					phy_iface);
		if (!phydev) {
			ret = -ENODEV;
			netdev_err(dev, "could not attach to PHY\n");
			goto err_phy_disable;
		}

		if (intf->internal_phy)
			dev->phydev->irq = PHY_MAC_INTERRUPT;

		/* Indicate that the MAC is responsible for PHY PM */
		phydev->mac_managed_pm = true;
	}

	umac_reset(intf);

	umac_init(intf);

	umac_set_hw_addr(intf, dev->dev_addr);

	intf->old_duplex = -1;
	intf->old_link = -1;
	intf->old_pause = -1;

	bcmasp_init_tx(intf);
	netif_napi_add_tx(intf->ndev, &intf->tx_napi, bcmasp_tx_poll);
	bcmasp_enable_tx(intf, 1);

	bcmasp_init_rx(intf);
	netif_napi_add(intf->ndev, &intf->rx_napi, bcmasp_rx_poll);
	bcmasp_enable_rx(intf, 1);

	intf->crc_fwd = !!(umac_rl(intf, UMC_CMD) & UMC_CMD_CRC_FWD);

	bcmasp_netif_start(dev);

	netif_start_queue(dev);

	return 0;

err_phy_disable:
	if (intf->internal_phy)
		bcmasp_ephy_enable_set(intf, false);
	else
		bcmasp_rgmii_mode_en_set(intf, false);
	return ret;
}

static int bcmasp_open(struct net_device *dev)
{
	struct bcmasp_intf *intf = netdev_priv(dev);
	int ret;

	netif_dbg(intf, ifup, dev, "bcmasp open\n");

	ret = bcmasp_alloc_buffers(intf);
	if (ret)
		return ret;

	ret = clk_prepare_enable(intf->parent->clk);
	if (ret)
		goto err_free_mem;

	ret = bcmasp_netif_init(dev, true);
	if (ret) {
		clk_disable_unprepare(intf->parent->clk);
		goto err_free_mem;
	}

	return ret;

err_free_mem:
	bcmasp_reclaim_free_buffers(intf);

	return ret;
}

static void bcmasp_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct bcmasp_intf *intf = netdev_priv(dev);

	netif_dbg(intf, tx_err, dev, "transmit timeout!\n");
	intf->mib.tx_timeout_cnt++;
}

static int bcmasp_get_phys_port_name(struct net_device *dev,
				     char *name, size_t len)
{
	struct bcmasp_intf *intf = netdev_priv(dev);

	if (snprintf(name, len, "p%d", intf->port) >= len)
		return -EINVAL;

	return 0;
}

static void bcmasp_get_stats64(struct net_device *dev,
			       struct rtnl_link_stats64 *stats)
{
	struct bcmasp_intf *intf = netdev_priv(dev);
	struct bcmasp_intf_stats64 *lstats;
	unsigned int start;

	lstats = &intf->stats64;

	do {
		start = u64_stats_fetch_begin(&lstats->syncp);
		stats->rx_packets = u64_stats_read(&lstats->rx_packets);
		stats->rx_bytes = u64_stats_read(&lstats->rx_bytes);
		stats->rx_dropped = u64_stats_read(&lstats->rx_dropped);
		stats->rx_crc_errors = u64_stats_read(&lstats->rx_crc_errs);
		stats->rx_frame_errors = u64_stats_read(&lstats->rx_sym_errs);
		stats->rx_errors = stats->rx_crc_errors + stats->rx_frame_errors;

		stats->tx_packets = u64_stats_read(&lstats->tx_packets);
		stats->tx_bytes = u64_stats_read(&lstats->tx_bytes);
	} while (u64_stats_fetch_retry(&lstats->syncp, start));
}

static const struct net_device_ops bcmasp_netdev_ops = {
	.ndo_open		= bcmasp_open,
	.ndo_stop		= bcmasp_stop,
	.ndo_start_xmit		= bcmasp_xmit,
	.ndo_tx_timeout		= bcmasp_tx_timeout,
	.ndo_set_rx_mode	= bcmasp_set_rx_mode,
	.ndo_get_phys_port_name	= bcmasp_get_phys_port_name,
	.ndo_eth_ioctl		= phy_do_ioctl_running,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_get_stats64	= bcmasp_get_stats64,
};

static void bcmasp_map_res(struct bcmasp_priv *priv, struct bcmasp_intf *intf)
{
	/* Per port */
	intf->res.umac = priv->base + UMC_OFFSET(intf);
	intf->res.umac2fb = priv->base + (priv->hw_info->umac2fb +
					  (intf->port * 0x4));
	intf->res.rgmii = priv->base + RGMII_OFFSET(intf);

	/* Per ch */
	intf->tx_spb_dma = priv->base + TX_SPB_DMA_OFFSET(intf);
	intf->res.tx_spb_ctrl = priv->base + TX_SPB_CTRL_OFFSET(intf);
	intf->res.tx_spb_top = priv->base + TX_SPB_TOP_OFFSET(intf);
	intf->res.tx_epkt_core = priv->base + TX_EPKT_C_OFFSET(intf);
	intf->res.tx_pause_ctrl = priv->base + TX_PAUSE_CTRL_OFFSET(intf);

	intf->rx_edpkt_dma = priv->base + RX_EDPKT_DMA_OFFSET(intf);
	intf->rx_edpkt_cfg = priv->base + RX_EDPKT_CFG_OFFSET(intf);
}

#define MAX_IRQ_STR_LEN		64
struct bcmasp_intf *bcmasp_interface_create(struct bcmasp_priv *priv,
					    struct device_node *ndev_dn, int i)
{
	struct device *dev = &priv->pdev->dev;
	struct bcmasp_intf *intf;
	struct net_device *ndev;
	int ch, port, ret;

	if (of_property_read_u32(ndev_dn, "reg", &port)) {
		dev_warn(dev, "%s: invalid port number\n", ndev_dn->name);
		goto err;
	}

	if (of_property_read_u32(ndev_dn, "brcm,channel", &ch)) {
		dev_warn(dev, "%s: invalid ch number\n", ndev_dn->name);
		goto err;
	}

	ndev = alloc_etherdev(sizeof(struct bcmasp_intf));
	if (!ndev) {
		dev_warn(dev, "%s: unable to alloc ndev\n", ndev_dn->name);
		goto err;
	}
	intf = netdev_priv(ndev);

	intf->parent = priv;
	intf->ndev = ndev;
	intf->channel = ch;
	intf->port = port;
	intf->ndev_dn = ndev_dn;
	intf->index = i;

	ret = of_get_phy_mode(ndev_dn, &intf->phy_interface);
	if (ret < 0) {
		dev_err(dev, "invalid PHY mode property\n");
		goto err_free_netdev;
	}

	if (intf->phy_interface == PHY_INTERFACE_MODE_INTERNAL)
		intf->internal_phy = true;

	intf->phy_dn = of_parse_phandle(ndev_dn, "phy-handle", 0);
	if (!intf->phy_dn && of_phy_is_fixed_link(ndev_dn)) {
		ret = of_phy_register_fixed_link(ndev_dn);
		if (ret) {
			dev_warn(dev, "%s: failed to register fixed PHY\n",
				 ndev_dn->name);
			goto err_free_netdev;
		}
		intf->phy_dn = ndev_dn;
	}

	/* Map resource */
	bcmasp_map_res(priv, intf);

	if ((!phy_interface_mode_is_rgmii(intf->phy_interface) &&
	     intf->phy_interface != PHY_INTERFACE_MODE_MII &&
	     intf->phy_interface != PHY_INTERFACE_MODE_INTERNAL) ||
	    (intf->port != 1 && intf->internal_phy)) {
		netdev_err(intf->ndev, "invalid PHY mode: %s for port %d\n",
			   phy_modes(intf->phy_interface), intf->port);
		ret = -EINVAL;
		goto err_free_netdev;
	}

	ret = of_get_ethdev_address(ndev_dn, ndev);
	if (ret) {
		netdev_warn(ndev, "using random Ethernet MAC\n");
		eth_hw_addr_random(ndev);
	}

	SET_NETDEV_DEV(ndev, dev);
	intf->ops = &bcmasp_intf_ops;
	ndev->netdev_ops = &bcmasp_netdev_ops;
	ndev->ethtool_ops = &bcmasp_ethtool_ops;
	intf->msg_enable = netif_msg_init(-1, NETIF_MSG_DRV |
					  NETIF_MSG_PROBE |
					  NETIF_MSG_LINK);
	ndev->features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_SG |
			  NETIF_F_RXCSUM;
	ndev->hw_features |= ndev->features;
	ndev->needed_headroom += sizeof(struct bcmasp_pkt_offload);

	return intf;

err_free_netdev:
	free_netdev(ndev);
err:
	return NULL;
}

void bcmasp_interface_destroy(struct bcmasp_intf *intf)
{
	if (intf->ndev->reg_state == NETREG_REGISTERED)
		unregister_netdev(intf->ndev);
	if (of_phy_is_fixed_link(intf->ndev_dn))
		of_phy_deregister_fixed_link(intf->ndev_dn);
	free_netdev(intf->ndev);
}

static void bcmasp_suspend_to_wol(struct bcmasp_intf *intf)
{
	struct net_device *ndev = intf->ndev;
	u32 reg;

	reg = umac_rl(intf, UMC_MPD_CTRL);
	if (intf->wolopts & (WAKE_MAGIC | WAKE_MAGICSECURE))
		reg |= UMC_MPD_CTRL_MPD_EN;
	reg &= ~UMC_MPD_CTRL_PSW_EN;
	if (intf->wolopts & WAKE_MAGICSECURE) {
		/* Program the SecureOn password */
		umac_wl(intf, get_unaligned_be16(&intf->sopass[0]),
			UMC_PSW_MS);
		umac_wl(intf, get_unaligned_be32(&intf->sopass[2]),
			UMC_PSW_LS);
		reg |= UMC_MPD_CTRL_PSW_EN;
	}
	umac_wl(intf, reg, UMC_MPD_CTRL);

	if (intf->wolopts & WAKE_FILTER)
		bcmasp_netfilt_suspend(intf);

	/* Bring UniMAC out of reset if needed and enable RX */
	reg = umac_rl(intf, UMC_CMD);
	if (reg & UMC_CMD_SW_RESET)
		reg &= ~UMC_CMD_SW_RESET;

	reg |= UMC_CMD_RX_EN | UMC_CMD_PROMISC;
	umac_wl(intf, reg, UMC_CMD);

	umac_enable_set(intf, UMC_CMD_RX_EN, 1);

	if (intf->parent->wol_irq > 0) {
		wakeup_intr2_core_wl(intf->parent, 0xffffffff,
				     ASP_WAKEUP_INTR2_MASK_CLEAR);
	}

	if (intf->eee.eee_enabled && intf->parent->eee_fixup)
		intf->parent->eee_fixup(intf, true);

	netif_dbg(intf, wol, ndev, "entered WOL mode\n");
}

int bcmasp_interface_suspend(struct bcmasp_intf *intf)
{
	struct device *kdev = &intf->parent->pdev->dev;
	struct net_device *dev = intf->ndev;

	if (!netif_running(dev))
		return 0;

	netif_device_detach(dev);

	bcmasp_netif_deinit(dev);

	if (!intf->wolopts) {
		if (intf->internal_phy)
			bcmasp_ephy_enable_set(intf, false);
		else
			bcmasp_rgmii_mode_en_set(intf, false);

		/* If Wake-on-LAN is disabled, we can safely
		 * disable the network interface clocks.
		 */
		bcmasp_core_clock_set_intf(intf, false);
	}

	if (device_may_wakeup(kdev) && intf->wolopts)
		bcmasp_suspend_to_wol(intf);

	clk_disable_unprepare(intf->parent->clk);

	return 0;
}

static void bcmasp_resume_from_wol(struct bcmasp_intf *intf)
{
	u32 reg;

	if (intf->eee.eee_enabled && intf->parent->eee_fixup)
		intf->parent->eee_fixup(intf, false);

	reg = umac_rl(intf, UMC_MPD_CTRL);
	reg &= ~UMC_MPD_CTRL_MPD_EN;
	umac_wl(intf, reg, UMC_MPD_CTRL);

	if (intf->parent->wol_irq > 0) {
		wakeup_intr2_core_wl(intf->parent, 0xffffffff,
				     ASP_WAKEUP_INTR2_MASK_SET);
	}
}

int bcmasp_interface_resume(struct bcmasp_intf *intf)
{
	struct net_device *dev = intf->ndev;
	int ret;

	if (!netif_running(dev))
		return 0;

	ret = clk_prepare_enable(intf->parent->clk);
	if (ret)
		return ret;

	ret = bcmasp_netif_init(dev, false);
	if (ret)
		goto out;

	bcmasp_resume_from_wol(intf);

	if (intf->eee.eee_enabled)
		bcmasp_eee_enable_set(intf, true);

	netif_device_attach(dev);

	return 0;

out:
	clk_disable_unprepare(intf->parent->clk);
	return ret;
}
