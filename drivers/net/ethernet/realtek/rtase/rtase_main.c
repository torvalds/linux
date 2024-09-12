// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 *  rtase is the Linux device driver released for Realtek Automotive Switch
 *  controllers with PCI-Express interface.
 *
 *  Copyright(c) 2024 Realtek Semiconductor Corp.
 *
 *  Below is a simplified block diagram of the chip and its relevant interfaces.
 *
 *               *************************
 *               *                       *
 *               *  CPU network device   *
 *               *                       *
 *               *   +-------------+     *
 *               *   |  PCIE Host  |     *
 *               ***********++************
 *                          ||
 *                         PCIE
 *                          ||
 *      ********************++**********************
 *      *            | PCIE Endpoint |             *
 *      *            +---------------+             *
 *      *                | GMAC |                  *
 *      *                +--++--+  Realtek         *
 *      *                   ||     RTL90xx Series  *
 *      *                   ||                     *
 *      *     +-------------++----------------+    *
 *      *     |           | MAC |             |    *
 *      *     |           +-----+             |    *
 *      *     |                               |    *
 *      *     |     Ethernet Switch Core      |    *
 *      *     |                               |    *
 *      *     |   +-----+           +-----+   |    *
 *      *     |   | MAC |...........| MAC |   |    *
 *      *     +---+-----+-----------+-----+---+    *
 *      *         | PHY |...........| PHY |        *
 *      *         +--++-+           +--++-+        *
 *      *************||****************||***********
 *
 *  The block of the Realtek RTL90xx series is our entire chip architecture,
 *  the GMAC is connected to the switch core, and there is no PHY in between.
 *  In addition, this driver is mainly used to control GMAC, but does not
 *  control the switch core, so it is not the same as DSA. Linux only plays
 *  the role of a normal leaf node in this model.
 */

#include <linux/crc32.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/prefetch.h>
#include <linux/rtnetlink.h>
#include <linux/tcp.h>
#include <asm/irq.h>
#include <net/ip6_checksum.h>
#include <net/netdev_queues.h>
#include <net/page_pool/helpers.h>
#include <net/pkt_cls.h>

#include "rtase.h"

#define RTK_OPTS1_DEBUG_VALUE 0x0BADBEEF
#define RTK_MAGIC_NUMBER      0x0BADBADBADBADBAD

static const struct pci_device_id rtase_pci_tbl[] = {
	{PCI_VDEVICE(REALTEK, 0x906A)},
	{}
};

MODULE_DEVICE_TABLE(pci, rtase_pci_tbl);

MODULE_AUTHOR("Realtek ARD Software Team");
MODULE_DESCRIPTION("Network Driver for the PCIe interface of Realtek Automotive Ethernet Switch");
MODULE_LICENSE("Dual BSD/GPL");

struct rtase_counters {
	__le64 tx_packets;
	__le64 rx_packets;
	__le64 tx_errors;
	__le32 rx_errors;
	__le16 rx_missed;
	__le16 align_errors;
	__le32 tx_one_collision;
	__le32 tx_multi_collision;
	__le64 rx_unicast;
	__le64 rx_broadcast;
	__le32 rx_multicast;
	__le16 tx_aborted;
	__le16 tx_underrun;
} __packed;

static void rtase_w8(const struct rtase_private *tp, u16 reg, u8 val8)
{
	writeb(val8, tp->mmio_addr + reg);
}

static void rtase_w16(const struct rtase_private *tp, u16 reg, u16 val16)
{
	writew(val16, tp->mmio_addr + reg);
}

static void rtase_w32(const struct rtase_private *tp, u16 reg, u32 val32)
{
	writel(val32, tp->mmio_addr + reg);
}

static u8 rtase_r8(const struct rtase_private *tp, u16 reg)
{
	return readb(tp->mmio_addr + reg);
}

static u16 rtase_r16(const struct rtase_private *tp, u16 reg)
{
	return readw(tp->mmio_addr + reg);
}

static u32 rtase_r32(const struct rtase_private *tp, u16 reg)
{
	return readl(tp->mmio_addr + reg);
}

static void rtase_free_desc(struct rtase_private *tp)
{
	struct pci_dev *pdev = tp->pdev;
	u32 i;

	for (i = 0; i < tp->func_tx_queue_num; i++) {
		if (!tp->tx_ring[i].desc)
			continue;

		dma_free_coherent(&pdev->dev, RTASE_TX_RING_DESC_SIZE,
				  tp->tx_ring[i].desc,
				  tp->tx_ring[i].phy_addr);
		tp->tx_ring[i].desc = NULL;
	}

	for (i = 0; i < tp->func_rx_queue_num; i++) {
		if (!tp->rx_ring[i].desc)
			continue;

		dma_free_coherent(&pdev->dev, RTASE_RX_RING_DESC_SIZE,
				  tp->rx_ring[i].desc,
				  tp->rx_ring[i].phy_addr);
		tp->rx_ring[i].desc = NULL;
	}
}

static int rtase_alloc_desc(struct rtase_private *tp)
{
	struct pci_dev *pdev = tp->pdev;
	u32 i;

	/* rx and tx descriptors needs 256 bytes alignment.
	 * dma_alloc_coherent provides more.
	 */
	for (i = 0; i < tp->func_tx_queue_num; i++) {
		tp->tx_ring[i].desc =
				dma_alloc_coherent(&pdev->dev,
						   RTASE_TX_RING_DESC_SIZE,
						   &tp->tx_ring[i].phy_addr,
						   GFP_KERNEL);
		if (!tp->tx_ring[i].desc)
			goto err_out;
	}

	for (i = 0; i < tp->func_rx_queue_num; i++) {
		tp->rx_ring[i].desc =
				dma_alloc_coherent(&pdev->dev,
						   RTASE_RX_RING_DESC_SIZE,
						   &tp->rx_ring[i].phy_addr,
						   GFP_KERNEL);
		if (!tp->rx_ring[i].desc)
			goto err_out;
	}

	return 0;

err_out:
	rtase_free_desc(tp);
	return -ENOMEM;
}

static void rtase_unmap_tx_skb(struct pci_dev *pdev, u32 len,
			       struct rtase_tx_desc *desc)
{
	dma_unmap_single(&pdev->dev, le64_to_cpu(desc->addr), len,
			 DMA_TO_DEVICE);
	desc->opts1 = cpu_to_le32(RTK_OPTS1_DEBUG_VALUE);
	desc->opts2 = 0x00;
	desc->addr = cpu_to_le64(RTK_MAGIC_NUMBER);
}

static void rtase_tx_clear_range(struct rtase_ring *ring, u32 start, u32 n)
{
	struct rtase_tx_desc *desc_base = ring->desc;
	struct rtase_private *tp = ring->ivec->tp;
	u32 i;

	for (i = 0; i < n; i++) {
		u32 entry = (start + i) % RTASE_NUM_DESC;
		struct rtase_tx_desc *desc = desc_base + entry;
		u32 len = ring->mis.len[entry];
		struct sk_buff *skb;

		if (len == 0)
			continue;

		rtase_unmap_tx_skb(tp->pdev, len, desc);
		ring->mis.len[entry] = 0;
		skb = ring->skbuff[entry];
		if (!skb)
			continue;

		tp->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		ring->skbuff[entry] = NULL;
	}
}

static void rtase_tx_clear(struct rtase_private *tp)
{
	struct rtase_ring *ring;
	u16 i;

	for (i = 0; i < tp->func_tx_queue_num; i++) {
		ring = &tp->tx_ring[i];
		rtase_tx_clear_range(ring, ring->dirty_idx, RTASE_NUM_DESC);
		ring->cur_idx = 0;
		ring->dirty_idx = 0;
	}
}

static void rtase_mark_to_asic(union rtase_rx_desc *desc, u32 rx_buf_sz)
{
	u32 eor = le32_to_cpu(desc->desc_cmd.opts1) & RTASE_RING_END;

	desc->desc_status.opts2 = 0;
	/* force memory writes to complete before releasing descriptor */
	dma_wmb();
	WRITE_ONCE(desc->desc_cmd.opts1,
		   cpu_to_le32(RTASE_DESC_OWN | eor | rx_buf_sz));
}

static u32 rtase_tx_avail(struct rtase_ring *ring)
{
	return READ_ONCE(ring->dirty_idx) + RTASE_NUM_DESC -
	       READ_ONCE(ring->cur_idx);
}

static int tx_handler(struct rtase_ring *ring, int budget)
{
	const struct rtase_private *tp = ring->ivec->tp;
	struct net_device *dev = tp->dev;
	u32 dirty_tx, tx_left;
	u32 bytes_compl = 0;
	u32 pkts_compl = 0;
	int workdone = 0;

	dirty_tx = ring->dirty_idx;
	tx_left = READ_ONCE(ring->cur_idx) - dirty_tx;

	while (tx_left > 0) {
		u32 entry = dirty_tx % RTASE_NUM_DESC;
		struct rtase_tx_desc *desc = ring->desc +
				       sizeof(struct rtase_tx_desc) * entry;
		u32 status;

		status = le32_to_cpu(desc->opts1);

		if (status & RTASE_DESC_OWN)
			break;

		rtase_unmap_tx_skb(tp->pdev, ring->mis.len[entry], desc);
		ring->mis.len[entry] = 0;
		if (ring->skbuff[entry]) {
			pkts_compl++;
			bytes_compl += ring->skbuff[entry]->len;
			napi_consume_skb(ring->skbuff[entry], budget);
			ring->skbuff[entry] = NULL;
		}

		dirty_tx++;
		tx_left--;
		workdone++;

		if (workdone == RTASE_TX_BUDGET_DEFAULT)
			break;
	}

	if (ring->dirty_idx != dirty_tx) {
		dev_sw_netstats_tx_add(dev, pkts_compl, bytes_compl);
		WRITE_ONCE(ring->dirty_idx, dirty_tx);

		netif_subqueue_completed_wake(dev, ring->index, pkts_compl,
					      bytes_compl,
					      rtase_tx_avail(ring),
					      RTASE_TX_START_THRS);

		if (ring->cur_idx != dirty_tx)
			rtase_w8(tp, RTASE_TPPOLL, BIT(ring->index));
	}

	return 0;
}

static void rtase_tx_desc_init(struct rtase_private *tp, u16 idx)
{
	struct rtase_ring *ring = &tp->tx_ring[idx];
	struct rtase_tx_desc *desc;
	u32 i;

	memset(ring->desc, 0x0, RTASE_TX_RING_DESC_SIZE);
	memset(ring->skbuff, 0x0, sizeof(ring->skbuff));
	ring->cur_idx = 0;
	ring->dirty_idx = 0;
	ring->index = idx;
	ring->alloc_fail = 0;

	for (i = 0; i < RTASE_NUM_DESC; i++) {
		ring->mis.len[i] = 0;
		if ((RTASE_NUM_DESC - 1) == i) {
			desc = ring->desc + sizeof(struct rtase_tx_desc) * i;
			desc->opts1 = cpu_to_le32(RTASE_RING_END);
		}
	}

	ring->ring_handler = tx_handler;
	if (idx < 4) {
		ring->ivec = &tp->int_vector[idx];
		list_add_tail(&ring->ring_entry,
			      &tp->int_vector[idx].ring_list);
	} else {
		ring->ivec = &tp->int_vector[0];
		list_add_tail(&ring->ring_entry, &tp->int_vector[0].ring_list);
	}
}

static void rtase_map_to_asic(union rtase_rx_desc *desc, dma_addr_t mapping,
			      u32 rx_buf_sz)
{
	desc->desc_cmd.addr = cpu_to_le64(mapping);

	rtase_mark_to_asic(desc, rx_buf_sz);
}

static void rtase_make_unusable_by_asic(union rtase_rx_desc *desc)
{
	desc->desc_cmd.addr = cpu_to_le64(RTK_MAGIC_NUMBER);
	desc->desc_cmd.opts1 &= ~cpu_to_le32(RTASE_DESC_OWN | RSVD_MASK);
}

static int rtase_alloc_rx_data_buf(struct rtase_ring *ring,
				   void **p_data_buf,
				   union rtase_rx_desc *desc,
				   dma_addr_t *rx_phy_addr)
{
	struct rtase_int_vector *ivec = ring->ivec;
	const struct rtase_private *tp = ivec->tp;
	dma_addr_t mapping;
	struct page *page;

	page = page_pool_dev_alloc_pages(tp->page_pool);
	if (!page) {
		ring->alloc_fail++;
		goto err_out;
	}

	*p_data_buf = page_address(page);
	mapping = page_pool_get_dma_addr(page);
	*rx_phy_addr = mapping;
	rtase_map_to_asic(desc, mapping, tp->rx_buf_sz);

	return 0;

err_out:
	rtase_make_unusable_by_asic(desc);

	return -ENOMEM;
}

static u32 rtase_rx_ring_fill(struct rtase_ring *ring, u32 ring_start,
			      u32 ring_end)
{
	union rtase_rx_desc *desc_base = ring->desc;
	u32 cur;

	for (cur = ring_start; ring_end - cur > 0; cur++) {
		u32 i = cur % RTASE_NUM_DESC;
		union rtase_rx_desc *desc = desc_base + i;
		int ret;

		if (ring->data_buf[i])
			continue;

		ret = rtase_alloc_rx_data_buf(ring, &ring->data_buf[i], desc,
					      &ring->mis.data_phy_addr[i]);
		if (ret)
			break;
	}

	return cur - ring_start;
}

static void rtase_mark_as_last_descriptor(union rtase_rx_desc *desc)
{
	desc->desc_cmd.opts1 |= cpu_to_le32(RTASE_RING_END);
}

static void rtase_rx_ring_clear(struct page_pool *page_pool,
				struct rtase_ring *ring)
{
	union rtase_rx_desc *desc;
	struct page *page;
	u32 i;

	for (i = 0; i < RTASE_NUM_DESC; i++) {
		desc = ring->desc + sizeof(union rtase_rx_desc) * i;
		page = virt_to_head_page(ring->data_buf[i]);

		if (ring->data_buf[i])
			page_pool_put_full_page(page_pool, page, true);

		rtase_make_unusable_by_asic(desc);
	}
}

static int rtase_fragmented_frame(u32 status)
{
	return (status & (RTASE_RX_FIRST_FRAG | RTASE_RX_LAST_FRAG)) !=
	       (RTASE_RX_FIRST_FRAG | RTASE_RX_LAST_FRAG);
}

static void rtase_rx_csum(const struct rtase_private *tp, struct sk_buff *skb,
			  const union rtase_rx_desc *desc)
{
	u32 opts2 = le32_to_cpu(desc->desc_status.opts2);

	/* rx csum offload */
	if (((opts2 & RTASE_RX_V4F) && !(opts2 & RTASE_RX_IPF)) ||
	    (opts2 & RTASE_RX_V6F)) {
		if (((opts2 & RTASE_RX_TCPT) && !(opts2 & RTASE_RX_TCPF)) ||
		    ((opts2 & RTASE_RX_UDPT) && !(opts2 & RTASE_RX_UDPF)))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;
	} else {
		skb->ip_summed = CHECKSUM_NONE;
	}
}

static void rtase_rx_vlan_skb(union rtase_rx_desc *desc, struct sk_buff *skb)
{
	u32 opts2 = le32_to_cpu(desc->desc_status.opts2);

	if (!(opts2 & RTASE_RX_VLAN_TAG))
		return;

	__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
			       swab16(opts2 & RTASE_VLAN_TAG_MASK));
}

static void rtase_rx_skb(const struct rtase_ring *ring, struct sk_buff *skb)
{
	struct rtase_int_vector *ivec = ring->ivec;

	napi_gro_receive(&ivec->napi, skb);
}

static int rx_handler(struct rtase_ring *ring, int budget)
{
	union rtase_rx_desc *desc_base = ring->desc;
	u32 pkt_size, cur_rx, delta, entry, status;
	struct rtase_private *tp = ring->ivec->tp;
	struct net_device *dev = tp->dev;
	union rtase_rx_desc *desc;
	struct sk_buff *skb;
	int workdone = 0;

	cur_rx = ring->cur_idx;
	entry = cur_rx % RTASE_NUM_DESC;
	desc = &desc_base[entry];

	while (workdone < budget) {
		status = le32_to_cpu(desc->desc_status.opts1);

		if (status & RTASE_DESC_OWN)
			break;

		/* This barrier is needed to keep us from reading
		 * any other fields out of the rx descriptor until
		 * we know the status of RTASE_DESC_OWN
		 */
		dma_rmb();

		if (unlikely(status & RTASE_RX_RES)) {
			if (net_ratelimit())
				netdev_warn(dev, "Rx ERROR. status = %08x\n",
					    status);

			tp->stats.rx_errors++;

			if (status & (RTASE_RX_RWT | RTASE_RX_RUNT))
				tp->stats.rx_length_errors++;

			if (status & RTASE_RX_CRC)
				tp->stats.rx_crc_errors++;

			if (dev->features & NETIF_F_RXALL)
				goto process_pkt;

			rtase_mark_to_asic(desc, tp->rx_buf_sz);
			goto skip_process_pkt;
		}

process_pkt:
		pkt_size = status & RTASE_RX_PKT_SIZE_MASK;
		if (likely(!(dev->features & NETIF_F_RXFCS)))
			pkt_size -= ETH_FCS_LEN;

		/* The driver does not support incoming fragmented frames.
		 * They are seen as a symptom of over-mtu sized frames.
		 */
		if (unlikely(rtase_fragmented_frame(status))) {
			tp->stats.rx_dropped++;
			tp->stats.rx_length_errors++;
			rtase_mark_to_asic(desc, tp->rx_buf_sz);
			goto skip_process_pkt;
		}

		dma_sync_single_for_cpu(&tp->pdev->dev,
					ring->mis.data_phy_addr[entry],
					tp->rx_buf_sz, DMA_FROM_DEVICE);

		skb = build_skb(ring->data_buf[entry], PAGE_SIZE);
		if (!skb) {
			tp->stats.rx_dropped++;
			rtase_mark_to_asic(desc, tp->rx_buf_sz);
			goto skip_process_pkt;
		}
		ring->data_buf[entry] = NULL;

		if (dev->features & NETIF_F_RXCSUM)
			rtase_rx_csum(tp, skb, desc);

		skb_put(skb, pkt_size);
		skb_mark_for_recycle(skb);
		skb->protocol = eth_type_trans(skb, dev);

		if (skb->pkt_type == PACKET_MULTICAST)
			tp->stats.multicast++;

		rtase_rx_vlan_skb(desc, skb);
		rtase_rx_skb(ring, skb);

		dev_sw_netstats_rx_add(dev, pkt_size);

skip_process_pkt:
		workdone++;
		cur_rx++;
		entry = cur_rx % RTASE_NUM_DESC;
		desc = ring->desc + sizeof(union rtase_rx_desc) * entry;
	}

	ring->cur_idx = cur_rx;
	delta = rtase_rx_ring_fill(ring, ring->dirty_idx, ring->cur_idx);
	ring->dirty_idx += delta;

	return workdone;
}

static void rtase_rx_desc_init(struct rtase_private *tp, u16 idx)
{
	struct rtase_ring *ring = &tp->rx_ring[idx];
	u16 i;

	memset(ring->desc, 0x0, RTASE_RX_RING_DESC_SIZE);
	memset(ring->data_buf, 0x0, sizeof(ring->data_buf));
	ring->cur_idx = 0;
	ring->dirty_idx = 0;
	ring->index = idx;
	ring->alloc_fail = 0;

	for (i = 0; i < RTASE_NUM_DESC; i++)
		ring->mis.data_phy_addr[i] = 0;

	ring->ring_handler = rx_handler;
	ring->ivec = &tp->int_vector[idx];
	list_add_tail(&ring->ring_entry, &tp->int_vector[idx].ring_list);
}

static void rtase_rx_clear(struct rtase_private *tp)
{
	u32 i;

	for (i = 0; i < tp->func_rx_queue_num; i++)
		rtase_rx_ring_clear(tp->page_pool, &tp->rx_ring[i]);

	page_pool_destroy(tp->page_pool);
	tp->page_pool = NULL;
}

static int rtase_init_ring(const struct net_device *dev)
{
	struct rtase_private *tp = netdev_priv(dev);
	struct page_pool_params pp_params = { 0 };
	struct page_pool *page_pool;
	u32 num;
	u16 i;

	pp_params.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV;
	pp_params.order = 0;
	pp_params.pool_size = RTASE_NUM_DESC * tp->func_rx_queue_num;
	pp_params.nid = dev_to_node(&tp->pdev->dev);
	pp_params.dev = &tp->pdev->dev;
	pp_params.dma_dir = DMA_FROM_DEVICE;
	pp_params.max_len = PAGE_SIZE;
	pp_params.offset = 0;

	page_pool = page_pool_create(&pp_params);
	if (IS_ERR(page_pool)) {
		netdev_err(tp->dev, "failed to create page pool\n");
		return -ENOMEM;
	}

	tp->page_pool = page_pool;

	for (i = 0; i < tp->func_tx_queue_num; i++)
		rtase_tx_desc_init(tp, i);

	for (i = 0; i < tp->func_rx_queue_num; i++) {
		rtase_rx_desc_init(tp, i);

		num = rtase_rx_ring_fill(&tp->rx_ring[i], 0, RTASE_NUM_DESC);
		if (num != RTASE_NUM_DESC)
			goto err_out;

		rtase_mark_as_last_descriptor(tp->rx_ring[i].desc +
					      sizeof(union rtase_rx_desc) *
					      (RTASE_NUM_DESC - 1));
	}

	return 0;

err_out:
	rtase_rx_clear(tp);
	return -ENOMEM;
}

static void rtase_interrupt_mitigation(const struct rtase_private *tp)
{
	u32 i;

	for (i = 0; i < tp->func_tx_queue_num; i++)
		rtase_w16(tp, RTASE_INT_MITI_TX + i * 2, tp->tx_int_mit);

	for (i = 0; i < tp->func_rx_queue_num; i++)
		rtase_w16(tp, RTASE_INT_MITI_RX + i * 2, tp->rx_int_mit);
}

static void rtase_tally_counter_addr_fill(const struct rtase_private *tp)
{
	rtase_w32(tp, RTASE_DTCCR4, upper_32_bits(tp->tally_paddr));
	rtase_w32(tp, RTASE_DTCCR0, lower_32_bits(tp->tally_paddr));
}

static void rtase_tally_counter_clear(const struct rtase_private *tp)
{
	u32 cmd = lower_32_bits(tp->tally_paddr);

	rtase_w32(tp, RTASE_DTCCR4, upper_32_bits(tp->tally_paddr));
	rtase_w32(tp, RTASE_DTCCR0, cmd | RTASE_COUNTER_RESET);
}

static void rtase_desc_addr_fill(const struct rtase_private *tp)
{
	const struct rtase_ring *ring;
	u16 i, cmd, val;
	int err;

	for (i = 0; i < tp->func_tx_queue_num; i++) {
		ring = &tp->tx_ring[i];

		rtase_w32(tp, RTASE_TX_DESC_ADDR0,
			  lower_32_bits(ring->phy_addr));
		rtase_w32(tp, RTASE_TX_DESC_ADDR4,
			  upper_32_bits(ring->phy_addr));

		cmd = i | RTASE_TX_DESC_CMD_WE | RTASE_TX_DESC_CMD_CS;
		rtase_w16(tp, RTASE_TX_DESC_COMMAND, cmd);

		err = read_poll_timeout(rtase_r16, val,
					!(val & RTASE_TX_DESC_CMD_CS), 10,
					1000, false, tp,
					RTASE_TX_DESC_COMMAND);

		if (err == -ETIMEDOUT)
			netdev_err(tp->dev,
				   "error occurred in fill tx descriptor\n");
	}

	for (i = 0; i < tp->func_rx_queue_num; i++) {
		ring = &tp->rx_ring[i];

		if (i == 0) {
			rtase_w32(tp, RTASE_Q0_RX_DESC_ADDR0,
				  lower_32_bits(ring->phy_addr));
			rtase_w32(tp, RTASE_Q0_RX_DESC_ADDR4,
				  upper_32_bits(ring->phy_addr));
		} else {
			rtase_w32(tp, (RTASE_Q1_RX_DESC_ADDR0 + ((i - 1) * 8)),
				  lower_32_bits(ring->phy_addr));
			rtase_w32(tp, (RTASE_Q1_RX_DESC_ADDR4 + ((i - 1) * 8)),
				  upper_32_bits(ring->phy_addr));
		}
	}
}

static void rtase_hw_set_features(const struct net_device *dev,
				  netdev_features_t features)
{
	const struct rtase_private *tp = netdev_priv(dev);
	u16 rx_config, val;

	rx_config = rtase_r16(tp, RTASE_RX_CONFIG_0);
	if (features & NETIF_F_RXALL)
		rx_config |= (RTASE_ACCEPT_ERR | RTASE_ACCEPT_RUNT);
	else
		rx_config &= ~(RTASE_ACCEPT_ERR | RTASE_ACCEPT_RUNT);

	rtase_w16(tp, RTASE_RX_CONFIG_0, rx_config);

	val = rtase_r16(tp, RTASE_CPLUS_CMD);
	if (features & NETIF_F_RXCSUM)
		rtase_w16(tp, RTASE_CPLUS_CMD, val | RTASE_RX_CHKSUM);
	else
		rtase_w16(tp, RTASE_CPLUS_CMD, val & ~RTASE_RX_CHKSUM);

	rx_config = rtase_r16(tp, RTASE_RX_CONFIG_1);
	if (dev->features & NETIF_F_HW_VLAN_CTAG_RX)
		rx_config |= (RTASE_INNER_VLAN_DETAG_EN |
			      RTASE_OUTER_VLAN_DETAG_EN);
	else
		rx_config &= ~(RTASE_INNER_VLAN_DETAG_EN |
			       RTASE_OUTER_VLAN_DETAG_EN);

	rtase_w16(tp, RTASE_RX_CONFIG_1, rx_config);
}

static void rtase_hw_set_rx_packet_filter(struct net_device *dev)
{
	u32 mc_filter[2] = { 0xFFFFFFFF, 0xFFFFFFFF };
	struct rtase_private *tp = netdev_priv(dev);
	u16 rx_mode;

	rx_mode = rtase_r16(tp, RTASE_RX_CONFIG_0) & ~RTASE_ACCEPT_MASK;
	rx_mode |= RTASE_ACCEPT_BROADCAST | RTASE_ACCEPT_MYPHYS;

	if (dev->flags & IFF_PROMISC) {
		rx_mode |= RTASE_ACCEPT_MULTICAST | RTASE_ACCEPT_ALLPHYS;
	} else if (dev->flags & IFF_ALLMULTI) {
		rx_mode |= RTASE_ACCEPT_MULTICAST;
	} else {
		struct netdev_hw_addr *hw_addr;

		mc_filter[0] = 0;
		mc_filter[1] = 0;

		netdev_for_each_mc_addr(hw_addr, dev) {
			u32 bit_nr = eth_hw_addr_crc(hw_addr);
			u32 idx = u32_get_bits(bit_nr, BIT(31));
			u32 bit = u32_get_bits(bit_nr,
					       RTASE_MULTICAST_FILTER_MASK);

			mc_filter[idx] |= BIT(bit);
			rx_mode |= RTASE_ACCEPT_MULTICAST;
		}
	}

	if (dev->features & NETIF_F_RXALL)
		rx_mode |= RTASE_ACCEPT_ERR | RTASE_ACCEPT_RUNT;

	rtase_w32(tp, RTASE_MAR0, swab32(mc_filter[1]));
	rtase_w32(tp, RTASE_MAR1, swab32(mc_filter[0]));
	rtase_w16(tp, RTASE_RX_CONFIG_0, rx_mode);
}

static void rtase_irq_dis_and_clear(const struct rtase_private *tp)
{
	const struct rtase_int_vector *ivec = &tp->int_vector[0];
	u32 val1;
	u16 val2;
	u8 i;

	rtase_w32(tp, ivec->imr_addr, 0);
	val1 = rtase_r32(tp, ivec->isr_addr);
	rtase_w32(tp, ivec->isr_addr, val1);

	for (i = 1; i < tp->int_nums; i++) {
		ivec = &tp->int_vector[i];
		rtase_w16(tp, ivec->imr_addr, 0);
		val2 = rtase_r16(tp, ivec->isr_addr);
		rtase_w16(tp, ivec->isr_addr, val2);
	}
}

static void rtase_poll_timeout(const struct rtase_private *tp, u32 cond,
			       u32 sleep_us, u64 timeout_us, u16 reg)
{
	int err;
	u8 val;

	err = read_poll_timeout(rtase_r8, val, val & cond, sleep_us,
				timeout_us, false, tp, reg);

	if (err == -ETIMEDOUT)
		netdev_err(tp->dev, "poll reg 0x00%x timeout\n", reg);
}

static void rtase_nic_reset(const struct net_device *dev)
{
	const struct rtase_private *tp = netdev_priv(dev);
	u16 rx_config;
	u8 val;

	rx_config = rtase_r16(tp, RTASE_RX_CONFIG_0);
	rtase_w16(tp, RTASE_RX_CONFIG_0, rx_config & ~RTASE_ACCEPT_MASK);

	val = rtase_r8(tp, RTASE_MISC);
	rtase_w8(tp, RTASE_MISC, val | RTASE_RX_DV_GATE_EN);

	val = rtase_r8(tp, RTASE_CHIP_CMD);
	rtase_w8(tp, RTASE_CHIP_CMD, val | RTASE_STOP_REQ);
	mdelay(2);

	rtase_poll_timeout(tp, RTASE_STOP_REQ_DONE, 100, 150000,
			   RTASE_CHIP_CMD);

	rtase_poll_timeout(tp, RTASE_TX_FIFO_EMPTY, 100, 100000,
			   RTASE_FIFOR);

	rtase_poll_timeout(tp, RTASE_RX_FIFO_EMPTY, 100, 100000,
			   RTASE_FIFOR);

	val = rtase_r8(tp, RTASE_CHIP_CMD);
	rtase_w8(tp, RTASE_CHIP_CMD, val & ~(RTASE_TE | RTASE_RE));
	val = rtase_r8(tp, RTASE_CHIP_CMD);
	rtase_w8(tp, RTASE_CHIP_CMD, val & ~RTASE_STOP_REQ);

	rtase_w16(tp, RTASE_RX_CONFIG_0, rx_config);
}

static void rtase_hw_reset(const struct net_device *dev)
{
	const struct rtase_private *tp = netdev_priv(dev);

	rtase_irq_dis_and_clear(tp);

	rtase_nic_reset(dev);
}

static void rtase_set_rx_queue(const struct rtase_private *tp)
{
	u16 reg_data;

	reg_data = rtase_r16(tp, RTASE_FCR);
	switch (tp->func_rx_queue_num) {
	case 1:
		u16p_replace_bits(&reg_data, 0x1, RTASE_FCR_RXQ_MASK);
		break;
	case 2:
		u16p_replace_bits(&reg_data, 0x2, RTASE_FCR_RXQ_MASK);
		break;
	case 4:
		u16p_replace_bits(&reg_data, 0x3, RTASE_FCR_RXQ_MASK);
		break;
	}
	rtase_w16(tp, RTASE_FCR, reg_data);
}

static void rtase_set_tx_queue(const struct rtase_private *tp)
{
	u16 reg_data;

	reg_data = rtase_r16(tp, RTASE_TX_CONFIG_1);
	switch (tp->tx_queue_ctrl) {
	case 1:
		u16p_replace_bits(&reg_data, 0x0, RTASE_TC_MODE_MASK);
		break;
	case 2:
		u16p_replace_bits(&reg_data, 0x1, RTASE_TC_MODE_MASK);
		break;
	case 3:
	case 4:
		u16p_replace_bits(&reg_data, 0x2, RTASE_TC_MODE_MASK);
		break;
	default:
		u16p_replace_bits(&reg_data, 0x3, RTASE_TC_MODE_MASK);
		break;
	}
	rtase_w16(tp, RTASE_TX_CONFIG_1, reg_data);
}

static void rtase_hw_config(struct net_device *dev)
{
	const struct rtase_private *tp = netdev_priv(dev);
	u32 reg_data32;
	u16 reg_data16;

	rtase_hw_reset(dev);

	/* set rx dma burst */
	reg_data16 = rtase_r16(tp, RTASE_RX_CONFIG_0);
	reg_data16 &= ~(RTASE_RX_SINGLE_TAG | RTASE_RX_SINGLE_FETCH);
	u16p_replace_bits(&reg_data16, RTASE_RX_DMA_BURST_256,
			  RTASE_RX_MX_DMA_MASK);
	rtase_w16(tp, RTASE_RX_CONFIG_0, reg_data16);

	/* new rx descritpor */
	reg_data16 = rtase_r16(tp, RTASE_RX_CONFIG_1);
	reg_data16 |= RTASE_RX_NEW_DESC_FORMAT_EN | RTASE_PCIE_NEW_FLOW;
	u16p_replace_bits(&reg_data16, 0xF, RTASE_RX_MAX_FETCH_DESC_MASK);
	rtase_w16(tp, RTASE_RX_CONFIG_1, reg_data16);

	rtase_set_rx_queue(tp);

	rtase_interrupt_mitigation(tp);

	/* set tx dma burst size and interframe gap time */
	reg_data32 = rtase_r32(tp, RTASE_TX_CONFIG_0);
	u32p_replace_bits(&reg_data32, RTASE_TX_DMA_BURST_UNLIMITED,
			  RTASE_TX_DMA_MASK);
	u32p_replace_bits(&reg_data32, RTASE_INTERFRAMEGAP,
			  RTASE_TX_INTER_FRAME_GAP_MASK);
	rtase_w32(tp, RTASE_TX_CONFIG_0, reg_data32);

	/* new tx descriptor */
	reg_data16 = rtase_r16(tp, RTASE_TFUN_CTRL);
	rtase_w16(tp, RTASE_TFUN_CTRL, reg_data16 |
		  RTASE_TX_NEW_DESC_FORMAT_EN);

	/* tx fetch desc number */
	rtase_w8(tp, RTASE_TDFNR, 0x10);

	/* tag num select */
	reg_data16 = rtase_r16(tp, RTASE_MTPS);
	u16p_replace_bits(&reg_data16, 0x4, RTASE_TAG_NUM_SEL_MASK);
	rtase_w16(tp, RTASE_MTPS, reg_data16);

	rtase_set_tx_queue(tp);

	rtase_w16(tp, RTASE_TOKSEL, 0x5555);

	rtase_tally_counter_addr_fill(tp);
	rtase_desc_addr_fill(tp);
	rtase_hw_set_features(dev, dev->features);

	/* enable flow control */
	reg_data16 = rtase_r16(tp, RTASE_CPLUS_CMD);
	reg_data16 |= (RTASE_FORCE_TXFLOW_EN | RTASE_FORCE_RXFLOW_EN);
	rtase_w16(tp, RTASE_CPLUS_CMD, reg_data16);
	/* set near fifo threshold - rx missed issue. */
	rtase_w16(tp, RTASE_RFIFONFULL, 0x190);

	rtase_w16(tp, RTASE_RMS, tp->rx_buf_sz);

	rtase_hw_set_rx_packet_filter(dev);
}

static void rtase_nic_enable(const struct net_device *dev)
{
	const struct rtase_private *tp = netdev_priv(dev);
	u16 rcr = rtase_r16(tp, RTASE_RX_CONFIG_1);
	u8 val;

	rtase_w16(tp, RTASE_RX_CONFIG_1, rcr & ~RTASE_PCIE_RELOAD_EN);
	rtase_w16(tp, RTASE_RX_CONFIG_1, rcr | RTASE_PCIE_RELOAD_EN);

	val = rtase_r8(tp, RTASE_CHIP_CMD);
	rtase_w8(tp, RTASE_CHIP_CMD, val | RTASE_TE | RTASE_RE);

	val = rtase_r8(tp, RTASE_MISC);
	rtase_w8(tp, RTASE_MISC, val & ~RTASE_RX_DV_GATE_EN);
}

static void rtase_enable_hw_interrupt(const struct rtase_private *tp)
{
	const struct rtase_int_vector *ivec = &tp->int_vector[0];
	u32 i;

	rtase_w32(tp, ivec->imr_addr, ivec->imr);

	for (i = 1; i < tp->int_nums; i++) {
		ivec = &tp->int_vector[i];
		rtase_w16(tp, ivec->imr_addr, ivec->imr);
	}
}

static void rtase_hw_start(const struct net_device *dev)
{
	const struct rtase_private *tp = netdev_priv(dev);

	rtase_nic_enable(dev);
	rtase_enable_hw_interrupt(tp);
}

/*  the interrupt handler does RXQ0 and TXQ0, TXQ4~7 interrutp status
 */
static irqreturn_t rtase_interrupt(int irq, void *dev_instance)
{
	const struct rtase_private *tp;
	struct rtase_int_vector *ivec;
	u32 status;

	ivec = dev_instance;
	tp = ivec->tp;
	status = rtase_r32(tp, ivec->isr_addr);

	rtase_w32(tp, ivec->imr_addr, 0x0);
	rtase_w32(tp, ivec->isr_addr, status & ~RTASE_FOVW);

	if (napi_schedule_prep(&ivec->napi))
		__napi_schedule(&ivec->napi);

	return IRQ_HANDLED;
}

/*  the interrupt handler does RXQ1&TXQ1 or RXQ2&TXQ2 or RXQ3&TXQ3 interrupt
 *  status according to interrupt vector
 */
static irqreturn_t rtase_q_interrupt(int irq, void *dev_instance)
{
	const struct rtase_private *tp;
	struct rtase_int_vector *ivec;
	u16 status;

	ivec = dev_instance;
	tp = ivec->tp;
	status = rtase_r16(tp, ivec->isr_addr);

	rtase_w16(tp, ivec->imr_addr, 0x0);
	rtase_w16(tp, ivec->isr_addr, status);

	if (napi_schedule_prep(&ivec->napi))
		__napi_schedule(&ivec->napi);

	return IRQ_HANDLED;
}

static int rtase_poll(struct napi_struct *napi, int budget)
{
	const struct rtase_int_vector *ivec;
	const struct rtase_private *tp;
	struct rtase_ring *ring;
	int total_workdone = 0;

	ivec = container_of(napi, struct rtase_int_vector, napi);
	tp = ivec->tp;

	list_for_each_entry(ring, &ivec->ring_list, ring_entry)
		total_workdone += ring->ring_handler(ring, budget);

	if (total_workdone >= budget)
		return budget;

	if (napi_complete_done(napi, total_workdone)) {
		if (!ivec->index)
			rtase_w32(tp, ivec->imr_addr, ivec->imr);
		else
			rtase_w16(tp, ivec->imr_addr, ivec->imr);
	}

	return total_workdone;
}

static int rtase_open(struct net_device *dev)
{
	struct rtase_private *tp = netdev_priv(dev);
	const struct pci_dev *pdev = tp->pdev;
	struct rtase_int_vector *ivec;
	u16 i = 0, j;
	int ret;

	ivec = &tp->int_vector[0];
	tp->rx_buf_sz = RTASE_RX_BUF_SIZE;

	ret = rtase_alloc_desc(tp);
	if (ret)
		return ret;

	ret = rtase_init_ring(dev);
	if (ret)
		goto err_free_all_allocated_mem;

	rtase_hw_config(dev);

	if (tp->sw_flag & RTASE_SWF_MSIX_ENABLED) {
		ret = request_irq(ivec->irq, rtase_interrupt, 0,
				  dev->name, ivec);
		if (ret)
			goto err_free_all_allocated_irq;

		/* request other interrupts to handle multiqueue */
		for (i = 1; i < tp->int_nums; i++) {
			ivec = &tp->int_vector[i];
			snprintf(ivec->name, sizeof(ivec->name), "%s_int%i",
				 tp->dev->name, i);
			ret = request_irq(ivec->irq, rtase_q_interrupt, 0,
					  ivec->name, ivec);
			if (ret)
				goto err_free_all_allocated_irq;
		}
	} else {
		ret = request_irq(pdev->irq, rtase_interrupt, 0, dev->name,
				  ivec);
		if (ret)
			goto err_free_all_allocated_mem;
	}

	rtase_hw_start(dev);

	for (i = 0; i < tp->int_nums; i++) {
		ivec = &tp->int_vector[i];
		napi_enable(&ivec->napi);
	}

	netif_carrier_on(dev);
	netif_wake_queue(dev);

	return 0;

err_free_all_allocated_irq:
	for (j = 0; j < i; j++)
		free_irq(tp->int_vector[j].irq, &tp->int_vector[j]);

err_free_all_allocated_mem:
	rtase_free_desc(tp);

	return ret;
}

static void rtase_down(struct net_device *dev)
{
	struct rtase_private *tp = netdev_priv(dev);
	struct rtase_int_vector *ivec;
	struct rtase_ring *ring, *tmp;
	u32 i;

	for (i = 0; i < tp->int_nums; i++) {
		ivec = &tp->int_vector[i];
		napi_disable(&ivec->napi);
		list_for_each_entry_safe(ring, tmp, &ivec->ring_list,
					 ring_entry)
			list_del(&ring->ring_entry);
	}

	netif_tx_disable(dev);

	netif_carrier_off(dev);

	rtase_hw_reset(dev);

	rtase_tx_clear(tp);

	rtase_rx_clear(tp);
}

static int rtase_close(struct net_device *dev)
{
	struct rtase_private *tp = netdev_priv(dev);
	const struct pci_dev *pdev = tp->pdev;
	u32 i;

	rtase_down(dev);

	if (tp->sw_flag & RTASE_SWF_MSIX_ENABLED) {
		for (i = 0; i < tp->int_nums; i++)
			free_irq(tp->int_vector[i].irq, &tp->int_vector[i]);

	} else {
		free_irq(pdev->irq, &tp->int_vector[0]);
	}

	rtase_free_desc(tp);

	return 0;
}

static u32 rtase_tx_vlan_tag(const struct rtase_private *tp,
			     const struct sk_buff *skb)
{
	return (skb_vlan_tag_present(skb)) ?
		(RTASE_TX_VLAN_TAG | swab16(skb_vlan_tag_get(skb))) : 0x00;
}

static u32 rtase_tx_csum(struct sk_buff *skb, const struct net_device *dev)
{
	u32 csum_cmd = 0;
	u8 ip_protocol;

	switch (vlan_get_protocol(skb)) {
	case htons(ETH_P_IP):
		csum_cmd = RTASE_TX_IPCS_C;
		ip_protocol = ip_hdr(skb)->protocol;
		break;

	case htons(ETH_P_IPV6):
		csum_cmd = RTASE_TX_IPV6F_C;
		ip_protocol = ipv6_hdr(skb)->nexthdr;
		break;

	default:
		ip_protocol = IPPROTO_RAW;
		break;
	}

	if (ip_protocol == IPPROTO_TCP)
		csum_cmd |= RTASE_TX_TCPCS_C;
	else if (ip_protocol == IPPROTO_UDP)
		csum_cmd |= RTASE_TX_UDPCS_C;

	csum_cmd |= u32_encode_bits(skb_transport_offset(skb),
				    RTASE_TCPHO_MASK);

	return csum_cmd;
}

static int rtase_xmit_frags(struct rtase_ring *ring, struct sk_buff *skb,
			    u32 opts1, u32 opts2)
{
	const struct skb_shared_info *info = skb_shinfo(skb);
	const struct rtase_private *tp = ring->ivec->tp;
	const u8 nr_frags = info->nr_frags;
	struct rtase_tx_desc *txd = NULL;
	u32 cur_frag, entry;

	entry = ring->cur_idx;
	for (cur_frag = 0; cur_frag < nr_frags; cur_frag++) {
		const skb_frag_t *frag = &info->frags[cur_frag];
		dma_addr_t mapping;
		u32 status, len;
		void *addr;

		entry = (entry + 1) % RTASE_NUM_DESC;

		txd = ring->desc + sizeof(struct rtase_tx_desc) * entry;
		len = skb_frag_size(frag);
		addr = skb_frag_address(frag);
		mapping = dma_map_single(&tp->pdev->dev, addr, len,
					 DMA_TO_DEVICE);

		if (unlikely(dma_mapping_error(&tp->pdev->dev, mapping))) {
			if (unlikely(net_ratelimit()))
				netdev_err(tp->dev,
					   "Failed to map TX fragments DMA!\n");

			goto err_out;
		}

		if (((entry + 1) % RTASE_NUM_DESC) == 0)
			status = (opts1 | len | RTASE_RING_END);
		else
			status = opts1 | len;

		if (cur_frag == (nr_frags - 1)) {
			ring->skbuff[entry] = skb;
			status |= RTASE_TX_LAST_FRAG;
		}

		ring->mis.len[entry] = len;
		txd->addr = cpu_to_le64(mapping);
		txd->opts2 = cpu_to_le32(opts2);

		/* make sure the operating fields have been updated */
		dma_wmb();
		txd->opts1 = cpu_to_le32(status);
	}

	return cur_frag;

err_out:
	rtase_tx_clear_range(ring, ring->cur_idx + 1, cur_frag);
	return -EIO;
}

static netdev_tx_t rtase_start_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	struct rtase_private *tp = netdev_priv(dev);
	u32 q_idx, entry, len, opts1, opts2;
	struct netdev_queue *tx_queue;
	bool stop_queue, door_bell;
	u32 mss = shinfo->gso_size;
	struct rtase_tx_desc *txd;
	struct rtase_ring *ring;
	dma_addr_t mapping;
	int frags;

	/* multiqueues */
	q_idx = skb_get_queue_mapping(skb);
	ring = &tp->tx_ring[q_idx];
	tx_queue = netdev_get_tx_queue(dev, q_idx);

	if (unlikely(!rtase_tx_avail(ring))) {
		if (net_ratelimit())
			netdev_err(dev,
				   "BUG! Tx Ring full when queue awake!\n");

		netif_stop_queue(dev);
		return NETDEV_TX_BUSY;
	}

	entry = ring->cur_idx % RTASE_NUM_DESC;
	txd = ring->desc + sizeof(struct rtase_tx_desc) * entry;

	opts1 = RTASE_DESC_OWN;
	opts2 = rtase_tx_vlan_tag(tp, skb);

	/* tcp segmentation offload (or tcp large send) */
	if (mss) {
		if (shinfo->gso_type & SKB_GSO_TCPV4) {
			opts1 |= RTASE_GIANT_SEND_V4;
		} else if (shinfo->gso_type & SKB_GSO_TCPV6) {
			if (skb_cow_head(skb, 0))
				goto err_dma_0;

			tcp_v6_gso_csum_prep(skb);
			opts1 |= RTASE_GIANT_SEND_V6;
		} else {
			WARN_ON_ONCE(1);
		}

		opts1 |= u32_encode_bits(skb_transport_offset(skb),
					 RTASE_TCPHO_MASK);
		opts2 |= u32_encode_bits(mss, RTASE_MSS_MASK);
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		opts2 |= rtase_tx_csum(skb, dev);
	}

	frags = rtase_xmit_frags(ring, skb, opts1, opts2);
	if (unlikely(frags < 0))
		goto err_dma_0;

	if (frags) {
		len = skb_headlen(skb);
		opts1 |= RTASE_TX_FIRST_FRAG;
	} else {
		len = skb->len;
		ring->skbuff[entry] = skb;
		opts1 |= RTASE_TX_FIRST_FRAG | RTASE_TX_LAST_FRAG;
	}

	if (((entry + 1) % RTASE_NUM_DESC) == 0)
		opts1 |= (len | RTASE_RING_END);
	else
		opts1 |= len;

	mapping = dma_map_single(&tp->pdev->dev, skb->data, len,
				 DMA_TO_DEVICE);

	if (unlikely(dma_mapping_error(&tp->pdev->dev, mapping))) {
		if (unlikely(net_ratelimit()))
			netdev_err(dev, "Failed to map TX DMA!\n");

		goto err_dma_1;
	}

	ring->mis.len[entry] = len;
	txd->addr = cpu_to_le64(mapping);
	txd->opts2 = cpu_to_le32(opts2);
	txd->opts1 = cpu_to_le32(opts1 & ~RTASE_DESC_OWN);

	/* make sure the operating fields have been updated */
	dma_wmb();

	door_bell = __netdev_tx_sent_queue(tx_queue, skb->len,
					   netdev_xmit_more());

	txd->opts1 = cpu_to_le32(opts1);

	skb_tx_timestamp(skb);

	/* tx needs to see descriptor changes before updated cur_idx */
	smp_wmb();

	WRITE_ONCE(ring->cur_idx, ring->cur_idx + frags + 1);

	stop_queue = !netif_subqueue_maybe_stop(dev, ring->index,
						rtase_tx_avail(ring),
						RTASE_TX_STOP_THRS,
						RTASE_TX_START_THRS);

	if (door_bell || stop_queue)
		rtase_w8(tp, RTASE_TPPOLL, BIT(ring->index));

	return NETDEV_TX_OK;

err_dma_1:
	ring->skbuff[entry] = NULL;
	rtase_tx_clear_range(ring, ring->cur_idx + 1, frags);

err_dma_0:
	tp->stats.tx_dropped++;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static void rtase_set_rx_mode(struct net_device *dev)
{
	rtase_hw_set_rx_packet_filter(dev);
}

static void rtase_enable_eem_write(const struct rtase_private *tp)
{
	u8 val;

	val = rtase_r8(tp, RTASE_EEM);
	rtase_w8(tp, RTASE_EEM, val | RTASE_EEM_UNLOCK);
}

static void rtase_disable_eem_write(const struct rtase_private *tp)
{
	u8 val;

	val = rtase_r8(tp, RTASE_EEM);
	rtase_w8(tp, RTASE_EEM, val & ~RTASE_EEM_UNLOCK);
}

static void rtase_rar_set(const struct rtase_private *tp, const u8 *addr)
{
	u32 rar_low, rar_high;

	rar_low = (u32)addr[0] | ((u32)addr[1] << 8) |
		  ((u32)addr[2] << 16) | ((u32)addr[3] << 24);

	rar_high = (u32)addr[4] | ((u32)addr[5] << 8);

	rtase_enable_eem_write(tp);
	rtase_w32(tp, RTASE_MAC0, rar_low);
	rtase_w32(tp, RTASE_MAC4, rar_high);
	rtase_disable_eem_write(tp);
	rtase_w16(tp, RTASE_LBK_CTRL, RTASE_LBK_ATLD | RTASE_LBK_CLR);
}

static int rtase_set_mac_address(struct net_device *dev, void *p)
{
	struct rtase_private *tp = netdev_priv(dev);
	int ret;

	ret = eth_mac_addr(dev, p);
	if (ret)
		return ret;

	rtase_rar_set(tp, dev->dev_addr);

	return 0;
}

static int rtase_change_mtu(struct net_device *dev, int new_mtu)
{
	dev->mtu = new_mtu;

	netdev_update_features(dev);

	return 0;
}

static void rtase_wait_for_quiescence(const struct net_device *dev)
{
	struct rtase_private *tp = netdev_priv(dev);
	struct rtase_int_vector *ivec;
	u32 i;

	for (i = 0; i < tp->int_nums; i++) {
		ivec = &tp->int_vector[i];
		synchronize_irq(ivec->irq);
		/* wait for any pending NAPI task to complete */
		napi_disable(&ivec->napi);
	}

	rtase_irq_dis_and_clear(tp);

	for (i = 0; i < tp->int_nums; i++) {
		ivec = &tp->int_vector[i];
		napi_enable(&ivec->napi);
	}
}

static void rtase_sw_reset(struct net_device *dev)
{
	struct rtase_private *tp = netdev_priv(dev);
	int ret;

	netif_stop_queue(dev);
	netif_carrier_off(dev);
	rtase_hw_reset(dev);

	/* let's wait a bit while any (async) irq lands on */
	rtase_wait_for_quiescence(dev);
	rtase_tx_clear(tp);
	rtase_rx_clear(tp);

	ret = rtase_init_ring(dev);
	if (ret) {
		netdev_err(dev, "unable to init ring\n");
		rtase_free_desc(tp);
		return;
	}

	rtase_hw_config(dev);
	/* always link, so start to transmit & receive */
	rtase_hw_start(dev);

	netif_carrier_on(dev);
	netif_wake_queue(dev);
}

static void rtase_dump_tally_counter(const struct rtase_private *tp)
{
	dma_addr_t paddr = tp->tally_paddr;
	u32 cmd = lower_32_bits(paddr);
	u32 val;
	int err;

	rtase_w32(tp, RTASE_DTCCR4, upper_32_bits(paddr));
	rtase_w32(tp, RTASE_DTCCR0, cmd);
	rtase_w32(tp, RTASE_DTCCR0, cmd | RTASE_COUNTER_DUMP);

	err = read_poll_timeout(rtase_r32, val, !(val & RTASE_COUNTER_DUMP),
				10, 250, false, tp, RTASE_DTCCR0);

	if (err == -ETIMEDOUT)
		netdev_err(tp->dev, "error occurred in dump tally counter\n");
}

static void rtase_dump_state(const struct net_device *dev)
{
	const struct rtase_private *tp = netdev_priv(dev);
	int max_reg_size = RTASE_PCI_REGS_SIZE;
	const struct rtase_counters *counters;
	const struct rtase_ring *ring;
	u32 dword_rd;
	int n = 0;

	ring = &tp->tx_ring[0];
	netdev_err(dev, "Tx descriptor info:\n");
	netdev_err(dev, "Tx curIdx = 0x%x\n", ring->cur_idx);
	netdev_err(dev, "Tx dirtyIdx = 0x%x\n", ring->dirty_idx);
	netdev_err(dev, "Tx phyAddr = %pad\n", &ring->phy_addr);

	ring = &tp->rx_ring[0];
	netdev_err(dev, "Rx descriptor info:\n");
	netdev_err(dev, "Rx curIdx = 0x%x\n", ring->cur_idx);
	netdev_err(dev, "Rx dirtyIdx = 0x%x\n", ring->dirty_idx);
	netdev_err(dev, "Rx phyAddr = %pad\n", &ring->phy_addr);

	netdev_err(dev, "Device Registers:\n");
	netdev_err(dev, "Chip Command = 0x%02x\n",
		   rtase_r8(tp, RTASE_CHIP_CMD));
	netdev_err(dev, "IMR = %08x\n", rtase_r32(tp, RTASE_IMR0));
	netdev_err(dev, "ISR = %08x\n", rtase_r32(tp, RTASE_ISR0));
	netdev_err(dev, "Boot Ctrl Reg(0xE004) = %04x\n",
		   rtase_r16(tp, RTASE_BOOT_CTL));
	netdev_err(dev, "EPHY ISR(0xE014) = %04x\n",
		   rtase_r16(tp, RTASE_EPHY_ISR));
	netdev_err(dev, "EPHY IMR(0xE016) = %04x\n",
		   rtase_r16(tp, RTASE_EPHY_IMR));
	netdev_err(dev, "CLKSW SET REG(0xE018) = %04x\n",
		   rtase_r16(tp, RTASE_CLKSW_SET));

	netdev_err(dev, "Dump PCI Registers:\n");

	while (n < max_reg_size) {
		if ((n % RTASE_DWORD_MOD) == 0)
			netdev_err(tp->dev, "0x%03x:\n", n);

		pci_read_config_dword(tp->pdev, n, &dword_rd);
		netdev_err(tp->dev, "%08x\n", dword_rd);
		n += 4;
	}

	netdev_err(dev, "Dump tally counter:\n");
	counters = tp->tally_vaddr;
	rtase_dump_tally_counter(tp);

	netdev_err(dev, "tx_packets %lld\n",
		   le64_to_cpu(counters->tx_packets));
	netdev_err(dev, "rx_packets %lld\n",
		   le64_to_cpu(counters->rx_packets));
	netdev_err(dev, "tx_errors %lld\n",
		   le64_to_cpu(counters->tx_errors));
	netdev_err(dev, "rx_errors %d\n",
		   le32_to_cpu(counters->rx_errors));
	netdev_err(dev, "rx_missed %d\n",
		   le16_to_cpu(counters->rx_missed));
	netdev_err(dev, "align_errors %d\n",
		   le16_to_cpu(counters->align_errors));
	netdev_err(dev, "tx_one_collision %d\n",
		   le32_to_cpu(counters->tx_one_collision));
	netdev_err(dev, "tx_multi_collision %d\n",
		   le32_to_cpu(counters->tx_multi_collision));
	netdev_err(dev, "rx_unicast %lld\n",
		   le64_to_cpu(counters->rx_unicast));
	netdev_err(dev, "rx_broadcast %lld\n",
		   le64_to_cpu(counters->rx_broadcast));
	netdev_err(dev, "rx_multicast %d\n",
		   le32_to_cpu(counters->rx_multicast));
	netdev_err(dev, "tx_aborted %d\n",
		   le16_to_cpu(counters->tx_aborted));
	netdev_err(dev, "tx_underrun %d\n",
		   le16_to_cpu(counters->tx_underrun));
}

static void rtase_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	rtase_dump_state(dev);
	rtase_sw_reset(dev);
}

static void rtase_get_stats64(struct net_device *dev,
			      struct rtnl_link_stats64 *stats)
{
	const struct rtase_private *tp = netdev_priv(dev);
	const struct rtase_counters *counters;

	counters = tp->tally_vaddr;

	dev_fetch_sw_netstats(stats, dev->tstats);

	/* fetch additional counter values missing in stats collected by driver
	 * from tally counter
	 */
	rtase_dump_tally_counter(tp);
	stats->rx_errors = tp->stats.rx_errors;
	stats->tx_errors = le64_to_cpu(counters->tx_errors);
	stats->rx_dropped = tp->stats.rx_dropped;
	stats->tx_dropped = tp->stats.tx_dropped;
	stats->multicast = tp->stats.multicast;
	stats->rx_length_errors = tp->stats.rx_length_errors;
}

static netdev_features_t rtase_fix_features(struct net_device *dev,
					    netdev_features_t features)
{
	netdev_features_t features_fix = features;

	/* not support TSO for jumbo frames */
	if (dev->mtu > ETH_DATA_LEN)
		features_fix &= ~NETIF_F_ALL_TSO;

	return features_fix;
}

static int rtase_set_features(struct net_device *dev,
			      netdev_features_t features)
{
	netdev_features_t features_set = features;

	features_set &= NETIF_F_RXALL | NETIF_F_RXCSUM |
			NETIF_F_HW_VLAN_CTAG_RX;

	if (features_set ^ dev->features)
		rtase_hw_set_features(dev, features_set);

	return 0;
}

static const struct net_device_ops rtase_netdev_ops = {
	.ndo_open = rtase_open,
	.ndo_stop = rtase_close,
	.ndo_start_xmit = rtase_start_xmit,
	.ndo_set_rx_mode = rtase_set_rx_mode,
	.ndo_set_mac_address = rtase_set_mac_address,
	.ndo_change_mtu = rtase_change_mtu,
	.ndo_tx_timeout = rtase_tx_timeout,
	.ndo_get_stats64 = rtase_get_stats64,
	.ndo_fix_features = rtase_fix_features,
	.ndo_set_features = rtase_set_features,
};

static void rtase_get_mac_address(struct net_device *dev)
{
	struct rtase_private *tp = netdev_priv(dev);
	u8 mac_addr[ETH_ALEN] __aligned(2) = {};
	u32 i;

	for (i = 0; i < ETH_ALEN; i++)
		mac_addr[i] = rtase_r8(tp, RTASE_MAC0 + i);

	if (!is_valid_ether_addr(mac_addr)) {
		eth_hw_addr_random(dev);
		netdev_warn(dev, "Random ether addr %pM\n", dev->dev_addr);
	} else {
		eth_hw_addr_set(dev, mac_addr);
		ether_addr_copy(dev->perm_addr, dev->dev_addr);
	}

	rtase_rar_set(tp, dev->dev_addr);
}

static int rtase_get_settings(struct net_device *dev,
			      struct ethtool_link_ksettings *cmd)
{
	u32 supported = SUPPORTED_MII | SUPPORTED_Pause | SUPPORTED_Asym_Pause;

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	cmd->base.speed = SPEED_5000;
	cmd->base.duplex = DUPLEX_FULL;
	cmd->base.port = PORT_MII;
	cmd->base.autoneg = AUTONEG_DISABLE;

	return 0;
}

static void rtase_get_pauseparam(struct net_device *dev,
				 struct ethtool_pauseparam *pause)
{
	const struct rtase_private *tp = netdev_priv(dev);
	u16 value = rtase_r16(tp, RTASE_CPLUS_CMD);

	pause->autoneg = AUTONEG_DISABLE;
	pause->tx_pause = !!(value & RTASE_FORCE_TXFLOW_EN);
	pause->rx_pause = !!(value & RTASE_FORCE_RXFLOW_EN);
}

static int rtase_set_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *pause)
{
	const struct rtase_private *tp = netdev_priv(dev);
	u16 value = rtase_r16(tp, RTASE_CPLUS_CMD);

	if (pause->autoneg)
		return -EOPNOTSUPP;

	value &= ~(RTASE_FORCE_TXFLOW_EN | RTASE_FORCE_RXFLOW_EN);

	if (pause->tx_pause)
		value |= RTASE_FORCE_TXFLOW_EN;

	if (pause->rx_pause)
		value |= RTASE_FORCE_RXFLOW_EN;

	rtase_w16(tp, RTASE_CPLUS_CMD, value);
	return 0;
}

static void rtase_get_eth_mac_stats(struct net_device *dev,
				    struct ethtool_eth_mac_stats *stats)
{
	struct rtase_private *tp = netdev_priv(dev);
	const struct rtase_counters *counters;

	counters = tp->tally_vaddr;

	rtase_dump_tally_counter(tp);

	stats->FramesTransmittedOK = le64_to_cpu(counters->tx_packets);
	stats->FramesReceivedOK = le64_to_cpu(counters->rx_packets);
	stats->FramesLostDueToIntMACXmitError =
		le64_to_cpu(counters->tx_errors);
	stats->BroadcastFramesReceivedOK = le64_to_cpu(counters->rx_broadcast);
}

static const struct ethtool_ops rtase_ethtool_ops = {
	.get_link = ethtool_op_get_link,
	.get_link_ksettings = rtase_get_settings,
	.get_pauseparam = rtase_get_pauseparam,
	.set_pauseparam = rtase_set_pauseparam,
	.get_eth_mac_stats = rtase_get_eth_mac_stats,
	.get_ts_info = ethtool_op_get_ts_info,
};

static void rtase_init_netdev_ops(struct net_device *dev)
{
	dev->netdev_ops = &rtase_netdev_ops;
	dev->ethtool_ops = &rtase_ethtool_ops;
}

static void rtase_reset_interrupt(struct pci_dev *pdev,
				  const struct rtase_private *tp)
{
	if (tp->sw_flag & RTASE_SWF_MSIX_ENABLED)
		pci_disable_msix(pdev);
	else
		pci_disable_msi(pdev);
}

static int rtase_alloc_msix(struct pci_dev *pdev, struct rtase_private *tp)
{
	int ret, irq;
	u16 i;

	memset(tp->msix_entry, 0x0, RTASE_NUM_MSIX *
	       sizeof(struct msix_entry));

	for (i = 0; i < RTASE_NUM_MSIX; i++)
		tp->msix_entry[i].entry = i;

	ret = pci_enable_msix_exact(pdev, tp->msix_entry, tp->int_nums);

	if (ret)
		return ret;

	for (i = 0; i < tp->int_nums; i++) {
		irq = pci_irq_vector(pdev, i);
		if (!irq) {
			pci_disable_msix(pdev);
			return irq;
		}

		tp->int_vector[i].irq = irq;
	}

	return 0;
}

static int rtase_alloc_interrupt(struct pci_dev *pdev,
				 struct rtase_private *tp)
{
	int ret;

	ret = rtase_alloc_msix(pdev, tp);
	if (ret) {
		ret = pci_enable_msi(pdev);
		if (ret) {
			dev_err(&pdev->dev,
				"unable to alloc interrupt.(MSI)\n");
			return ret;
		}

		tp->sw_flag |= RTASE_SWF_MSI_ENABLED;
	} else {
		tp->sw_flag |= RTASE_SWF_MSIX_ENABLED;
	}

	return 0;
}

static void rtase_init_hardware(const struct rtase_private *tp)
{
	u16 i;

	for (i = 0; i < RTASE_VLAN_FILTER_ENTRY_NUM; i++)
		rtase_w32(tp, RTASE_VLAN_ENTRY_0 + i * 4, 0);
}

static void rtase_init_int_vector(struct rtase_private *tp)
{
	u16 i;

	/* interrupt vector 0 */
	tp->int_vector[0].tp = tp;
	tp->int_vector[0].index = 0;
	tp->int_vector[0].imr_addr = RTASE_IMR0;
	tp->int_vector[0].isr_addr = RTASE_ISR0;
	tp->int_vector[0].imr = RTASE_ROK | RTASE_RDU | RTASE_TOK |
				RTASE_TOK4 | RTASE_TOK5 | RTASE_TOK6 |
				RTASE_TOK7;
	tp->int_vector[0].poll = rtase_poll;

	memset(tp->int_vector[0].name, 0x0, sizeof(tp->int_vector[0].name));
	INIT_LIST_HEAD(&tp->int_vector[0].ring_list);

	netif_napi_add(tp->dev, &tp->int_vector[0].napi,
		       tp->int_vector[0].poll);

	/* interrupt vector 1 ~ 3 */
	for (i = 1; i < tp->int_nums; i++) {
		tp->int_vector[i].tp = tp;
		tp->int_vector[i].index = i;
		tp->int_vector[i].imr_addr = RTASE_IMR1 + (i - 1) * 4;
		tp->int_vector[i].isr_addr = RTASE_ISR1 + (i - 1) * 4;
		tp->int_vector[i].imr = RTASE_Q_ROK | RTASE_Q_RDU |
					RTASE_Q_TOK;
		tp->int_vector[i].poll = rtase_poll;

		memset(tp->int_vector[i].name, 0x0,
		       sizeof(tp->int_vector[0].name));
		INIT_LIST_HEAD(&tp->int_vector[i].ring_list);

		netif_napi_add(tp->dev, &tp->int_vector[i].napi,
			       tp->int_vector[i].poll);
	}
}

static u16 rtase_calc_time_mitigation(u32 time_us)
{
	u8 msb, time_count, time_unit;
	u16 int_miti;

	time_us = min_t(int, time_us, RTASE_MITI_MAX_TIME);

	msb = fls(time_us);
	if (msb >= RTASE_MITI_COUNT_BIT_NUM) {
		time_unit = msb - RTASE_MITI_COUNT_BIT_NUM;
		time_count = time_us >> (msb - RTASE_MITI_COUNT_BIT_NUM);
	} else {
		time_unit = 0;
		time_count = time_us;
	}

	int_miti = u16_encode_bits(time_count, RTASE_MITI_TIME_COUNT_MASK) |
		   u16_encode_bits(time_unit, RTASE_MITI_TIME_UNIT_MASK);

	return int_miti;
}

static u16 rtase_calc_packet_num_mitigation(u16 pkt_num)
{
	u8 msb, pkt_num_count, pkt_num_unit;
	u16 int_miti;

	pkt_num = min_t(int, pkt_num, RTASE_MITI_MAX_PKT_NUM);

	if (pkt_num > 60) {
		pkt_num_unit = RTASE_MITI_MAX_PKT_NUM_IDX;
		pkt_num_count = pkt_num / RTASE_MITI_MAX_PKT_NUM_UNIT;
	} else {
		msb = fls(pkt_num);
		if (msb >= RTASE_MITI_COUNT_BIT_NUM) {
			pkt_num_unit = msb - RTASE_MITI_COUNT_BIT_NUM;
			pkt_num_count = pkt_num >> (msb -
						    RTASE_MITI_COUNT_BIT_NUM);
		} else {
			pkt_num_unit = 0;
			pkt_num_count = pkt_num;
		}
	}

	int_miti = u16_encode_bits(pkt_num_count,
				   RTASE_MITI_PKT_NUM_COUNT_MASK) |
		   u16_encode_bits(pkt_num_unit,
				   RTASE_MITI_PKT_NUM_UNIT_MASK);

	return int_miti;
}

static void rtase_init_software_variable(struct pci_dev *pdev,
					 struct rtase_private *tp)
{
	u16 int_miti;

	tp->tx_queue_ctrl = RTASE_TXQ_CTRL;
	tp->func_tx_queue_num = RTASE_FUNC_TXQ_NUM;
	tp->func_rx_queue_num = RTASE_FUNC_RXQ_NUM;
	tp->int_nums = RTASE_INTERRUPT_NUM;

	int_miti = rtase_calc_time_mitigation(RTASE_MITI_DEFAULT_TIME) |
		   rtase_calc_packet_num_mitigation(RTASE_MITI_DEFAULT_PKT_NUM);
	tp->tx_int_mit = int_miti;
	tp->rx_int_mit = int_miti;

	tp->sw_flag = 0;

	rtase_init_int_vector(tp);

	/* MTU range: 60 - hw-specific max */
	tp->dev->min_mtu = ETH_ZLEN;
	tp->dev->max_mtu = RTASE_MAX_JUMBO_SIZE;
}

static bool rtase_check_mac_version_valid(struct rtase_private *tp)
{
	u32 hw_ver = rtase_r32(tp, RTASE_TX_CONFIG_0) & RTASE_HW_VER_MASK;
	bool known_ver = false;

	switch (hw_ver) {
	case 0x00800000:
	case 0x04000000:
	case 0x04800000:
		known_ver = true;
		break;
	}

	return known_ver;
}

static int rtase_init_board(struct pci_dev *pdev, struct net_device **dev_out,
			    void __iomem **ioaddr_out)
{
	struct net_device *dev;
	void __iomem *ioaddr;
	int ret = -ENOMEM;

	/* dev zeroed in alloc_etherdev */
	dev = alloc_etherdev_mq(sizeof(struct rtase_private),
				RTASE_FUNC_TXQ_NUM);
	if (!dev)
		goto err_out;

	SET_NETDEV_DEV(dev, &pdev->dev);

	ret = pci_enable_device(pdev);
	if (ret < 0)
		goto err_out_free_dev;

	/* make sure PCI base addr 1 is MMIO */
	if (!(pci_resource_flags(pdev, 2) & IORESOURCE_MEM)) {
		ret = -ENODEV;
		goto err_out_disable;
	}

	/* check for weird/broken PCI region reporting */
	if (pci_resource_len(pdev, 2) < RTASE_REGS_SIZE) {
		ret = -ENODEV;
		goto err_out_disable;
	}

	ret = pci_request_regions(pdev, KBUILD_MODNAME);
	if (ret < 0)
		goto err_out_disable;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "no usable dma addressing method\n");
		goto err_out_free_res;
	}

	pci_set_master(pdev);

	/* ioremap MMIO region */
	ioaddr = ioremap(pci_resource_start(pdev, 2),
			 pci_resource_len(pdev, 2));
	if (!ioaddr) {
		ret = -EIO;
		goto err_out_free_res;
	}

	*ioaddr_out = ioaddr;
	*dev_out = dev;

	return ret;

err_out_free_res:
	pci_release_regions(pdev);

err_out_disable:
	pci_disable_device(pdev);

err_out_free_dev:
	free_netdev(dev);

err_out:
	*ioaddr_out = NULL;
	*dev_out = NULL;

	return ret;
}

static void rtase_release_board(struct pci_dev *pdev, struct net_device *dev,
				void __iomem *ioaddr)
{
	const struct rtase_private *tp = netdev_priv(dev);

	rtase_rar_set(tp, tp->dev->perm_addr);
	iounmap(ioaddr);

	if (tp->sw_flag & RTASE_SWF_MSIX_ENABLED)
		pci_disable_msix(pdev);
	else
		pci_disable_msi(pdev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	free_netdev(dev);
}

static int rtase_init_one(struct pci_dev *pdev,
			  const struct pci_device_id *ent)
{
	struct net_device *dev = NULL;
	struct rtase_int_vector *ivec;
	void __iomem *ioaddr = NULL;
	struct rtase_private *tp;
	int ret, i;

	if (!pdev->is_physfn && pdev->is_virtfn) {
		dev_err(&pdev->dev,
			"This module does not support a virtual function.");
		return -EINVAL;
	}

	dev_dbg(&pdev->dev, "Automotive Switch Ethernet driver loaded\n");

	ret = rtase_init_board(pdev, &dev, &ioaddr);
	if (ret != 0)
		return ret;

	tp = netdev_priv(dev);
	tp->mmio_addr = ioaddr;
	tp->dev = dev;
	tp->pdev = pdev;

	/* identify chip attached to board */
	if (!rtase_check_mac_version_valid(tp))
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "unknown chip version, contact rtase maintainers (see MAINTAINERS file)\n");

	rtase_init_software_variable(pdev, tp);
	rtase_init_hardware(tp);

	ret = rtase_alloc_interrupt(pdev, tp);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to alloc MSIX/MSI\n");
		goto err_out_1;
	}

	rtase_init_netdev_ops(dev);

	dev->pcpu_stat_type = NETDEV_PCPU_STAT_TSTATS;

	dev->features |= NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
			 NETIF_F_IP_CSUM | NETIF_F_HIGHDMA |
			 NETIF_F_RXCSUM | NETIF_F_SG |
			 NETIF_F_TSO | NETIF_F_IPV6_CSUM |
			 NETIF_F_TSO6;

	dev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM |
			   NETIF_F_TSO | NETIF_F_RXCSUM |
			   NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
			   NETIF_F_RXALL | NETIF_F_RXFCS |
			   NETIF_F_IPV6_CSUM | NETIF_F_TSO6;

	dev->vlan_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO |
			     NETIF_F_HIGHDMA;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	netif_set_tso_max_size(dev, RTASE_LSO_64K);
	netif_set_tso_max_segs(dev, RTASE_NIC_MAX_PHYS_BUF_COUNT_LSO2);

	rtase_get_mac_address(dev);

	tp->tally_vaddr = dma_alloc_coherent(&pdev->dev,
					     sizeof(*tp->tally_vaddr),
					     &tp->tally_paddr,
					     GFP_KERNEL);
	if (!tp->tally_vaddr) {
		ret = -ENOMEM;
		goto err_out;
	}

	rtase_tally_counter_clear(tp);

	pci_set_drvdata(pdev, dev);

	netif_carrier_off(dev);

	ret = register_netdev(dev);
	if (ret != 0)
		goto err_out;

	netdev_dbg(dev, "%pM, IRQ %d\n", dev->dev_addr, dev->irq);

	return 0;

err_out:
	if (tp->tally_vaddr) {
		dma_free_coherent(&pdev->dev,
				  sizeof(*tp->tally_vaddr),
				  tp->tally_vaddr,
				  tp->tally_paddr);

		tp->tally_vaddr = NULL;
	}

err_out_1:
	for (i = 0; i < tp->int_nums; i++) {
		ivec = &tp->int_vector[i];
		netif_napi_del(&ivec->napi);
	}

	rtase_release_board(pdev, dev, ioaddr);

	return ret;
}

static void rtase_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct rtase_private *tp = netdev_priv(dev);
	struct rtase_int_vector *ivec;
	u32 i;

	unregister_netdev(dev);

	for (i = 0; i < tp->int_nums; i++) {
		ivec = &tp->int_vector[i];
		netif_napi_del(&ivec->napi);
	}

	rtase_reset_interrupt(pdev, tp);
	if (tp->tally_vaddr) {
		dma_free_coherent(&pdev->dev,
				  sizeof(*tp->tally_vaddr),
				  tp->tally_vaddr,
				  tp->tally_paddr);
		tp->tally_vaddr = NULL;
	}

	rtase_release_board(pdev, dev, tp->mmio_addr);
	pci_set_drvdata(pdev, NULL);
}

static void rtase_shutdown(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	const struct rtase_private *tp;

	tp = netdev_priv(dev);

	if (netif_running(dev))
		rtase_close(dev);

	rtase_reset_interrupt(pdev, tp);
}

static int rtase_suspend(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);

	if (netif_running(dev)) {
		netif_device_detach(dev);
		rtase_hw_reset(dev);
	}

	return 0;
}

static int rtase_resume(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);
	struct rtase_private *tp = netdev_priv(dev);
	int ret;

	/* restore last modified mac address */
	rtase_rar_set(tp, dev->dev_addr);

	if (!netif_running(dev))
		goto out;

	rtase_wait_for_quiescence(dev);

	rtase_tx_clear(tp);
	rtase_rx_clear(tp);

	ret = rtase_init_ring(dev);
	if (ret) {
		netdev_err(dev, "unable to init ring\n");
		rtase_free_desc(tp);
		return -ENOMEM;
	}

	rtase_hw_config(dev);
	/* always link, so start to transmit & receive */
	rtase_hw_start(dev);

	netif_device_attach(dev);
out:

	return 0;
}

static const struct dev_pm_ops rtase_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(rtase_suspend, rtase_resume)
};

static struct pci_driver rtase_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtase_pci_tbl,
	.probe = rtase_init_one,
	.remove = rtase_remove_one,
	.shutdown = rtase_shutdown,
	.driver.pm = pm_ptr(&rtase_pm_ops),
};

module_pci_driver(rtase_pci_driver);
