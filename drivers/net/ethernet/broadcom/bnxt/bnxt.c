/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>

#include <linux/stringify.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <asm/page.h>
#include <linux/time.h>
#include <linux/mii.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#if defined(CONFIG_VXLAN) || defined(CONFIG_VXLAN_MODULE)
#include <net/vxlan.h>
#endif
#ifdef CONFIG_NET_RX_BUSY_POLL
#include <net/busy_poll.h>
#endif
#include <linux/workqueue.h>
#include <linux/prefetch.h>
#include <linux/cache.h>
#include <linux/log2.h>
#include <linux/aer.h>
#include <linux/bitmap.h>
#include <linux/cpu_rmap.h>

#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_sriov.h"
#include "bnxt_ethtool.h"

#define BNXT_TX_TIMEOUT		(5 * HZ)

static const char version[] =
	"Broadcom NetXtreme-C/E driver " DRV_MODULE_NAME " v" DRV_MODULE_VERSION "\n";

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Broadcom BCM573xx network driver");
MODULE_VERSION(DRV_MODULE_VERSION);

#define BNXT_RX_OFFSET (NET_SKB_PAD + NET_IP_ALIGN)
#define BNXT_RX_DMA_OFFSET NET_SKB_PAD
#define BNXT_RX_COPY_THRESH 256

#define BNXT_TX_PUSH_THRESH 92

enum board_idx {
	BCM57302,
	BCM57304,
	BCM57404,
	BCM57406,
	BCM57304_VF,
	BCM57404_VF,
};

/* indexed by enum above */
static const struct {
	char *name;
} board_info[] = {
	{ "Broadcom BCM57302 NetXtreme-C Single-port 10Gb/25Gb/40Gb/50Gb Ethernet" },
	{ "Broadcom BCM57304 NetXtreme-C Dual-port 10Gb/25Gb/40Gb/50Gb Ethernet" },
	{ "Broadcom BCM57404 NetXtreme-E Dual-port 10Gb/25Gb Ethernet" },
	{ "Broadcom BCM57406 NetXtreme-E Dual-port 10Gb Ethernet" },
	{ "Broadcom BCM57304 NetXtreme-C Ethernet Virtual Function" },
	{ "Broadcom BCM57404 NetXtreme-E Ethernet Virtual Function" },
};

static const struct pci_device_id bnxt_pci_tbl[] = {
	{ PCI_VDEVICE(BROADCOM, 0x16c9), .driver_data = BCM57302 },
	{ PCI_VDEVICE(BROADCOM, 0x16ca), .driver_data = BCM57304 },
	{ PCI_VDEVICE(BROADCOM, 0x16d1), .driver_data = BCM57404 },
	{ PCI_VDEVICE(BROADCOM, 0x16d2), .driver_data = BCM57406 },
#ifdef CONFIG_BNXT_SRIOV
	{ PCI_VDEVICE(BROADCOM, 0x16cb), .driver_data = BCM57304_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16d3), .driver_data = BCM57404_VF },
#endif
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, bnxt_pci_tbl);

static const u16 bnxt_vf_req_snif[] = {
	HWRM_FUNC_CFG,
	HWRM_PORT_PHY_QCFG,
	HWRM_CFA_L2_FILTER_ALLOC,
};

static bool bnxt_vf_pciid(enum board_idx idx)
{
	return (idx == BCM57304_VF || idx == BCM57404_VF);
}

#define DB_CP_REARM_FLAGS	(DB_KEY_CP | DB_IDX_VALID)
#define DB_CP_FLAGS		(DB_KEY_CP | DB_IDX_VALID | DB_IRQ_DIS)
#define DB_CP_IRQ_DIS_FLAGS	(DB_KEY_CP | DB_IRQ_DIS)

#define BNXT_CP_DB_REARM(db, raw_cons)					\
		writel(DB_CP_REARM_FLAGS | RING_CMP(raw_cons), db)

#define BNXT_CP_DB(db, raw_cons)					\
		writel(DB_CP_FLAGS | RING_CMP(raw_cons), db)

#define BNXT_CP_DB_IRQ_DIS(db)						\
		writel(DB_CP_IRQ_DIS_FLAGS, db)

static inline u32 bnxt_tx_avail(struct bnxt *bp, struct bnxt_tx_ring_info *txr)
{
	/* Tell compiler to fetch tx indices from memory. */
	barrier();

	return bp->tx_ring_size -
		((txr->tx_prod - txr->tx_cons) & bp->tx_ring_mask);
}

static const u16 bnxt_lhint_arr[] = {
	TX_BD_FLAGS_LHINT_512_AND_SMALLER,
	TX_BD_FLAGS_LHINT_512_TO_1023,
	TX_BD_FLAGS_LHINT_1024_TO_2047,
	TX_BD_FLAGS_LHINT_1024_TO_2047,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
};

static netdev_tx_t bnxt_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);
	struct tx_bd *txbd;
	struct tx_bd_ext *txbd1;
	struct netdev_queue *txq;
	int i;
	dma_addr_t mapping;
	unsigned int length, pad = 0;
	u32 len, free_size, vlan_tag_flags, cfa_action, flags;
	u16 prod, last_frag;
	struct pci_dev *pdev = bp->pdev;
	struct bnxt_napi *bnapi;
	struct bnxt_tx_ring_info *txr;
	struct bnxt_sw_tx_bd *tx_buf;

	i = skb_get_queue_mapping(skb);
	if (unlikely(i >= bp->tx_nr_rings)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	bnapi = bp->bnapi[i];
	txr = &bnapi->tx_ring;
	txq = netdev_get_tx_queue(dev, i);
	prod = txr->tx_prod;

	free_size = bnxt_tx_avail(bp, txr);
	if (unlikely(free_size < skb_shinfo(skb)->nr_frags + 2)) {
		netif_tx_stop_queue(txq);
		return NETDEV_TX_BUSY;
	}

	length = skb->len;
	len = skb_headlen(skb);
	last_frag = skb_shinfo(skb)->nr_frags;

	txbd = &txr->tx_desc_ring[TX_RING(prod)][TX_IDX(prod)];

	txbd->tx_bd_opaque = prod;

	tx_buf = &txr->tx_buf_ring[prod];
	tx_buf->skb = skb;
	tx_buf->nr_frags = last_frag;

	vlan_tag_flags = 0;
	cfa_action = 0;
	if (skb_vlan_tag_present(skb)) {
		vlan_tag_flags = TX_BD_CFA_META_KEY_VLAN |
				 skb_vlan_tag_get(skb);
		/* Currently supports 8021Q, 8021AD vlan offloads
		 * QINQ1, QINQ2, QINQ3 vlan headers are deprecated
		 */
		if (skb->vlan_proto == htons(ETH_P_8021Q))
			vlan_tag_flags |= 1 << TX_BD_CFA_META_TPID_SHIFT;
	}

	if (free_size == bp->tx_ring_size && length <= bp->tx_push_thresh) {
		struct tx_push_bd *push = txr->tx_push;
		struct tx_bd *tx_push = &push->txbd1;
		struct tx_bd_ext *tx_push1 = &push->txbd2;
		void *pdata = tx_push1 + 1;
		int j;

		/* Set COAL_NOW to be ready quickly for the next push */
		tx_push->tx_bd_len_flags_type =
			cpu_to_le32((length << TX_BD_LEN_SHIFT) |
					TX_BD_TYPE_LONG_TX_BD |
					TX_BD_FLAGS_LHINT_512_AND_SMALLER |
					TX_BD_FLAGS_COAL_NOW |
					TX_BD_FLAGS_PACKET_END |
					(2 << TX_BD_FLAGS_BD_CNT_SHIFT));

		if (skb->ip_summed == CHECKSUM_PARTIAL)
			tx_push1->tx_bd_hsize_lflags =
					cpu_to_le32(TX_BD_FLAGS_TCP_UDP_CHKSUM);
		else
			tx_push1->tx_bd_hsize_lflags = 0;

		tx_push1->tx_bd_cfa_meta = cpu_to_le32(vlan_tag_flags);
		tx_push1->tx_bd_cfa_action = cpu_to_le32(cfa_action);

		skb_copy_from_linear_data(skb, pdata, len);
		pdata += len;
		for (j = 0; j < last_frag; j++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[j];
			void *fptr;

			fptr = skb_frag_address_safe(frag);
			if (!fptr)
				goto normal_tx;

			memcpy(pdata, fptr, skb_frag_size(frag));
			pdata += skb_frag_size(frag);
		}

		memcpy(txbd, tx_push, sizeof(*txbd));
		prod = NEXT_TX(prod);
		txbd = &txr->tx_desc_ring[TX_RING(prod)][TX_IDX(prod)];
		memcpy(txbd, tx_push1, sizeof(*txbd));
		prod = NEXT_TX(prod);
		push->doorbell =
			cpu_to_le32(DB_KEY_TX_PUSH | DB_LONG_TX_PUSH | prod);
		txr->tx_prod = prod;

		netdev_tx_sent_queue(txq, skb->len);

		__iowrite64_copy(txr->tx_doorbell, push,
				 (length + sizeof(*push) + 8) / 8);

		tx_buf->is_push = 1;

		goto tx_done;
	}

normal_tx:
	if (length < BNXT_MIN_PKT_SIZE) {
		pad = BNXT_MIN_PKT_SIZE - length;
		if (skb_pad(skb, pad)) {
			/* SKB already freed. */
			tx_buf->skb = NULL;
			return NETDEV_TX_OK;
		}
		length = BNXT_MIN_PKT_SIZE;
	}

	mapping = dma_map_single(&pdev->dev, skb->data, len, DMA_TO_DEVICE);

	if (unlikely(dma_mapping_error(&pdev->dev, mapping))) {
		dev_kfree_skb_any(skb);
		tx_buf->skb = NULL;
		return NETDEV_TX_OK;
	}

	dma_unmap_addr_set(tx_buf, mapping, mapping);
	flags = (len << TX_BD_LEN_SHIFT) | TX_BD_TYPE_LONG_TX_BD |
		((last_frag + 2) << TX_BD_FLAGS_BD_CNT_SHIFT);

	txbd->tx_bd_haddr = cpu_to_le64(mapping);

	prod = NEXT_TX(prod);
	txbd1 = (struct tx_bd_ext *)
		&txr->tx_desc_ring[TX_RING(prod)][TX_IDX(prod)];

	txbd1->tx_bd_hsize_lflags = 0;
	if (skb_is_gso(skb)) {
		u32 hdr_len;

		if (skb->encapsulation)
			hdr_len = skb_inner_network_offset(skb) +
				skb_inner_network_header_len(skb) +
				inner_tcp_hdrlen(skb);
		else
			hdr_len = skb_transport_offset(skb) +
				tcp_hdrlen(skb);

		txbd1->tx_bd_hsize_lflags = cpu_to_le32(TX_BD_FLAGS_LSO |
					TX_BD_FLAGS_T_IPID |
					(hdr_len << (TX_BD_HSIZE_SHIFT - 1)));
		length = skb_shinfo(skb)->gso_size;
		txbd1->tx_bd_mss = cpu_to_le32(length);
		length += hdr_len;
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		txbd1->tx_bd_hsize_lflags =
			cpu_to_le32(TX_BD_FLAGS_TCP_UDP_CHKSUM);
		txbd1->tx_bd_mss = 0;
	}

	length >>= 9;
	flags |= bnxt_lhint_arr[length];
	txbd->tx_bd_len_flags_type = cpu_to_le32(flags);

	txbd1->tx_bd_cfa_meta = cpu_to_le32(vlan_tag_flags);
	txbd1->tx_bd_cfa_action = cpu_to_le32(cfa_action);
	for (i = 0; i < last_frag; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		prod = NEXT_TX(prod);
		txbd = &txr->tx_desc_ring[TX_RING(prod)][TX_IDX(prod)];

		len = skb_frag_size(frag);
		mapping = skb_frag_dma_map(&pdev->dev, frag, 0, len,
					   DMA_TO_DEVICE);

		if (unlikely(dma_mapping_error(&pdev->dev, mapping)))
			goto tx_dma_error;

		tx_buf = &txr->tx_buf_ring[prod];
		dma_unmap_addr_set(tx_buf, mapping, mapping);

		txbd->tx_bd_haddr = cpu_to_le64(mapping);

		flags = len << TX_BD_LEN_SHIFT;
		txbd->tx_bd_len_flags_type = cpu_to_le32(flags);
	}

	flags &= ~TX_BD_LEN;
	txbd->tx_bd_len_flags_type =
		cpu_to_le32(((len + pad) << TX_BD_LEN_SHIFT) | flags |
			    TX_BD_FLAGS_PACKET_END);

	netdev_tx_sent_queue(txq, skb->len);

	/* Sync BD data before updating doorbell */
	wmb();

	prod = NEXT_TX(prod);
	txr->tx_prod = prod;

	writel(DB_KEY_TX | prod, txr->tx_doorbell);
	writel(DB_KEY_TX | prod, txr->tx_doorbell);

tx_done:

	mmiowb();

	if (unlikely(bnxt_tx_avail(bp, txr) <= MAX_SKB_FRAGS + 1)) {
		netif_tx_stop_queue(txq);

		/* netif_tx_stop_queue() must be done before checking
		 * tx index in bnxt_tx_avail() below, because in
		 * bnxt_tx_int(), we update tx index before checking for
		 * netif_tx_queue_stopped().
		 */
		smp_mb();
		if (bnxt_tx_avail(bp, txr) > bp->tx_wake_thresh)
			netif_tx_wake_queue(txq);
	}
	return NETDEV_TX_OK;

tx_dma_error:
	last_frag = i;

	/* start back at beginning and unmap skb */
	prod = txr->tx_prod;
	tx_buf = &txr->tx_buf_ring[prod];
	tx_buf->skb = NULL;
	dma_unmap_single(&pdev->dev, dma_unmap_addr(tx_buf, mapping),
			 skb_headlen(skb), PCI_DMA_TODEVICE);
	prod = NEXT_TX(prod);

	/* unmap remaining mapped pages */
	for (i = 0; i < last_frag; i++) {
		prod = NEXT_TX(prod);
		tx_buf = &txr->tx_buf_ring[prod];
		dma_unmap_page(&pdev->dev, dma_unmap_addr(tx_buf, mapping),
			       skb_frag_size(&skb_shinfo(skb)->frags[i]),
			       PCI_DMA_TODEVICE);
	}

	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static void bnxt_tx_int(struct bnxt *bp, struct bnxt_napi *bnapi, int nr_pkts)
{
	struct bnxt_tx_ring_info *txr = &bnapi->tx_ring;
	int index = bnapi->index;
	struct netdev_queue *txq = netdev_get_tx_queue(bp->dev, index);
	u16 cons = txr->tx_cons;
	struct pci_dev *pdev = bp->pdev;
	int i;
	unsigned int tx_bytes = 0;

	for (i = 0; i < nr_pkts; i++) {
		struct bnxt_sw_tx_bd *tx_buf;
		struct sk_buff *skb;
		int j, last;

		tx_buf = &txr->tx_buf_ring[cons];
		cons = NEXT_TX(cons);
		skb = tx_buf->skb;
		tx_buf->skb = NULL;

		if (tx_buf->is_push) {
			tx_buf->is_push = 0;
			goto next_tx_int;
		}

		dma_unmap_single(&pdev->dev, dma_unmap_addr(tx_buf, mapping),
				 skb_headlen(skb), PCI_DMA_TODEVICE);
		last = tx_buf->nr_frags;

		for (j = 0; j < last; j++) {
			cons = NEXT_TX(cons);
			tx_buf = &txr->tx_buf_ring[cons];
			dma_unmap_page(
				&pdev->dev,
				dma_unmap_addr(tx_buf, mapping),
				skb_frag_size(&skb_shinfo(skb)->frags[j]),
				PCI_DMA_TODEVICE);
		}

next_tx_int:
		cons = NEXT_TX(cons);

		tx_bytes += skb->len;
		dev_kfree_skb_any(skb);
	}

	netdev_tx_completed_queue(txq, nr_pkts, tx_bytes);
	txr->tx_cons = cons;

	/* Need to make the tx_cons update visible to bnxt_start_xmit()
	 * before checking for netif_tx_queue_stopped().  Without the
	 * memory barrier, there is a small possibility that bnxt_start_xmit()
	 * will miss it and cause the queue to be stopped forever.
	 */
	smp_mb();

	if (unlikely(netif_tx_queue_stopped(txq)) &&
	    (bnxt_tx_avail(bp, txr) > bp->tx_wake_thresh)) {
		__netif_tx_lock(txq, smp_processor_id());
		if (netif_tx_queue_stopped(txq) &&
		    bnxt_tx_avail(bp, txr) > bp->tx_wake_thresh &&
		    txr->dev_state != BNXT_DEV_STATE_CLOSING)
			netif_tx_wake_queue(txq);
		__netif_tx_unlock(txq);
	}
}

static inline u8 *__bnxt_alloc_rx_data(struct bnxt *bp, dma_addr_t *mapping,
				       gfp_t gfp)
{
	u8 *data;
	struct pci_dev *pdev = bp->pdev;

	data = kmalloc(bp->rx_buf_size, gfp);
	if (!data)
		return NULL;

	*mapping = dma_map_single(&pdev->dev, data + BNXT_RX_DMA_OFFSET,
				  bp->rx_buf_use_size, PCI_DMA_FROMDEVICE);

	if (dma_mapping_error(&pdev->dev, *mapping)) {
		kfree(data);
		data = NULL;
	}
	return data;
}

static inline int bnxt_alloc_rx_data(struct bnxt *bp,
				     struct bnxt_rx_ring_info *rxr,
				     u16 prod, gfp_t gfp)
{
	struct rx_bd *rxbd = &rxr->rx_desc_ring[RX_RING(prod)][RX_IDX(prod)];
	struct bnxt_sw_rx_bd *rx_buf = &rxr->rx_buf_ring[prod];
	u8 *data;
	dma_addr_t mapping;

	data = __bnxt_alloc_rx_data(bp, &mapping, gfp);
	if (!data)
		return -ENOMEM;

	rx_buf->data = data;
	dma_unmap_addr_set(rx_buf, mapping, mapping);

	rxbd->rx_bd_haddr = cpu_to_le64(mapping);

	return 0;
}

static void bnxt_reuse_rx_data(struct bnxt_rx_ring_info *rxr, u16 cons,
			       u8 *data)
{
	u16 prod = rxr->rx_prod;
	struct bnxt_sw_rx_bd *cons_rx_buf, *prod_rx_buf;
	struct rx_bd *cons_bd, *prod_bd;

	prod_rx_buf = &rxr->rx_buf_ring[prod];
	cons_rx_buf = &rxr->rx_buf_ring[cons];

	prod_rx_buf->data = data;

	dma_unmap_addr_set(prod_rx_buf, mapping,
			   dma_unmap_addr(cons_rx_buf, mapping));

	prod_bd = &rxr->rx_desc_ring[RX_RING(prod)][RX_IDX(prod)];
	cons_bd = &rxr->rx_desc_ring[RX_RING(cons)][RX_IDX(cons)];

	prod_bd->rx_bd_haddr = cons_bd->rx_bd_haddr;
}

static inline u16 bnxt_find_next_agg_idx(struct bnxt_rx_ring_info *rxr, u16 idx)
{
	u16 next, max = rxr->rx_agg_bmap_size;

	next = find_next_zero_bit(rxr->rx_agg_bmap, max, idx);
	if (next >= max)
		next = find_first_zero_bit(rxr->rx_agg_bmap, max);
	return next;
}

static inline int bnxt_alloc_rx_page(struct bnxt *bp,
				     struct bnxt_rx_ring_info *rxr,
				     u16 prod, gfp_t gfp)
{
	struct rx_bd *rxbd =
		&rxr->rx_agg_desc_ring[RX_RING(prod)][RX_IDX(prod)];
	struct bnxt_sw_rx_agg_bd *rx_agg_buf;
	struct pci_dev *pdev = bp->pdev;
	struct page *page;
	dma_addr_t mapping;
	u16 sw_prod = rxr->rx_sw_agg_prod;

	page = alloc_page(gfp);
	if (!page)
		return -ENOMEM;

	mapping = dma_map_page(&pdev->dev, page, 0, PAGE_SIZE,
			       PCI_DMA_FROMDEVICE);
	if (dma_mapping_error(&pdev->dev, mapping)) {
		__free_page(page);
		return -EIO;
	}

	if (unlikely(test_bit(sw_prod, rxr->rx_agg_bmap)))
		sw_prod = bnxt_find_next_agg_idx(rxr, sw_prod);

	__set_bit(sw_prod, rxr->rx_agg_bmap);
	rx_agg_buf = &rxr->rx_agg_ring[sw_prod];
	rxr->rx_sw_agg_prod = NEXT_RX_AGG(sw_prod);

	rx_agg_buf->page = page;
	rx_agg_buf->mapping = mapping;
	rxbd->rx_bd_haddr = cpu_to_le64(mapping);
	rxbd->rx_bd_opaque = sw_prod;
	return 0;
}

static void bnxt_reuse_rx_agg_bufs(struct bnxt_napi *bnapi, u16 cp_cons,
				   u32 agg_bufs)
{
	struct bnxt *bp = bnapi->bp;
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	struct bnxt_rx_ring_info *rxr = &bnapi->rx_ring;
	u16 prod = rxr->rx_agg_prod;
	u16 sw_prod = rxr->rx_sw_agg_prod;
	u32 i;

	for (i = 0; i < agg_bufs; i++) {
		u16 cons;
		struct rx_agg_cmp *agg;
		struct bnxt_sw_rx_agg_bd *cons_rx_buf, *prod_rx_buf;
		struct rx_bd *prod_bd;
		struct page *page;

		agg = (struct rx_agg_cmp *)
			&cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];
		cons = agg->rx_agg_cmp_opaque;
		__clear_bit(cons, rxr->rx_agg_bmap);

		if (unlikely(test_bit(sw_prod, rxr->rx_agg_bmap)))
			sw_prod = bnxt_find_next_agg_idx(rxr, sw_prod);

		__set_bit(sw_prod, rxr->rx_agg_bmap);
		prod_rx_buf = &rxr->rx_agg_ring[sw_prod];
		cons_rx_buf = &rxr->rx_agg_ring[cons];

		/* It is possible for sw_prod to be equal to cons, so
		 * set cons_rx_buf->page to NULL first.
		 */
		page = cons_rx_buf->page;
		cons_rx_buf->page = NULL;
		prod_rx_buf->page = page;

		prod_rx_buf->mapping = cons_rx_buf->mapping;

		prod_bd = &rxr->rx_agg_desc_ring[RX_RING(prod)][RX_IDX(prod)];

		prod_bd->rx_bd_haddr = cpu_to_le64(cons_rx_buf->mapping);
		prod_bd->rx_bd_opaque = sw_prod;

		prod = NEXT_RX_AGG(prod);
		sw_prod = NEXT_RX_AGG(sw_prod);
		cp_cons = NEXT_CMP(cp_cons);
	}
	rxr->rx_agg_prod = prod;
	rxr->rx_sw_agg_prod = sw_prod;
}

static struct sk_buff *bnxt_rx_skb(struct bnxt *bp,
				   struct bnxt_rx_ring_info *rxr, u16 cons,
				   u16 prod, u8 *data, dma_addr_t dma_addr,
				   unsigned int len)
{
	int err;
	struct sk_buff *skb;

	err = bnxt_alloc_rx_data(bp, rxr, prod, GFP_ATOMIC);
	if (unlikely(err)) {
		bnxt_reuse_rx_data(rxr, cons, data);
		return NULL;
	}

	skb = build_skb(data, 0);
	dma_unmap_single(&bp->pdev->dev, dma_addr, bp->rx_buf_use_size,
			 PCI_DMA_FROMDEVICE);
	if (!skb) {
		kfree(data);
		return NULL;
	}

	skb_reserve(skb, BNXT_RX_OFFSET);
	skb_put(skb, len);
	return skb;
}

static struct sk_buff *bnxt_rx_pages(struct bnxt *bp, struct bnxt_napi *bnapi,
				     struct sk_buff *skb, u16 cp_cons,
				     u32 agg_bufs)
{
	struct pci_dev *pdev = bp->pdev;
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	struct bnxt_rx_ring_info *rxr = &bnapi->rx_ring;
	u16 prod = rxr->rx_agg_prod;
	u32 i;

	for (i = 0; i < agg_bufs; i++) {
		u16 cons, frag_len;
		struct rx_agg_cmp *agg;
		struct bnxt_sw_rx_agg_bd *cons_rx_buf;
		struct page *page;
		dma_addr_t mapping;

		agg = (struct rx_agg_cmp *)
			&cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];
		cons = agg->rx_agg_cmp_opaque;
		frag_len = (le32_to_cpu(agg->rx_agg_cmp_len_flags_type) &
			    RX_AGG_CMP_LEN) >> RX_AGG_CMP_LEN_SHIFT;

		cons_rx_buf = &rxr->rx_agg_ring[cons];
		skb_fill_page_desc(skb, i, cons_rx_buf->page, 0, frag_len);
		__clear_bit(cons, rxr->rx_agg_bmap);

		/* It is possible for bnxt_alloc_rx_page() to allocate
		 * a sw_prod index that equals the cons index, so we
		 * need to clear the cons entry now.
		 */
		mapping = dma_unmap_addr(cons_rx_buf, mapping);
		page = cons_rx_buf->page;
		cons_rx_buf->page = NULL;

		if (bnxt_alloc_rx_page(bp, rxr, prod, GFP_ATOMIC) != 0) {
			struct skb_shared_info *shinfo;
			unsigned int nr_frags;

			shinfo = skb_shinfo(skb);
			nr_frags = --shinfo->nr_frags;
			__skb_frag_set_page(&shinfo->frags[nr_frags], NULL);

			dev_kfree_skb(skb);

			cons_rx_buf->page = page;

			/* Update prod since possibly some pages have been
			 * allocated already.
			 */
			rxr->rx_agg_prod = prod;
			bnxt_reuse_rx_agg_bufs(bnapi, cp_cons, agg_bufs - i);
			return NULL;
		}

		dma_unmap_page(&pdev->dev, mapping, PAGE_SIZE,
			       PCI_DMA_FROMDEVICE);

		skb->data_len += frag_len;
		skb->len += frag_len;
		skb->truesize += PAGE_SIZE;

		prod = NEXT_RX_AGG(prod);
		cp_cons = NEXT_CMP(cp_cons);
	}
	rxr->rx_agg_prod = prod;
	return skb;
}

static int bnxt_agg_bufs_valid(struct bnxt *bp, struct bnxt_cp_ring_info *cpr,
			       u8 agg_bufs, u32 *raw_cons)
{
	u16 last;
	struct rx_agg_cmp *agg;

	*raw_cons = ADV_RAW_CMP(*raw_cons, agg_bufs);
	last = RING_CMP(*raw_cons);
	agg = (struct rx_agg_cmp *)
		&cpr->cp_desc_ring[CP_RING(last)][CP_IDX(last)];
	return RX_AGG_CMP_VALID(agg, *raw_cons);
}

static inline struct sk_buff *bnxt_copy_skb(struct bnxt_napi *bnapi, u8 *data,
					    unsigned int len,
					    dma_addr_t mapping)
{
	struct bnxt *bp = bnapi->bp;
	struct pci_dev *pdev = bp->pdev;
	struct sk_buff *skb;

	skb = napi_alloc_skb(&bnapi->napi, len);
	if (!skb)
		return NULL;

	dma_sync_single_for_cpu(&pdev->dev, mapping,
				bp->rx_copy_thresh, PCI_DMA_FROMDEVICE);

	memcpy(skb->data - BNXT_RX_OFFSET, data, len + BNXT_RX_OFFSET);

	dma_sync_single_for_device(&pdev->dev, mapping,
				   bp->rx_copy_thresh,
				   PCI_DMA_FROMDEVICE);

	skb_put(skb, len);
	return skb;
}

static void bnxt_tpa_start(struct bnxt *bp, struct bnxt_rx_ring_info *rxr,
			   struct rx_tpa_start_cmp *tpa_start,
			   struct rx_tpa_start_cmp_ext *tpa_start1)
{
	u8 agg_id = TPA_START_AGG_ID(tpa_start);
	u16 cons, prod;
	struct bnxt_tpa_info *tpa_info;
	struct bnxt_sw_rx_bd *cons_rx_buf, *prod_rx_buf;
	struct rx_bd *prod_bd;
	dma_addr_t mapping;

	cons = tpa_start->rx_tpa_start_cmp_opaque;
	prod = rxr->rx_prod;
	cons_rx_buf = &rxr->rx_buf_ring[cons];
	prod_rx_buf = &rxr->rx_buf_ring[prod];
	tpa_info = &rxr->rx_tpa[agg_id];

	prod_rx_buf->data = tpa_info->data;

	mapping = tpa_info->mapping;
	dma_unmap_addr_set(prod_rx_buf, mapping, mapping);

	prod_bd = &rxr->rx_desc_ring[RX_RING(prod)][RX_IDX(prod)];

	prod_bd->rx_bd_haddr = cpu_to_le64(mapping);

	tpa_info->data = cons_rx_buf->data;
	cons_rx_buf->data = NULL;
	tpa_info->mapping = dma_unmap_addr(cons_rx_buf, mapping);

	tpa_info->len =
		le32_to_cpu(tpa_start->rx_tpa_start_cmp_len_flags_type) >>
				RX_TPA_START_CMP_LEN_SHIFT;
	if (likely(TPA_START_HASH_VALID(tpa_start))) {
		u32 hash_type = TPA_START_HASH_TYPE(tpa_start);

		tpa_info->hash_type = PKT_HASH_TYPE_L4;
		tpa_info->gso_type = SKB_GSO_TCPV4;
		/* RSS profiles 1 and 3 with extract code 0 for inner 4-tuple */
		if (hash_type == 3)
			tpa_info->gso_type = SKB_GSO_TCPV6;
		tpa_info->rss_hash =
			le32_to_cpu(tpa_start->rx_tpa_start_cmp_rss_hash);
	} else {
		tpa_info->hash_type = PKT_HASH_TYPE_NONE;
		tpa_info->gso_type = 0;
		if (netif_msg_rx_err(bp))
			netdev_warn(bp->dev, "TPA packet without valid hash\n");
	}
	tpa_info->flags2 = le32_to_cpu(tpa_start1->rx_tpa_start_cmp_flags2);
	tpa_info->metadata = le32_to_cpu(tpa_start1->rx_tpa_start_cmp_metadata);

	rxr->rx_prod = NEXT_RX(prod);
	cons = NEXT_RX(cons);
	cons_rx_buf = &rxr->rx_buf_ring[cons];

	bnxt_reuse_rx_data(rxr, cons, cons_rx_buf->data);
	rxr->rx_prod = NEXT_RX(rxr->rx_prod);
	cons_rx_buf->data = NULL;
}

static void bnxt_abort_tpa(struct bnxt *bp, struct bnxt_napi *bnapi,
			   u16 cp_cons, u32 agg_bufs)
{
	if (agg_bufs)
		bnxt_reuse_rx_agg_bufs(bnapi, cp_cons, agg_bufs);
}

#define BNXT_IPV4_HDR_SIZE	(sizeof(struct iphdr) + sizeof(struct tcphdr))
#define BNXT_IPV6_HDR_SIZE	(sizeof(struct ipv6hdr) + sizeof(struct tcphdr))

static inline struct sk_buff *bnxt_gro_skb(struct bnxt_tpa_info *tpa_info,
					   struct rx_tpa_end_cmp *tpa_end,
					   struct rx_tpa_end_cmp_ext *tpa_end1,
					   struct sk_buff *skb)
{
#ifdef CONFIG_INET
	struct tcphdr *th;
	int payload_off, tcp_opt_len = 0;
	int len, nw_off;

	NAPI_GRO_CB(skb)->count = TPA_END_TPA_SEGS(tpa_end);
	skb_shinfo(skb)->gso_size =
		le32_to_cpu(tpa_end1->rx_tpa_end_cmp_seg_len);
	skb_shinfo(skb)->gso_type = tpa_info->gso_type;
	payload_off = (le32_to_cpu(tpa_end->rx_tpa_end_cmp_misc_v1) &
		       RX_TPA_END_CMP_PAYLOAD_OFFSET) >>
		      RX_TPA_END_CMP_PAYLOAD_OFFSET_SHIFT;
	if (TPA_END_GRO_TS(tpa_end))
		tcp_opt_len = 12;

	if (tpa_info->gso_type == SKB_GSO_TCPV4) {
		struct iphdr *iph;

		nw_off = payload_off - BNXT_IPV4_HDR_SIZE - tcp_opt_len -
			 ETH_HLEN;
		skb_set_network_header(skb, nw_off);
		iph = ip_hdr(skb);
		skb_set_transport_header(skb, nw_off + sizeof(struct iphdr));
		len = skb->len - skb_transport_offset(skb);
		th = tcp_hdr(skb);
		th->check = ~tcp_v4_check(len, iph->saddr, iph->daddr, 0);
	} else if (tpa_info->gso_type == SKB_GSO_TCPV6) {
		struct ipv6hdr *iph;

		nw_off = payload_off - BNXT_IPV6_HDR_SIZE - tcp_opt_len -
			 ETH_HLEN;
		skb_set_network_header(skb, nw_off);
		iph = ipv6_hdr(skb);
		skb_set_transport_header(skb, nw_off + sizeof(struct ipv6hdr));
		len = skb->len - skb_transport_offset(skb);
		th = tcp_hdr(skb);
		th->check = ~tcp_v6_check(len, &iph->saddr, &iph->daddr, 0);
	} else {
		dev_kfree_skb_any(skb);
		return NULL;
	}
	tcp_gro_complete(skb);

	if (nw_off) { /* tunnel */
		struct udphdr *uh = NULL;

		if (skb->protocol == htons(ETH_P_IP)) {
			struct iphdr *iph = (struct iphdr *)skb->data;

			if (iph->protocol == IPPROTO_UDP)
				uh = (struct udphdr *)(iph + 1);
		} else {
			struct ipv6hdr *iph = (struct ipv6hdr *)skb->data;

			if (iph->nexthdr == IPPROTO_UDP)
				uh = (struct udphdr *)(iph + 1);
		}
		if (uh) {
			if (uh->check)
				skb_shinfo(skb)->gso_type |=
					SKB_GSO_UDP_TUNNEL_CSUM;
			else
				skb_shinfo(skb)->gso_type |= SKB_GSO_UDP_TUNNEL;
		}
	}
#endif
	return skb;
}

static inline struct sk_buff *bnxt_tpa_end(struct bnxt *bp,
					   struct bnxt_napi *bnapi,
					   u32 *raw_cons,
					   struct rx_tpa_end_cmp *tpa_end,
					   struct rx_tpa_end_cmp_ext *tpa_end1,
					   bool *agg_event)
{
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	struct bnxt_rx_ring_info *rxr = &bnapi->rx_ring;
	u8 agg_id = TPA_END_AGG_ID(tpa_end);
	u8 *data, agg_bufs;
	u16 cp_cons = RING_CMP(*raw_cons);
	unsigned int len;
	struct bnxt_tpa_info *tpa_info;
	dma_addr_t mapping;
	struct sk_buff *skb;

	tpa_info = &rxr->rx_tpa[agg_id];
	data = tpa_info->data;
	prefetch(data);
	len = tpa_info->len;
	mapping = tpa_info->mapping;

	agg_bufs = (le32_to_cpu(tpa_end->rx_tpa_end_cmp_misc_v1) &
		    RX_TPA_END_CMP_AGG_BUFS) >> RX_TPA_END_CMP_AGG_BUFS_SHIFT;

	if (agg_bufs) {
		if (!bnxt_agg_bufs_valid(bp, cpr, agg_bufs, raw_cons))
			return ERR_PTR(-EBUSY);

		*agg_event = true;
		cp_cons = NEXT_CMP(cp_cons);
	}

	if (unlikely(agg_bufs > MAX_SKB_FRAGS)) {
		bnxt_abort_tpa(bp, bnapi, cp_cons, agg_bufs);
		netdev_warn(bp->dev, "TPA frags %d exceeded MAX_SKB_FRAGS %d\n",
			    agg_bufs, (int)MAX_SKB_FRAGS);
		return NULL;
	}

	if (len <= bp->rx_copy_thresh) {
		skb = bnxt_copy_skb(bnapi, data, len, mapping);
		if (!skb) {
			bnxt_abort_tpa(bp, bnapi, cp_cons, agg_bufs);
			return NULL;
		}
	} else {
		u8 *new_data;
		dma_addr_t new_mapping;

		new_data = __bnxt_alloc_rx_data(bp, &new_mapping, GFP_ATOMIC);
		if (!new_data) {
			bnxt_abort_tpa(bp, bnapi, cp_cons, agg_bufs);
			return NULL;
		}

		tpa_info->data = new_data;
		tpa_info->mapping = new_mapping;

		skb = build_skb(data, 0);
		dma_unmap_single(&bp->pdev->dev, mapping, bp->rx_buf_use_size,
				 PCI_DMA_FROMDEVICE);

		if (!skb) {
			kfree(data);
			bnxt_abort_tpa(bp, bnapi, cp_cons, agg_bufs);
			return NULL;
		}
		skb_reserve(skb, BNXT_RX_OFFSET);
		skb_put(skb, len);
	}

	if (agg_bufs) {
		skb = bnxt_rx_pages(bp, bnapi, skb, cp_cons, agg_bufs);
		if (!skb) {
			/* Page reuse already handled by bnxt_rx_pages(). */
			return NULL;
		}
	}
	skb->protocol = eth_type_trans(skb, bp->dev);

	if (tpa_info->hash_type != PKT_HASH_TYPE_NONE)
		skb_set_hash(skb, tpa_info->rss_hash, tpa_info->hash_type);

	if (tpa_info->flags2 & RX_CMP_FLAGS2_META_FORMAT_VLAN) {
		netdev_features_t features = skb->dev->features;
		u16 vlan_proto = tpa_info->metadata >>
			RX_CMP_FLAGS2_METADATA_TPID_SFT;

		if (((features & NETIF_F_HW_VLAN_CTAG_RX) &&
		     vlan_proto == ETH_P_8021Q) ||
		    ((features & NETIF_F_HW_VLAN_STAG_RX) &&
		     vlan_proto == ETH_P_8021AD)) {
			__vlan_hwaccel_put_tag(skb, htons(vlan_proto),
					       tpa_info->metadata &
					       RX_CMP_FLAGS2_METADATA_VID_MASK);
		}
	}

	skb_checksum_none_assert(skb);
	if (likely(tpa_info->flags2 & RX_TPA_START_CMP_FLAGS2_L4_CS_CALC)) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->csum_level =
			(tpa_info->flags2 & RX_CMP_FLAGS2_T_L4_CS_CALC) >> 3;
	}

	if (TPA_END_GRO(tpa_end))
		skb = bnxt_gro_skb(tpa_info, tpa_end, tpa_end1, skb);

	return skb;
}

/* returns the following:
 * 1       - 1 packet successfully received
 * 0       - successful TPA_START, packet not completed yet
 * -EBUSY  - completion ring does not have all the agg buffers yet
 * -ENOMEM - packet aborted due to out of memory
 * -EIO    - packet aborted due to hw error indicated in BD
 */
static int bnxt_rx_pkt(struct bnxt *bp, struct bnxt_napi *bnapi, u32 *raw_cons,
		       bool *agg_event)
{
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	struct bnxt_rx_ring_info *rxr = &bnapi->rx_ring;
	struct net_device *dev = bp->dev;
	struct rx_cmp *rxcmp;
	struct rx_cmp_ext *rxcmp1;
	u32 tmp_raw_cons = *raw_cons;
	u16 cons, prod, cp_cons = RING_CMP(tmp_raw_cons);
	struct bnxt_sw_rx_bd *rx_buf;
	unsigned int len;
	u8 *data, agg_bufs, cmp_type;
	dma_addr_t dma_addr;
	struct sk_buff *skb;
	int rc = 0;

	rxcmp = (struct rx_cmp *)
			&cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

	tmp_raw_cons = NEXT_RAW_CMP(tmp_raw_cons);
	cp_cons = RING_CMP(tmp_raw_cons);
	rxcmp1 = (struct rx_cmp_ext *)
			&cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

	if (!RX_CMP_VALID(rxcmp1, tmp_raw_cons))
		return -EBUSY;

	cmp_type = RX_CMP_TYPE(rxcmp);

	prod = rxr->rx_prod;

	if (cmp_type == CMP_TYPE_RX_L2_TPA_START_CMP) {
		bnxt_tpa_start(bp, rxr, (struct rx_tpa_start_cmp *)rxcmp,
			       (struct rx_tpa_start_cmp_ext *)rxcmp1);

		goto next_rx_no_prod;

	} else if (cmp_type == CMP_TYPE_RX_L2_TPA_END_CMP) {
		skb = bnxt_tpa_end(bp, bnapi, &tmp_raw_cons,
				   (struct rx_tpa_end_cmp *)rxcmp,
				   (struct rx_tpa_end_cmp_ext *)rxcmp1,
				   agg_event);

		if (unlikely(IS_ERR(skb)))
			return -EBUSY;

		rc = -ENOMEM;
		if (likely(skb)) {
			skb_record_rx_queue(skb, bnapi->index);
			skb_mark_napi_id(skb, &bnapi->napi);
			if (bnxt_busy_polling(bnapi))
				netif_receive_skb(skb);
			else
				napi_gro_receive(&bnapi->napi, skb);
			rc = 1;
		}
		goto next_rx_no_prod;
	}

	cons = rxcmp->rx_cmp_opaque;
	rx_buf = &rxr->rx_buf_ring[cons];
	data = rx_buf->data;
	prefetch(data);

	agg_bufs = (le32_to_cpu(rxcmp->rx_cmp_misc_v1) & RX_CMP_AGG_BUFS) >>
				RX_CMP_AGG_BUFS_SHIFT;

	if (agg_bufs) {
		if (!bnxt_agg_bufs_valid(bp, cpr, agg_bufs, &tmp_raw_cons))
			return -EBUSY;

		cp_cons = NEXT_CMP(cp_cons);
		*agg_event = true;
	}

	rx_buf->data = NULL;
	if (rxcmp1->rx_cmp_cfa_code_errors_v2 & RX_CMP_L2_ERRORS) {
		bnxt_reuse_rx_data(rxr, cons, data);
		if (agg_bufs)
			bnxt_reuse_rx_agg_bufs(bnapi, cp_cons, agg_bufs);

		rc = -EIO;
		goto next_rx;
	}

	len = le32_to_cpu(rxcmp->rx_cmp_len_flags_type) >> RX_CMP_LEN_SHIFT;
	dma_addr = dma_unmap_addr(rx_buf, mapping);

	if (len <= bp->rx_copy_thresh) {
		skb = bnxt_copy_skb(bnapi, data, len, dma_addr);
		bnxt_reuse_rx_data(rxr, cons, data);
		if (!skb) {
			rc = -ENOMEM;
			goto next_rx;
		}
	} else {
		skb = bnxt_rx_skb(bp, rxr, cons, prod, data, dma_addr, len);
		if (!skb) {
			rc = -ENOMEM;
			goto next_rx;
		}
	}

	if (agg_bufs) {
		skb = bnxt_rx_pages(bp, bnapi, skb, cp_cons, agg_bufs);
		if (!skb) {
			rc = -ENOMEM;
			goto next_rx;
		}
	}

	if (RX_CMP_HASH_VALID(rxcmp)) {
		u32 hash_type = RX_CMP_HASH_TYPE(rxcmp);
		enum pkt_hash_types type = PKT_HASH_TYPE_L4;

		/* RSS profiles 1 and 3 with extract code 0 for inner 4-tuple */
		if (hash_type != 1 && hash_type != 3)
			type = PKT_HASH_TYPE_L3;
		skb_set_hash(skb, le32_to_cpu(rxcmp->rx_cmp_rss_hash), type);
	}

	skb->protocol = eth_type_trans(skb, dev);

	if (rxcmp1->rx_cmp_flags2 &
	    cpu_to_le32(RX_CMP_FLAGS2_META_FORMAT_VLAN)) {
		netdev_features_t features = skb->dev->features;
		u32 meta_data = le32_to_cpu(rxcmp1->rx_cmp_meta_data);
		u16 vlan_proto = meta_data >> RX_CMP_FLAGS2_METADATA_TPID_SFT;

		if (((features & NETIF_F_HW_VLAN_CTAG_RX) &&
		     vlan_proto == ETH_P_8021Q) ||
		    ((features & NETIF_F_HW_VLAN_STAG_RX) &&
		     vlan_proto == ETH_P_8021AD))
			__vlan_hwaccel_put_tag(skb, htons(vlan_proto),
					       meta_data &
					       RX_CMP_FLAGS2_METADATA_VID_MASK);
	}

	skb_checksum_none_assert(skb);
	if (RX_CMP_L4_CS_OK(rxcmp1)) {
		if (dev->features & NETIF_F_RXCSUM) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			skb->csum_level = RX_CMP_ENCAP(rxcmp1);
		}
	} else {
		if (rxcmp1->rx_cmp_cfa_code_errors_v2 & RX_CMP_L4_CS_ERR_BITS)
			cpr->rx_l4_csum_errors++;
	}

	skb_record_rx_queue(skb, bnapi->index);
	skb_mark_napi_id(skb, &bnapi->napi);
	if (bnxt_busy_polling(bnapi))
		netif_receive_skb(skb);
	else
		napi_gro_receive(&bnapi->napi, skb);
	rc = 1;

next_rx:
	rxr->rx_prod = NEXT_RX(prod);

next_rx_no_prod:
	*raw_cons = tmp_raw_cons;

	return rc;
}

static int bnxt_async_event_process(struct bnxt *bp,
				    struct hwrm_async_event_cmpl *cmpl)
{
	u16 event_id = le16_to_cpu(cmpl->event_id);

	/* TODO CHIMP_FW: Define event id's for link change, error etc */
	switch (event_id) {
	case HWRM_ASYNC_EVENT_CMPL_EVENT_ID_LINK_STATUS_CHANGE:
		set_bit(BNXT_LINK_CHNG_SP_EVENT, &bp->sp_event);
		schedule_work(&bp->sp_task);
		break;
	default:
		netdev_err(bp->dev, "unhandled ASYNC event (id 0x%x)\n",
			   event_id);
		break;
	}
	return 0;
}

static int bnxt_hwrm_handler(struct bnxt *bp, struct tx_cmp *txcmp)
{
	u16 cmpl_type = TX_CMP_TYPE(txcmp), vf_id, seq_id;
	struct hwrm_cmpl *h_cmpl = (struct hwrm_cmpl *)txcmp;
	struct hwrm_fwd_req_cmpl *fwd_req_cmpl =
				(struct hwrm_fwd_req_cmpl *)txcmp;

	switch (cmpl_type) {
	case CMPL_BASE_TYPE_HWRM_DONE:
		seq_id = le16_to_cpu(h_cmpl->sequence_id);
		if (seq_id == bp->hwrm_intr_seq_id)
			bp->hwrm_intr_seq_id = HWRM_SEQ_ID_INVALID;
		else
			netdev_err(bp->dev, "Invalid hwrm seq id %d\n", seq_id);
		break;

	case CMPL_BASE_TYPE_HWRM_FWD_REQ:
		vf_id = le16_to_cpu(fwd_req_cmpl->source_id);

		if ((vf_id < bp->pf.first_vf_id) ||
		    (vf_id >= bp->pf.first_vf_id + bp->pf.active_vfs)) {
			netdev_err(bp->dev, "Msg contains invalid VF id %x\n",
				   vf_id);
			return -EINVAL;
		}

		set_bit(vf_id - bp->pf.first_vf_id, bp->pf.vf_event_bmap);
		set_bit(BNXT_HWRM_EXEC_FWD_REQ_SP_EVENT, &bp->sp_event);
		schedule_work(&bp->sp_task);
		break;

	case CMPL_BASE_TYPE_HWRM_ASYNC_EVENT:
		bnxt_async_event_process(bp,
					 (struct hwrm_async_event_cmpl *)txcmp);

	default:
		break;
	}

	return 0;
}

static irqreturn_t bnxt_msix(int irq, void *dev_instance)
{
	struct bnxt_napi *bnapi = dev_instance;
	struct bnxt *bp = bnapi->bp;
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	u32 cons = RING_CMP(cpr->cp_raw_cons);

	prefetch(&cpr->cp_desc_ring[CP_RING(cons)][CP_IDX(cons)]);
	napi_schedule(&bnapi->napi);
	return IRQ_HANDLED;
}

static inline int bnxt_has_work(struct bnxt *bp, struct bnxt_cp_ring_info *cpr)
{
	u32 raw_cons = cpr->cp_raw_cons;
	u16 cons = RING_CMP(raw_cons);
	struct tx_cmp *txcmp;

	txcmp = &cpr->cp_desc_ring[CP_RING(cons)][CP_IDX(cons)];

	return TX_CMP_VALID(txcmp, raw_cons);
}

#define CAG_LEGACY_INT_STATUS	0x2014

static irqreturn_t bnxt_inta(int irq, void *dev_instance)
{
	struct bnxt_napi *bnapi = dev_instance;
	struct bnxt *bp = bnapi->bp;
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	u32 cons = RING_CMP(cpr->cp_raw_cons);
	u32 int_status;

	prefetch(&cpr->cp_desc_ring[CP_RING(cons)][CP_IDX(cons)]);

	if (!bnxt_has_work(bp, cpr)) {
		int_status = readl(bp->bar0 + CAG_LEGACY_INT_STATUS);
		/* return if erroneous interrupt */
		if (!(int_status & (0x10000 << cpr->cp_ring_struct.fw_ring_id)))
			return IRQ_NONE;
	}

	/* disable ring IRQ */
	BNXT_CP_DB_IRQ_DIS(cpr->cp_doorbell);

	/* Return here if interrupt is shared and is disabled. */
	if (unlikely(atomic_read(&bp->intr_sem) != 0))
		return IRQ_HANDLED;

	napi_schedule(&bnapi->napi);
	return IRQ_HANDLED;
}

static int bnxt_poll_work(struct bnxt *bp, struct bnxt_napi *bnapi, int budget)
{
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	u32 raw_cons = cpr->cp_raw_cons;
	u32 cons;
	int tx_pkts = 0;
	int rx_pkts = 0;
	bool rx_event = false;
	bool agg_event = false;
	struct tx_cmp *txcmp;

	while (1) {
		int rc;

		cons = RING_CMP(raw_cons);
		txcmp = &cpr->cp_desc_ring[CP_RING(cons)][CP_IDX(cons)];

		if (!TX_CMP_VALID(txcmp, raw_cons))
			break;

		if (TX_CMP_TYPE(txcmp) == CMP_TYPE_TX_L2_CMP) {
			tx_pkts++;
			/* return full budget so NAPI will complete. */
			if (unlikely(tx_pkts > bp->tx_wake_thresh))
				rx_pkts = budget;
		} else if ((TX_CMP_TYPE(txcmp) & 0x30) == 0x10) {
			rc = bnxt_rx_pkt(bp, bnapi, &raw_cons, &agg_event);
			if (likely(rc >= 0))
				rx_pkts += rc;
			else if (rc == -EBUSY)	/* partial completion */
				break;
			rx_event = true;
		} else if (unlikely((TX_CMP_TYPE(txcmp) ==
				     CMPL_BASE_TYPE_HWRM_DONE) ||
				    (TX_CMP_TYPE(txcmp) ==
				     CMPL_BASE_TYPE_HWRM_FWD_REQ) ||
				    (TX_CMP_TYPE(txcmp) ==
				     CMPL_BASE_TYPE_HWRM_ASYNC_EVENT))) {
			bnxt_hwrm_handler(bp, txcmp);
		}
		raw_cons = NEXT_RAW_CMP(raw_cons);

		if (rx_pkts == budget)
			break;
	}

	cpr->cp_raw_cons = raw_cons;
	/* ACK completion ring before freeing tx ring and producing new
	 * buffers in rx/agg rings to prevent overflowing the completion
	 * ring.
	 */
	BNXT_CP_DB(cpr->cp_doorbell, cpr->cp_raw_cons);

	if (tx_pkts)
		bnxt_tx_int(bp, bnapi, tx_pkts);

	if (rx_event) {
		struct bnxt_rx_ring_info *rxr = &bnapi->rx_ring;

		writel(DB_KEY_RX | rxr->rx_prod, rxr->rx_doorbell);
		writel(DB_KEY_RX | rxr->rx_prod, rxr->rx_doorbell);
		if (agg_event) {
			writel(DB_KEY_RX | rxr->rx_agg_prod,
			       rxr->rx_agg_doorbell);
			writel(DB_KEY_RX | rxr->rx_agg_prod,
			       rxr->rx_agg_doorbell);
		}
	}
	return rx_pkts;
}

static int bnxt_poll(struct napi_struct *napi, int budget)
{
	struct bnxt_napi *bnapi = container_of(napi, struct bnxt_napi, napi);
	struct bnxt *bp = bnapi->bp;
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	int work_done = 0;

	if (!bnxt_lock_napi(bnapi))
		return budget;

	while (1) {
		work_done += bnxt_poll_work(bp, bnapi, budget - work_done);

		if (work_done >= budget)
			break;

		if (!bnxt_has_work(bp, cpr)) {
			napi_complete(napi);
			BNXT_CP_DB_REARM(cpr->cp_doorbell, cpr->cp_raw_cons);
			break;
		}
	}
	mmiowb();
	bnxt_unlock_napi(bnapi);
	return work_done;
}

#ifdef CONFIG_NET_RX_BUSY_POLL
static int bnxt_busy_poll(struct napi_struct *napi)
{
	struct bnxt_napi *bnapi = container_of(napi, struct bnxt_napi, napi);
	struct bnxt *bp = bnapi->bp;
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	int rx_work, budget = 4;

	if (atomic_read(&bp->intr_sem) != 0)
		return LL_FLUSH_FAILED;

	if (!bnxt_lock_poll(bnapi))
		return LL_FLUSH_BUSY;

	rx_work = bnxt_poll_work(bp, bnapi, budget);

	BNXT_CP_DB_REARM(cpr->cp_doorbell, cpr->cp_raw_cons);

	bnxt_unlock_poll(bnapi);
	return rx_work;
}
#endif

static void bnxt_free_tx_skbs(struct bnxt *bp)
{
	int i, max_idx;
	struct pci_dev *pdev = bp->pdev;

	if (!bp->bnapi)
		return;

	max_idx = bp->tx_nr_pages * TX_DESC_CNT;
	for (i = 0; i < bp->tx_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_tx_ring_info *txr;
		int j;

		if (!bnapi)
			continue;

		txr = &bnapi->tx_ring;
		for (j = 0; j < max_idx;) {
			struct bnxt_sw_tx_bd *tx_buf = &txr->tx_buf_ring[j];
			struct sk_buff *skb = tx_buf->skb;
			int k, last;

			if (!skb) {
				j++;
				continue;
			}

			tx_buf->skb = NULL;

			if (tx_buf->is_push) {
				dev_kfree_skb(skb);
				j += 2;
				continue;
			}

			dma_unmap_single(&pdev->dev,
					 dma_unmap_addr(tx_buf, mapping),
					 skb_headlen(skb),
					 PCI_DMA_TODEVICE);

			last = tx_buf->nr_frags;
			j += 2;
			for (k = 0; k < last; k++, j = NEXT_TX(j)) {
				skb_frag_t *frag = &skb_shinfo(skb)->frags[k];

				tx_buf = &txr->tx_buf_ring[j];
				dma_unmap_page(
					&pdev->dev,
					dma_unmap_addr(tx_buf, mapping),
					skb_frag_size(frag), PCI_DMA_TODEVICE);
			}
			dev_kfree_skb(skb);
		}
		netdev_tx_reset_queue(netdev_get_tx_queue(bp->dev, i));
	}
}

static void bnxt_free_rx_skbs(struct bnxt *bp)
{
	int i, max_idx, max_agg_idx;
	struct pci_dev *pdev = bp->pdev;

	if (!bp->bnapi)
		return;

	max_idx = bp->rx_nr_pages * RX_DESC_CNT;
	max_agg_idx = bp->rx_agg_nr_pages * RX_DESC_CNT;
	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_rx_ring_info *rxr;
		int j;

		if (!bnapi)
			continue;

		rxr = &bnapi->rx_ring;

		if (rxr->rx_tpa) {
			for (j = 0; j < MAX_TPA; j++) {
				struct bnxt_tpa_info *tpa_info =
							&rxr->rx_tpa[j];
				u8 *data = tpa_info->data;

				if (!data)
					continue;

				dma_unmap_single(
					&pdev->dev,
					dma_unmap_addr(tpa_info, mapping),
					bp->rx_buf_use_size,
					PCI_DMA_FROMDEVICE);

				tpa_info->data = NULL;

				kfree(data);
			}
		}

		for (j = 0; j < max_idx; j++) {
			struct bnxt_sw_rx_bd *rx_buf = &rxr->rx_buf_ring[j];
			u8 *data = rx_buf->data;

			if (!data)
				continue;

			dma_unmap_single(&pdev->dev,
					 dma_unmap_addr(rx_buf, mapping),
					 bp->rx_buf_use_size,
					 PCI_DMA_FROMDEVICE);

			rx_buf->data = NULL;

			kfree(data);
		}

		for (j = 0; j < max_agg_idx; j++) {
			struct bnxt_sw_rx_agg_bd *rx_agg_buf =
				&rxr->rx_agg_ring[j];
			struct page *page = rx_agg_buf->page;

			if (!page)
				continue;

			dma_unmap_page(&pdev->dev,
				       dma_unmap_addr(rx_agg_buf, mapping),
				       PAGE_SIZE, PCI_DMA_FROMDEVICE);

			rx_agg_buf->page = NULL;
			__clear_bit(j, rxr->rx_agg_bmap);

			__free_page(page);
		}
	}
}

static void bnxt_free_skbs(struct bnxt *bp)
{
	bnxt_free_tx_skbs(bp);
	bnxt_free_rx_skbs(bp);
}

static void bnxt_free_ring(struct bnxt *bp, struct bnxt_ring_struct *ring)
{
	struct pci_dev *pdev = bp->pdev;
	int i;

	for (i = 0; i < ring->nr_pages; i++) {
		if (!ring->pg_arr[i])
			continue;

		dma_free_coherent(&pdev->dev, ring->page_size,
				  ring->pg_arr[i], ring->dma_arr[i]);

		ring->pg_arr[i] = NULL;
	}
	if (ring->pg_tbl) {
		dma_free_coherent(&pdev->dev, ring->nr_pages * 8,
				  ring->pg_tbl, ring->pg_tbl_map);
		ring->pg_tbl = NULL;
	}
	if (ring->vmem_size && *ring->vmem) {
		vfree(*ring->vmem);
		*ring->vmem = NULL;
	}
}

static int bnxt_alloc_ring(struct bnxt *bp, struct bnxt_ring_struct *ring)
{
	int i;
	struct pci_dev *pdev = bp->pdev;

	if (ring->nr_pages > 1) {
		ring->pg_tbl = dma_alloc_coherent(&pdev->dev,
						  ring->nr_pages * 8,
						  &ring->pg_tbl_map,
						  GFP_KERNEL);
		if (!ring->pg_tbl)
			return -ENOMEM;
	}

	for (i = 0; i < ring->nr_pages; i++) {
		ring->pg_arr[i] = dma_alloc_coherent(&pdev->dev,
						     ring->page_size,
						     &ring->dma_arr[i],
						     GFP_KERNEL);
		if (!ring->pg_arr[i])
			return -ENOMEM;

		if (ring->nr_pages > 1)
			ring->pg_tbl[i] = cpu_to_le64(ring->dma_arr[i]);
	}

	if (ring->vmem_size) {
		*ring->vmem = vzalloc(ring->vmem_size);
		if (!(*ring->vmem))
			return -ENOMEM;
	}
	return 0;
}

static void bnxt_free_rx_rings(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_rx_ring_info *rxr;
		struct bnxt_ring_struct *ring;

		if (!bnapi)
			continue;

		rxr = &bnapi->rx_ring;

		kfree(rxr->rx_tpa);
		rxr->rx_tpa = NULL;

		kfree(rxr->rx_agg_bmap);
		rxr->rx_agg_bmap = NULL;

		ring = &rxr->rx_ring_struct;
		bnxt_free_ring(bp, ring);

		ring = &rxr->rx_agg_ring_struct;
		bnxt_free_ring(bp, ring);
	}
}

static int bnxt_alloc_rx_rings(struct bnxt *bp)
{
	int i, rc, agg_rings = 0, tpa_rings = 0;

	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		agg_rings = 1;

	if (bp->flags & BNXT_FLAG_TPA)
		tpa_rings = 1;

	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_rx_ring_info *rxr;
		struct bnxt_ring_struct *ring;

		if (!bnapi)
			continue;

		rxr = &bnapi->rx_ring;
		ring = &rxr->rx_ring_struct;

		rc = bnxt_alloc_ring(bp, ring);
		if (rc)
			return rc;

		if (agg_rings) {
			u16 mem_size;

			ring = &rxr->rx_agg_ring_struct;
			rc = bnxt_alloc_ring(bp, ring);
			if (rc)
				return rc;

			rxr->rx_agg_bmap_size = bp->rx_agg_ring_mask + 1;
			mem_size = rxr->rx_agg_bmap_size / 8;
			rxr->rx_agg_bmap = kzalloc(mem_size, GFP_KERNEL);
			if (!rxr->rx_agg_bmap)
				return -ENOMEM;

			if (tpa_rings) {
				rxr->rx_tpa = kcalloc(MAX_TPA,
						sizeof(struct bnxt_tpa_info),
						GFP_KERNEL);
				if (!rxr->rx_tpa)
					return -ENOMEM;
			}
		}
	}
	return 0;
}

static void bnxt_free_tx_rings(struct bnxt *bp)
{
	int i;
	struct pci_dev *pdev = bp->pdev;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->tx_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_tx_ring_info *txr;
		struct bnxt_ring_struct *ring;

		if (!bnapi)
			continue;

		txr = &bnapi->tx_ring;

		if (txr->tx_push) {
			dma_free_coherent(&pdev->dev, bp->tx_push_size,
					  txr->tx_push, txr->tx_push_mapping);
			txr->tx_push = NULL;
		}

		ring = &txr->tx_ring_struct;

		bnxt_free_ring(bp, ring);
	}
}

static int bnxt_alloc_tx_rings(struct bnxt *bp)
{
	int i, j, rc;
	struct pci_dev *pdev = bp->pdev;

	bp->tx_push_size = 0;
	if (bp->tx_push_thresh) {
		int push_size;

		push_size  = L1_CACHE_ALIGN(sizeof(struct tx_push_bd) +
					bp->tx_push_thresh);

		if (push_size > 128) {
			push_size = 0;
			bp->tx_push_thresh = 0;
		}

		bp->tx_push_size = push_size;
	}

	for (i = 0, j = 0; i < bp->tx_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_tx_ring_info *txr;
		struct bnxt_ring_struct *ring;

		if (!bnapi)
			continue;

		txr = &bnapi->tx_ring;
		ring = &txr->tx_ring_struct;

		rc = bnxt_alloc_ring(bp, ring);
		if (rc)
			return rc;

		if (bp->tx_push_size) {
			struct tx_bd *txbd;
			dma_addr_t mapping;

			/* One pre-allocated DMA buffer to backup
			 * TX push operation
			 */
			txr->tx_push = dma_alloc_coherent(&pdev->dev,
						bp->tx_push_size,
						&txr->tx_push_mapping,
						GFP_KERNEL);

			if (!txr->tx_push)
				return -ENOMEM;

			txbd = &txr->tx_push->txbd1;

			mapping = txr->tx_push_mapping +
				sizeof(struct tx_push_bd);
			txbd->tx_bd_haddr = cpu_to_le64(mapping);

			memset(txbd + 1, 0, sizeof(struct tx_bd_ext));
		}
		ring->queue_id = bp->q_info[j].queue_id;
		if (i % bp->tx_nr_rings_per_tc == (bp->tx_nr_rings_per_tc - 1))
			j++;
	}
	return 0;
}

static void bnxt_free_cp_rings(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr;
		struct bnxt_ring_struct *ring;

		if (!bnapi)
			continue;

		cpr = &bnapi->cp_ring;
		ring = &cpr->cp_ring_struct;

		bnxt_free_ring(bp, ring);
	}
}

static int bnxt_alloc_cp_rings(struct bnxt *bp)
{
	int i, rc;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr;
		struct bnxt_ring_struct *ring;

		if (!bnapi)
			continue;

		cpr = &bnapi->cp_ring;
		ring = &cpr->cp_ring_struct;

		rc = bnxt_alloc_ring(bp, ring);
		if (rc)
			return rc;
	}
	return 0;
}

static void bnxt_init_ring_struct(struct bnxt *bp)
{
	int i;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr;
		struct bnxt_rx_ring_info *rxr;
		struct bnxt_tx_ring_info *txr;
		struct bnxt_ring_struct *ring;

		if (!bnapi)
			continue;

		cpr = &bnapi->cp_ring;
		ring = &cpr->cp_ring_struct;
		ring->nr_pages = bp->cp_nr_pages;
		ring->page_size = HW_CMPD_RING_SIZE;
		ring->pg_arr = (void **)cpr->cp_desc_ring;
		ring->dma_arr = cpr->cp_desc_mapping;
		ring->vmem_size = 0;

		rxr = &bnapi->rx_ring;
		ring = &rxr->rx_ring_struct;
		ring->nr_pages = bp->rx_nr_pages;
		ring->page_size = HW_RXBD_RING_SIZE;
		ring->pg_arr = (void **)rxr->rx_desc_ring;
		ring->dma_arr = rxr->rx_desc_mapping;
		ring->vmem_size = SW_RXBD_RING_SIZE * bp->rx_nr_pages;
		ring->vmem = (void **)&rxr->rx_buf_ring;

		ring = &rxr->rx_agg_ring_struct;
		ring->nr_pages = bp->rx_agg_nr_pages;
		ring->page_size = HW_RXBD_RING_SIZE;
		ring->pg_arr = (void **)rxr->rx_agg_desc_ring;
		ring->dma_arr = rxr->rx_agg_desc_mapping;
		ring->vmem_size = SW_RXBD_AGG_RING_SIZE * bp->rx_agg_nr_pages;
		ring->vmem = (void **)&rxr->rx_agg_ring;

		txr = &bnapi->tx_ring;
		ring = &txr->tx_ring_struct;
		ring->nr_pages = bp->tx_nr_pages;
		ring->page_size = HW_RXBD_RING_SIZE;
		ring->pg_arr = (void **)txr->tx_desc_ring;
		ring->dma_arr = txr->tx_desc_mapping;
		ring->vmem_size = SW_TXBD_RING_SIZE * bp->tx_nr_pages;
		ring->vmem = (void **)&txr->tx_buf_ring;
	}
}

static void bnxt_init_rxbd_pages(struct bnxt_ring_struct *ring, u32 type)
{
	int i;
	u32 prod;
	struct rx_bd **rx_buf_ring;

	rx_buf_ring = (struct rx_bd **)ring->pg_arr;
	for (i = 0, prod = 0; i < ring->nr_pages; i++) {
		int j;
		struct rx_bd *rxbd;

		rxbd = rx_buf_ring[i];
		if (!rxbd)
			continue;

		for (j = 0; j < RX_DESC_CNT; j++, rxbd++, prod++) {
			rxbd->rx_bd_len_flags_type = cpu_to_le32(type);
			rxbd->rx_bd_opaque = prod;
		}
	}
}

static int bnxt_init_one_rx_ring(struct bnxt *bp, int ring_nr)
{
	struct net_device *dev = bp->dev;
	struct bnxt_napi *bnapi = bp->bnapi[ring_nr];
	struct bnxt_rx_ring_info *rxr;
	struct bnxt_ring_struct *ring;
	u32 prod, type;
	int i;

	if (!bnapi)
		return -EINVAL;

	type = (bp->rx_buf_use_size << RX_BD_LEN_SHIFT) |
		RX_BD_TYPE_RX_PACKET_BD | RX_BD_FLAGS_EOP;

	if (NET_IP_ALIGN == 2)
		type |= RX_BD_FLAGS_SOP;

	rxr = &bnapi->rx_ring;
	ring = &rxr->rx_ring_struct;
	bnxt_init_rxbd_pages(ring, type);

	prod = rxr->rx_prod;
	for (i = 0; i < bp->rx_ring_size; i++) {
		if (bnxt_alloc_rx_data(bp, rxr, prod, GFP_KERNEL) != 0) {
			netdev_warn(dev, "init'ed rx ring %d with %d/%d skbs only\n",
				    ring_nr, i, bp->rx_ring_size);
			break;
		}
		prod = NEXT_RX(prod);
	}
	rxr->rx_prod = prod;
	ring->fw_ring_id = INVALID_HW_RING_ID;

	if (!(bp->flags & BNXT_FLAG_AGG_RINGS))
		return 0;

	ring = &rxr->rx_agg_ring_struct;

	type = ((u32)PAGE_SIZE << RX_BD_LEN_SHIFT) |
		RX_BD_TYPE_RX_AGG_BD | RX_BD_FLAGS_SOP;

	bnxt_init_rxbd_pages(ring, type);

	prod = rxr->rx_agg_prod;
	for (i = 0; i < bp->rx_agg_ring_size; i++) {
		if (bnxt_alloc_rx_page(bp, rxr, prod, GFP_KERNEL) != 0) {
			netdev_warn(dev, "init'ed rx ring %d with %d/%d pages only\n",
				    ring_nr, i, bp->rx_ring_size);
			break;
		}
		prod = NEXT_RX_AGG(prod);
	}
	rxr->rx_agg_prod = prod;
	ring->fw_ring_id = INVALID_HW_RING_ID;

	if (bp->flags & BNXT_FLAG_TPA) {
		if (rxr->rx_tpa) {
			u8 *data;
			dma_addr_t mapping;

			for (i = 0; i < MAX_TPA; i++) {
				data = __bnxt_alloc_rx_data(bp, &mapping,
							    GFP_KERNEL);
				if (!data)
					return -ENOMEM;

				rxr->rx_tpa[i].data = data;
				rxr->rx_tpa[i].mapping = mapping;
			}
		} else {
			netdev_err(bp->dev, "No resource allocated for LRO/GRO\n");
			return -ENOMEM;
		}
	}

	return 0;
}

static int bnxt_init_rx_rings(struct bnxt *bp)
{
	int i, rc = 0;

	for (i = 0; i < bp->rx_nr_rings; i++) {
		rc = bnxt_init_one_rx_ring(bp, i);
		if (rc)
			break;
	}

	return rc;
}

static int bnxt_init_tx_rings(struct bnxt *bp)
{
	u16 i;

	bp->tx_wake_thresh = max_t(int, bp->tx_ring_size / 2,
				   MAX_SKB_FRAGS + 1);

	for (i = 0; i < bp->tx_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_tx_ring_info *txr = &bnapi->tx_ring;
		struct bnxt_ring_struct *ring = &txr->tx_ring_struct;

		ring->fw_ring_id = INVALID_HW_RING_ID;
	}

	return 0;
}

static void bnxt_free_ring_grps(struct bnxt *bp)
{
	kfree(bp->grp_info);
	bp->grp_info = NULL;
}

static int bnxt_init_ring_grps(struct bnxt *bp, bool irq_re_init)
{
	int i;

	if (irq_re_init) {
		bp->grp_info = kcalloc(bp->cp_nr_rings,
				       sizeof(struct bnxt_ring_grp_info),
				       GFP_KERNEL);
		if (!bp->grp_info)
			return -ENOMEM;
	}
	for (i = 0; i < bp->cp_nr_rings; i++) {
		if (irq_re_init)
			bp->grp_info[i].fw_stats_ctx = INVALID_HW_RING_ID;
		bp->grp_info[i].fw_grp_id = INVALID_HW_RING_ID;
		bp->grp_info[i].rx_fw_ring_id = INVALID_HW_RING_ID;
		bp->grp_info[i].agg_fw_ring_id = INVALID_HW_RING_ID;
		bp->grp_info[i].cp_fw_ring_id = INVALID_HW_RING_ID;
	}
	return 0;
}

static void bnxt_free_vnics(struct bnxt *bp)
{
	kfree(bp->vnic_info);
	bp->vnic_info = NULL;
	bp->nr_vnics = 0;
}

static int bnxt_alloc_vnics(struct bnxt *bp)
{
	int num_vnics = 1;

#ifdef CONFIG_RFS_ACCEL
	if (bp->flags & BNXT_FLAG_RFS)
		num_vnics += bp->rx_nr_rings;
#endif

	bp->vnic_info = kcalloc(num_vnics, sizeof(struct bnxt_vnic_info),
				GFP_KERNEL);
	if (!bp->vnic_info)
		return -ENOMEM;

	bp->nr_vnics = num_vnics;
	return 0;
}

static void bnxt_init_vnics(struct bnxt *bp)
{
	int i;

	for (i = 0; i < bp->nr_vnics; i++) {
		struct bnxt_vnic_info *vnic = &bp->vnic_info[i];

		vnic->fw_vnic_id = INVALID_HW_RING_ID;
		vnic->fw_rss_cos_lb_ctx = INVALID_HW_RING_ID;
		vnic->fw_l2_ctx_id = INVALID_HW_RING_ID;

		if (bp->vnic_info[i].rss_hash_key) {
			if (i == 0)
				prandom_bytes(vnic->rss_hash_key,
					      HW_HASH_KEY_SIZE);
			else
				memcpy(vnic->rss_hash_key,
				       bp->vnic_info[0].rss_hash_key,
				       HW_HASH_KEY_SIZE);
		}
	}
}

static int bnxt_calc_nr_ring_pages(u32 ring_size, int desc_per_pg)
{
	int pages;

	pages = ring_size / desc_per_pg;

	if (!pages)
		return 1;

	pages++;

	while (pages & (pages - 1))
		pages++;

	return pages;
}

static void bnxt_set_tpa_flags(struct bnxt *bp)
{
	bp->flags &= ~BNXT_FLAG_TPA;
	if (bp->dev->features & NETIF_F_LRO)
		bp->flags |= BNXT_FLAG_LRO;
	if ((bp->dev->features & NETIF_F_GRO) && (bp->pdev->revision > 0))
		bp->flags |= BNXT_FLAG_GRO;
}

/* bp->rx_ring_size, bp->tx_ring_size, dev->mtu, BNXT_FLAG_{G|L}RO flags must
 * be set on entry.
 */
void bnxt_set_ring_params(struct bnxt *bp)
{
	u32 ring_size, rx_size, rx_space;
	u32 agg_factor = 0, agg_ring_size = 0;

	/* 8 for CRC and VLAN */
	rx_size = SKB_DATA_ALIGN(bp->dev->mtu + ETH_HLEN + NET_IP_ALIGN + 8);

	rx_space = rx_size + NET_SKB_PAD +
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	bp->rx_copy_thresh = BNXT_RX_COPY_THRESH;
	ring_size = bp->rx_ring_size;
	bp->rx_agg_ring_size = 0;
	bp->rx_agg_nr_pages = 0;

	if (bp->flags & BNXT_FLAG_TPA)
		agg_factor = 4;

	bp->flags &= ~BNXT_FLAG_JUMBO;
	if (rx_space > PAGE_SIZE) {
		u32 jumbo_factor;

		bp->flags |= BNXT_FLAG_JUMBO;
		jumbo_factor = PAGE_ALIGN(bp->dev->mtu - 40) >> PAGE_SHIFT;
		if (jumbo_factor > agg_factor)
			agg_factor = jumbo_factor;
	}
	agg_ring_size = ring_size * agg_factor;

	if (agg_ring_size) {
		bp->rx_agg_nr_pages = bnxt_calc_nr_ring_pages(agg_ring_size,
							RX_DESC_CNT);
		if (bp->rx_agg_nr_pages > MAX_RX_AGG_PAGES) {
			u32 tmp = agg_ring_size;

			bp->rx_agg_nr_pages = MAX_RX_AGG_PAGES;
			agg_ring_size = MAX_RX_AGG_PAGES * RX_DESC_CNT - 1;
			netdev_warn(bp->dev, "rx agg ring size %d reduced to %d.\n",
				    tmp, agg_ring_size);
		}
		bp->rx_agg_ring_size = agg_ring_size;
		bp->rx_agg_ring_mask = (bp->rx_agg_nr_pages * RX_DESC_CNT) - 1;
		rx_size = SKB_DATA_ALIGN(BNXT_RX_COPY_THRESH + NET_IP_ALIGN);
		rx_space = rx_size + NET_SKB_PAD +
			SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	}

	bp->rx_buf_use_size = rx_size;
	bp->rx_buf_size = rx_space;

	bp->rx_nr_pages = bnxt_calc_nr_ring_pages(ring_size, RX_DESC_CNT);
	bp->rx_ring_mask = (bp->rx_nr_pages * RX_DESC_CNT) - 1;

	ring_size = bp->tx_ring_size;
	bp->tx_nr_pages = bnxt_calc_nr_ring_pages(ring_size, TX_DESC_CNT);
	bp->tx_ring_mask = (bp->tx_nr_pages * TX_DESC_CNT) - 1;

	ring_size = bp->rx_ring_size * (2 + agg_factor) + bp->tx_ring_size;
	bp->cp_ring_size = ring_size;

	bp->cp_nr_pages = bnxt_calc_nr_ring_pages(ring_size, CP_DESC_CNT);
	if (bp->cp_nr_pages > MAX_CP_PAGES) {
		bp->cp_nr_pages = MAX_CP_PAGES;
		bp->cp_ring_size = MAX_CP_PAGES * CP_DESC_CNT - 1;
		netdev_warn(bp->dev, "completion ring size %d reduced to %d.\n",
			    ring_size, bp->cp_ring_size);
	}
	bp->cp_bit = bp->cp_nr_pages * CP_DESC_CNT;
	bp->cp_ring_mask = bp->cp_bit - 1;
}

static void bnxt_free_vnic_attributes(struct bnxt *bp)
{
	int i;
	struct bnxt_vnic_info *vnic;
	struct pci_dev *pdev = bp->pdev;

	if (!bp->vnic_info)
		return;

	for (i = 0; i < bp->nr_vnics; i++) {
		vnic = &bp->vnic_info[i];

		kfree(vnic->fw_grp_ids);
		vnic->fw_grp_ids = NULL;

		kfree(vnic->uc_list);
		vnic->uc_list = NULL;

		if (vnic->mc_list) {
			dma_free_coherent(&pdev->dev, vnic->mc_list_size,
					  vnic->mc_list, vnic->mc_list_mapping);
			vnic->mc_list = NULL;
		}

		if (vnic->rss_table) {
			dma_free_coherent(&pdev->dev, PAGE_SIZE,
					  vnic->rss_table,
					  vnic->rss_table_dma_addr);
			vnic->rss_table = NULL;
		}

		vnic->rss_hash_key = NULL;
		vnic->flags = 0;
	}
}

static int bnxt_alloc_vnic_attributes(struct bnxt *bp)
{
	int i, rc = 0, size;
	struct bnxt_vnic_info *vnic;
	struct pci_dev *pdev = bp->pdev;
	int max_rings;

	for (i = 0; i < bp->nr_vnics; i++) {
		vnic = &bp->vnic_info[i];

		if (vnic->flags & BNXT_VNIC_UCAST_FLAG) {
			int mem_size = (BNXT_MAX_UC_ADDRS - 1) * ETH_ALEN;

			if (mem_size > 0) {
				vnic->uc_list = kmalloc(mem_size, GFP_KERNEL);
				if (!vnic->uc_list) {
					rc = -ENOMEM;
					goto out;
				}
			}
		}

		if (vnic->flags & BNXT_VNIC_MCAST_FLAG) {
			vnic->mc_list_size = BNXT_MAX_MC_ADDRS * ETH_ALEN;
			vnic->mc_list =
				dma_alloc_coherent(&pdev->dev,
						   vnic->mc_list_size,
						   &vnic->mc_list_mapping,
						   GFP_KERNEL);
			if (!vnic->mc_list) {
				rc = -ENOMEM;
				goto out;
			}
		}

		if (vnic->flags & BNXT_VNIC_RSS_FLAG)
			max_rings = bp->rx_nr_rings;
		else
			max_rings = 1;

		vnic->fw_grp_ids = kcalloc(max_rings, sizeof(u16), GFP_KERNEL);
		if (!vnic->fw_grp_ids) {
			rc = -ENOMEM;
			goto out;
		}

		/* Allocate rss table and hash key */
		vnic->rss_table = dma_alloc_coherent(&pdev->dev, PAGE_SIZE,
						     &vnic->rss_table_dma_addr,
						     GFP_KERNEL);
		if (!vnic->rss_table) {
			rc = -ENOMEM;
			goto out;
		}

		size = L1_CACHE_ALIGN(HW_HASH_INDEX_SIZE * sizeof(u16));

		vnic->rss_hash_key = ((void *)vnic->rss_table) + size;
		vnic->rss_hash_key_dma_addr = vnic->rss_table_dma_addr + size;
	}
	return 0;

out:
	return rc;
}

static void bnxt_free_hwrm_resources(struct bnxt *bp)
{
	struct pci_dev *pdev = bp->pdev;

	dma_free_coherent(&pdev->dev, PAGE_SIZE, bp->hwrm_cmd_resp_addr,
			  bp->hwrm_cmd_resp_dma_addr);

	bp->hwrm_cmd_resp_addr = NULL;
	if (bp->hwrm_dbg_resp_addr) {
		dma_free_coherent(&pdev->dev, HWRM_DBG_REG_BUF_SIZE,
				  bp->hwrm_dbg_resp_addr,
				  bp->hwrm_dbg_resp_dma_addr);

		bp->hwrm_dbg_resp_addr = NULL;
	}
}

static int bnxt_alloc_hwrm_resources(struct bnxt *bp)
{
	struct pci_dev *pdev = bp->pdev;

	bp->hwrm_cmd_resp_addr = dma_alloc_coherent(&pdev->dev, PAGE_SIZE,
						   &bp->hwrm_cmd_resp_dma_addr,
						   GFP_KERNEL);
	if (!bp->hwrm_cmd_resp_addr)
		return -ENOMEM;
	bp->hwrm_dbg_resp_addr = dma_alloc_coherent(&pdev->dev,
						    HWRM_DBG_REG_BUF_SIZE,
						    &bp->hwrm_dbg_resp_dma_addr,
						    GFP_KERNEL);
	if (!bp->hwrm_dbg_resp_addr)
		netdev_warn(bp->dev, "fail to alloc debug register dma mem\n");

	return 0;
}

static void bnxt_free_stats(struct bnxt *bp)
{
	u32 size, i;
	struct pci_dev *pdev = bp->pdev;

	if (!bp->bnapi)
		return;

	size = sizeof(struct ctx_hw_stats);

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;

		if (cpr->hw_stats) {
			dma_free_coherent(&pdev->dev, size, cpr->hw_stats,
					  cpr->hw_stats_map);
			cpr->hw_stats = NULL;
		}
	}
}

static int bnxt_alloc_stats(struct bnxt *bp)
{
	u32 size, i;
	struct pci_dev *pdev = bp->pdev;

	size = sizeof(struct ctx_hw_stats);

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;

		cpr->hw_stats = dma_alloc_coherent(&pdev->dev, size,
						   &cpr->hw_stats_map,
						   GFP_KERNEL);
		if (!cpr->hw_stats)
			return -ENOMEM;

		cpr->hw_stats_ctx_id = INVALID_STATS_CTX_ID;
	}
	return 0;
}

static void bnxt_clear_ring_indices(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr;
		struct bnxt_rx_ring_info *rxr;
		struct bnxt_tx_ring_info *txr;

		if (!bnapi)
			continue;

		cpr = &bnapi->cp_ring;
		cpr->cp_raw_cons = 0;

		txr = &bnapi->tx_ring;
		txr->tx_prod = 0;
		txr->tx_cons = 0;

		rxr = &bnapi->rx_ring;
		rxr->rx_prod = 0;
		rxr->rx_agg_prod = 0;
		rxr->rx_sw_agg_prod = 0;
	}
}

static void bnxt_free_ntp_fltrs(struct bnxt *bp, bool irq_reinit)
{
#ifdef CONFIG_RFS_ACCEL
	int i;

	/* Under rtnl_lock and all our NAPIs have been disabled.  It's
	 * safe to delete the hash table.
	 */
	for (i = 0; i < BNXT_NTP_FLTR_HASH_SIZE; i++) {
		struct hlist_head *head;
		struct hlist_node *tmp;
		struct bnxt_ntuple_filter *fltr;

		head = &bp->ntp_fltr_hash_tbl[i];
		hlist_for_each_entry_safe(fltr, tmp, head, hash) {
			hlist_del(&fltr->hash);
			kfree(fltr);
		}
	}
	if (irq_reinit) {
		kfree(bp->ntp_fltr_bmap);
		bp->ntp_fltr_bmap = NULL;
	}
	bp->ntp_fltr_count = 0;
#endif
}

static int bnxt_alloc_ntp_fltrs(struct bnxt *bp)
{
#ifdef CONFIG_RFS_ACCEL
	int i, rc = 0;

	if (!(bp->flags & BNXT_FLAG_RFS))
		return 0;

	for (i = 0; i < BNXT_NTP_FLTR_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&bp->ntp_fltr_hash_tbl[i]);

	bp->ntp_fltr_count = 0;
	bp->ntp_fltr_bmap = kzalloc(BITS_TO_LONGS(BNXT_NTP_FLTR_MAX_FLTR),
				    GFP_KERNEL);

	if (!bp->ntp_fltr_bmap)
		rc = -ENOMEM;

	return rc;
#else
	return 0;
#endif
}

static void bnxt_free_mem(struct bnxt *bp, bool irq_re_init)
{
	bnxt_free_vnic_attributes(bp);
	bnxt_free_tx_rings(bp);
	bnxt_free_rx_rings(bp);
	bnxt_free_cp_rings(bp);
	bnxt_free_ntp_fltrs(bp, irq_re_init);
	if (irq_re_init) {
		bnxt_free_stats(bp);
		bnxt_free_ring_grps(bp);
		bnxt_free_vnics(bp);
		kfree(bp->bnapi);
		bp->bnapi = NULL;
	} else {
		bnxt_clear_ring_indices(bp);
	}
}

static int bnxt_alloc_mem(struct bnxt *bp, bool irq_re_init)
{
	int i, rc, size, arr_size;
	void *bnapi;

	if (irq_re_init) {
		/* Allocate bnapi mem pointer array and mem block for
		 * all queues
		 */
		arr_size = L1_CACHE_ALIGN(sizeof(struct bnxt_napi *) *
				bp->cp_nr_rings);
		size = L1_CACHE_ALIGN(sizeof(struct bnxt_napi));
		bnapi = kzalloc(arr_size + size * bp->cp_nr_rings, GFP_KERNEL);
		if (!bnapi)
			return -ENOMEM;

		bp->bnapi = bnapi;
		bnapi += arr_size;
		for (i = 0; i < bp->cp_nr_rings; i++, bnapi += size) {
			bp->bnapi[i] = bnapi;
			bp->bnapi[i]->index = i;
			bp->bnapi[i]->bp = bp;
		}

		rc = bnxt_alloc_stats(bp);
		if (rc)
			goto alloc_mem_err;

		rc = bnxt_alloc_ntp_fltrs(bp);
		if (rc)
			goto alloc_mem_err;

		rc = bnxt_alloc_vnics(bp);
		if (rc)
			goto alloc_mem_err;
	}

	bnxt_init_ring_struct(bp);

	rc = bnxt_alloc_rx_rings(bp);
	if (rc)
		goto alloc_mem_err;

	rc = bnxt_alloc_tx_rings(bp);
	if (rc)
		goto alloc_mem_err;

	rc = bnxt_alloc_cp_rings(bp);
	if (rc)
		goto alloc_mem_err;

	bp->vnic_info[0].flags |= BNXT_VNIC_RSS_FLAG | BNXT_VNIC_MCAST_FLAG |
				  BNXT_VNIC_UCAST_FLAG;
	rc = bnxt_alloc_vnic_attributes(bp);
	if (rc)
		goto alloc_mem_err;
	return 0;

alloc_mem_err:
	bnxt_free_mem(bp, true);
	return rc;
}

void bnxt_hwrm_cmd_hdr_init(struct bnxt *bp, void *request, u16 req_type,
			    u16 cmpl_ring, u16 target_id)
{
	struct hwrm_cmd_req_hdr *req = request;

	req->cmpl_ring_req_type =
		cpu_to_le32(req_type | (cmpl_ring << HWRM_CMPL_RING_SFT));
	req->target_id_seq_id = cpu_to_le32(target_id << HWRM_TARGET_FID_SFT);
	req->resp_addr = cpu_to_le64(bp->hwrm_cmd_resp_dma_addr);
}

int _hwrm_send_message(struct bnxt *bp, void *msg, u32 msg_len, int timeout)
{
	int i, intr_process, rc;
	struct hwrm_cmd_req_hdr *req = msg;
	u32 *data = msg;
	__le32 *resp_len, *valid;
	u16 cp_ring_id, len = 0;
	struct hwrm_err_output *resp = bp->hwrm_cmd_resp_addr;

	req->target_id_seq_id |= cpu_to_le32(bp->hwrm_cmd_seq++);
	memset(resp, 0, PAGE_SIZE);
	cp_ring_id = (le32_to_cpu(req->cmpl_ring_req_type) &
		      HWRM_CMPL_RING_MASK) >>
		     HWRM_CMPL_RING_SFT;
	intr_process = (cp_ring_id == INVALID_HW_RING_ID) ? 0 : 1;

	/* Write request msg to hwrm channel */
	__iowrite32_copy(bp->bar0, data, msg_len / 4);

	/* currently supports only one outstanding message */
	if (intr_process)
		bp->hwrm_intr_seq_id = le32_to_cpu(req->target_id_seq_id) &
				       HWRM_SEQ_ID_MASK;

	/* Ring channel doorbell */
	writel(1, bp->bar0 + 0x100);

	i = 0;
	if (intr_process) {
		/* Wait until hwrm response cmpl interrupt is processed */
		while (bp->hwrm_intr_seq_id != HWRM_SEQ_ID_INVALID &&
		       i++ < timeout) {
			usleep_range(600, 800);
		}

		if (bp->hwrm_intr_seq_id != HWRM_SEQ_ID_INVALID) {
			netdev_err(bp->dev, "Resp cmpl intr err msg: 0x%x\n",
				   req->cmpl_ring_req_type);
			return -1;
		}
	} else {
		/* Check if response len is updated */
		resp_len = bp->hwrm_cmd_resp_addr + HWRM_RESP_LEN_OFFSET;
		for (i = 0; i < timeout; i++) {
			len = (le32_to_cpu(*resp_len) & HWRM_RESP_LEN_MASK) >>
			      HWRM_RESP_LEN_SFT;
			if (len)
				break;
			usleep_range(600, 800);
		}

		if (i >= timeout) {
			netdev_err(bp->dev, "Error (timeout: %d) msg {0x%x 0x%x} len:%d\n",
				   timeout, req->cmpl_ring_req_type,
				   req->target_id_seq_id, *resp_len);
			return -1;
		}

		/* Last word of resp contains valid bit */
		valid = bp->hwrm_cmd_resp_addr + len - 4;
		for (i = 0; i < timeout; i++) {
			if (le32_to_cpu(*valid) & HWRM_RESP_VALID_MASK)
				break;
			usleep_range(600, 800);
		}

		if (i >= timeout) {
			netdev_err(bp->dev, "Error (timeout: %d) msg {0x%x 0x%x} len:%d v:%d\n",
				   timeout, req->cmpl_ring_req_type,
				   req->target_id_seq_id, len, *valid);
			return -1;
		}
	}

	rc = le16_to_cpu(resp->error_code);
	if (rc) {
		netdev_err(bp->dev, "hwrm req_type 0x%x seq id 0x%x error 0x%x\n",
			   le16_to_cpu(resp->req_type),
			   le16_to_cpu(resp->seq_id), rc);
		return rc;
	}
	return 0;
}

int hwrm_send_message(struct bnxt *bp, void *msg, u32 msg_len, int timeout)
{
	int rc;

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, msg, msg_len, timeout);
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_func_drv_rgtr(struct bnxt *bp)
{
	struct hwrm_func_drv_rgtr_input req = {0};
	int i;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_DRV_RGTR, -1, -1);

	req.enables =
		cpu_to_le32(FUNC_DRV_RGTR_REQ_ENABLES_OS_TYPE |
			    FUNC_DRV_RGTR_REQ_ENABLES_VER |
			    FUNC_DRV_RGTR_REQ_ENABLES_ASYNC_EVENT_FWD);

	/* TODO: current async event fwd bits are not defined and the firmware
	 * only checks if it is non-zero to enable async event forwarding
	 */
	req.async_event_fwd[0] |= cpu_to_le32(1);
	req.os_type = cpu_to_le16(1);
	req.ver_maj = DRV_VER_MAJ;
	req.ver_min = DRV_VER_MIN;
	req.ver_upd = DRV_VER_UPD;

	if (BNXT_PF(bp)) {
		unsigned long vf_req_snif_bmap[4];
		u32 *data = (u32 *)vf_req_snif_bmap;

		memset(vf_req_snif_bmap, 0, 32);
		for (i = 0; i < ARRAY_SIZE(bnxt_vf_req_snif); i++)
			__set_bit(bnxt_vf_req_snif[i], vf_req_snif_bmap);

		for (i = 0; i < 8; i++) {
			req.vf_req_fwd[i] = cpu_to_le32(*data);
			data++;
		}
		req.enables |=
			cpu_to_le32(FUNC_DRV_RGTR_REQ_ENABLES_VF_REQ_FWD);
	}

	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_hwrm_tunnel_dst_port_free(struct bnxt *bp, u8 tunnel_type)
{
	u32 rc = 0;
	struct hwrm_tunnel_dst_port_free_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_TUNNEL_DST_PORT_FREE, -1, -1);
	req.tunnel_type = tunnel_type;

	switch (tunnel_type) {
	case TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN:
		req.tunnel_dst_port_id = bp->vxlan_fw_dst_port_id;
		break;
	case TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_GENEVE:
		req.tunnel_dst_port_id = bp->nge_fw_dst_port_id;
		break;
	default:
		break;
	}

	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		netdev_err(bp->dev, "hwrm_tunnel_dst_port_free failed. rc:%d\n",
			   rc);
	return rc;
}

static int bnxt_hwrm_tunnel_dst_port_alloc(struct bnxt *bp, __be16 port,
					   u8 tunnel_type)
{
	u32 rc = 0;
	struct hwrm_tunnel_dst_port_alloc_input req = {0};
	struct hwrm_tunnel_dst_port_alloc_output *resp = bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_TUNNEL_DST_PORT_ALLOC, -1, -1);

	req.tunnel_type = tunnel_type;
	req.tunnel_dst_port_val = port;

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc) {
		netdev_err(bp->dev, "hwrm_tunnel_dst_port_alloc failed. rc:%d\n",
			   rc);
		goto err_out;
	}

	if (tunnel_type & TUNNEL_DST_PORT_ALLOC_REQ_TUNNEL_TYPE_VXLAN)
		bp->vxlan_fw_dst_port_id = resp->tunnel_dst_port_id;

	else if (tunnel_type & TUNNEL_DST_PORT_ALLOC_REQ_TUNNEL_TYPE_GENEVE)
		bp->nge_fw_dst_port_id = resp->tunnel_dst_port_id;
err_out:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_cfa_l2_set_rx_mask(struct bnxt *bp, u16 vnic_id)
{
	struct hwrm_cfa_l2_set_rx_mask_input req = {0};
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_L2_SET_RX_MASK, -1, -1);
	req.dflt_vnic_id = cpu_to_le32(vnic->fw_vnic_id);

	req.num_mc_entries = cpu_to_le32(vnic->mc_list_count);
	req.mc_tbl_addr = cpu_to_le64(vnic->mc_list_mapping);
	req.mask = cpu_to_le32(vnic->rx_mask);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

#ifdef CONFIG_RFS_ACCEL
static int bnxt_hwrm_cfa_ntuple_filter_free(struct bnxt *bp,
					    struct bnxt_ntuple_filter *fltr)
{
	struct hwrm_cfa_ntuple_filter_free_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_NTUPLE_FILTER_FREE, -1, -1);
	req.ntuple_filter_id = fltr->filter_id;
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

#define BNXT_NTP_FLTR_FLAGS					\
	(CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_L2_FILTER_ID |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_ETHERTYPE |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_MACADDR |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_IPADDR_TYPE |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_IPADDR |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_IPADDR_MASK |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_IPADDR |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_IPADDR_MASK |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_IP_PROTOCOL |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_PORT |		\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_PORT_MASK |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_PORT |		\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_PORT_MASK |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_VNIC_ID)

static int bnxt_hwrm_cfa_ntuple_filter_alloc(struct bnxt *bp,
					     struct bnxt_ntuple_filter *fltr)
{
	int rc = 0;
	struct hwrm_cfa_ntuple_filter_alloc_input req = {0};
	struct hwrm_cfa_ntuple_filter_alloc_output *resp =
		bp->hwrm_cmd_resp_addr;
	struct flow_keys *keys = &fltr->fkeys;
	struct bnxt_vnic_info *vnic = &bp->vnic_info[fltr->rxq + 1];

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_NTUPLE_FILTER_ALLOC, -1, -1);
	req.l2_filter_id = bp->vnic_info[0].fw_l2_filter_id[0];

	req.enables = cpu_to_le32(BNXT_NTP_FLTR_FLAGS);

	req.ethertype = htons(ETH_P_IP);
	memcpy(req.src_macaddr, fltr->src_mac_addr, ETH_ALEN);
	req.ipaddr_type = 4;
	req.ip_protocol = keys->basic.ip_proto;

	req.src_ipaddr[0] = keys->addrs.v4addrs.src;
	req.src_ipaddr_mask[0] = cpu_to_be32(0xffffffff);
	req.dst_ipaddr[0] = keys->addrs.v4addrs.dst;
	req.dst_ipaddr_mask[0] = cpu_to_be32(0xffffffff);

	req.src_port = keys->ports.src;
	req.src_port_mask = cpu_to_be16(0xffff);
	req.dst_port = keys->ports.dst;
	req.dst_port_mask = cpu_to_be16(0xffff);

	req.dst_vnic_id = cpu_to_le16(vnic->fw_vnic_id);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		fltr->filter_id = resp->ntuple_filter_id;
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}
#endif

static int bnxt_hwrm_set_vnic_filter(struct bnxt *bp, u16 vnic_id, u16 idx,
				     u8 *mac_addr)
{
	u32 rc = 0;
	struct hwrm_cfa_l2_filter_alloc_input req = {0};
	struct hwrm_cfa_l2_filter_alloc_output *resp = bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_L2_FILTER_ALLOC, -1, -1);
	req.flags = cpu_to_le32(CFA_L2_FILTER_ALLOC_REQ_FLAGS_PATH_RX |
				CFA_L2_FILTER_ALLOC_REQ_FLAGS_OUTERMOST);
	req.dst_vnic_id = cpu_to_le16(bp->vnic_info[vnic_id].fw_vnic_id);
	req.enables =
		cpu_to_le32(CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_ADDR |
			    CFA_L2_FILTER_ALLOC_REQ_ENABLES_DST_VNIC_ID |
			    CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_ADDR_MASK);
	memcpy(req.l2_addr, mac_addr, ETH_ALEN);
	req.l2_addr_mask[0] = 0xff;
	req.l2_addr_mask[1] = 0xff;
	req.l2_addr_mask[2] = 0xff;
	req.l2_addr_mask[3] = 0xff;
	req.l2_addr_mask[4] = 0xff;
	req.l2_addr_mask[5] = 0xff;

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		bp->vnic_info[vnic_id].fw_l2_filter_id[idx] =
							resp->l2_filter_id;
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_clear_vnic_filter(struct bnxt *bp)
{
	u16 i, j, num_of_vnics = 1; /* only vnic 0 supported */
	int rc = 0;

	/* Any associated ntuple filters will also be cleared by firmware. */
	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 0; i < num_of_vnics; i++) {
		struct bnxt_vnic_info *vnic = &bp->vnic_info[i];

		for (j = 0; j < vnic->uc_filter_count; j++) {
			struct hwrm_cfa_l2_filter_free_input req = {0};

			bnxt_hwrm_cmd_hdr_init(bp, &req,
					       HWRM_CFA_L2_FILTER_FREE, -1, -1);

			req.l2_filter_id = vnic->fw_l2_filter_id[j];

			rc = _hwrm_send_message(bp, &req, sizeof(req),
						HWRM_CMD_TIMEOUT);
		}
		vnic->uc_filter_count = 0;
	}
	mutex_unlock(&bp->hwrm_cmd_lock);

	return rc;
}

static int bnxt_hwrm_vnic_set_tpa(struct bnxt *bp, u16 vnic_id, u32 tpa_flags)
{
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];
	struct hwrm_vnic_tpa_cfg_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_TPA_CFG, -1, -1);

	if (tpa_flags) {
		u16 mss = bp->dev->mtu - 40;
		u32 nsegs, n, segs = 0, flags;

		flags = VNIC_TPA_CFG_REQ_FLAGS_TPA |
			VNIC_TPA_CFG_REQ_FLAGS_ENCAP_TPA |
			VNIC_TPA_CFG_REQ_FLAGS_RSC_WND_UPDATE |
			VNIC_TPA_CFG_REQ_FLAGS_AGG_WITH_ECN |
			VNIC_TPA_CFG_REQ_FLAGS_AGG_WITH_SAME_GRE_SEQ;
		if (tpa_flags & BNXT_FLAG_GRO)
			flags |= VNIC_TPA_CFG_REQ_FLAGS_GRO;

		req.flags = cpu_to_le32(flags);

		req.enables =
			cpu_to_le32(VNIC_TPA_CFG_REQ_ENABLES_MAX_AGG_SEGS |
				    VNIC_TPA_CFG_REQ_ENABLES_MAX_AGGS);

		/* Number of segs are log2 units, and first packet is not
		 * included as part of this units.
		 */
		if (mss <= PAGE_SIZE) {
			n = PAGE_SIZE / mss;
			nsegs = (MAX_SKB_FRAGS - 1) * n;
		} else {
			n = mss / PAGE_SIZE;
			if (mss & (PAGE_SIZE - 1))
				n++;
			nsegs = (MAX_SKB_FRAGS - n) / n;
		}

		segs = ilog2(nsegs);
		req.max_agg_segs = cpu_to_le16(segs);
		req.max_aggs = cpu_to_le16(VNIC_TPA_CFG_REQ_MAX_AGGS_MAX);
	}
	req.vnic_id = cpu_to_le16(vnic->fw_vnic_id);

	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_hwrm_vnic_set_rss(struct bnxt *bp, u16 vnic_id, bool set_rss)
{
	u32 i, j, max_rings;
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];
	struct hwrm_vnic_rss_cfg_input req = {0};

	if (vnic->fw_rss_cos_lb_ctx == INVALID_HW_RING_ID)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_RSS_CFG, -1, -1);
	if (set_rss) {
		vnic->hash_type = BNXT_RSS_HASH_TYPE_FLAG_IPV4 |
				 BNXT_RSS_HASH_TYPE_FLAG_TCP_IPV4 |
				 BNXT_RSS_HASH_TYPE_FLAG_IPV6 |
				 BNXT_RSS_HASH_TYPE_FLAG_TCP_IPV6;

		req.hash_type = cpu_to_le32(vnic->hash_type);

		if (vnic->flags & BNXT_VNIC_RSS_FLAG)
			max_rings = bp->rx_nr_rings;
		else
			max_rings = 1;

		/* Fill the RSS indirection table with ring group ids */
		for (i = 0, j = 0; i < HW_HASH_INDEX_SIZE; i++, j++) {
			if (j == max_rings)
				j = 0;
			vnic->rss_table[i] = cpu_to_le16(vnic->fw_grp_ids[j]);
		}

		req.ring_grp_tbl_addr = cpu_to_le64(vnic->rss_table_dma_addr);
		req.hash_key_tbl_addr =
			cpu_to_le64(vnic->rss_hash_key_dma_addr);
	}
	req.rss_ctx_idx = cpu_to_le16(vnic->fw_rss_cos_lb_ctx);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_hwrm_vnic_set_hds(struct bnxt *bp, u16 vnic_id)
{
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];
	struct hwrm_vnic_plcmodes_cfg_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_PLCMODES_CFG, -1, -1);
	req.flags = cpu_to_le32(VNIC_PLCMODES_CFG_REQ_FLAGS_JUMBO_PLACEMENT |
				VNIC_PLCMODES_CFG_REQ_FLAGS_HDS_IPV4 |
				VNIC_PLCMODES_CFG_REQ_FLAGS_HDS_IPV6);
	req.enables =
		cpu_to_le32(VNIC_PLCMODES_CFG_REQ_ENABLES_JUMBO_THRESH_VALID |
			    VNIC_PLCMODES_CFG_REQ_ENABLES_HDS_THRESHOLD_VALID);
	/* thresholds not implemented in firmware yet */
	req.jumbo_thresh = cpu_to_le16(bp->rx_copy_thresh);
	req.hds_threshold = cpu_to_le16(bp->rx_copy_thresh);
	req.vnic_id = cpu_to_le32(vnic->fw_vnic_id);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static void bnxt_hwrm_vnic_ctx_free_one(struct bnxt *bp, u16 vnic_id)
{
	struct hwrm_vnic_rss_cos_lb_ctx_free_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_RSS_COS_LB_CTX_FREE, -1, -1);
	req.rss_cos_lb_ctx_id =
		cpu_to_le16(bp->vnic_info[vnic_id].fw_rss_cos_lb_ctx);

	hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	bp->vnic_info[vnic_id].fw_rss_cos_lb_ctx = INVALID_HW_RING_ID;
}

static void bnxt_hwrm_vnic_ctx_free(struct bnxt *bp)
{
	int i;

	for (i = 0; i < bp->nr_vnics; i++) {
		struct bnxt_vnic_info *vnic = &bp->vnic_info[i];

		if (vnic->fw_rss_cos_lb_ctx != INVALID_HW_RING_ID)
			bnxt_hwrm_vnic_ctx_free_one(bp, i);
	}
	bp->rsscos_nr_ctxs = 0;
}

static int bnxt_hwrm_vnic_ctx_alloc(struct bnxt *bp, u16 vnic_id)
{
	int rc;
	struct hwrm_vnic_rss_cos_lb_ctx_alloc_input req = {0};
	struct hwrm_vnic_rss_cos_lb_ctx_alloc_output *resp =
						bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_RSS_COS_LB_CTX_ALLOC, -1,
			       -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		bp->vnic_info[vnic_id].fw_rss_cos_lb_ctx =
			le16_to_cpu(resp->rss_cos_lb_ctx_id);
	mutex_unlock(&bp->hwrm_cmd_lock);

	return rc;
}

static int bnxt_hwrm_vnic_cfg(struct bnxt *bp, u16 vnic_id)
{
	int grp_idx = 0;
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];
	struct hwrm_vnic_cfg_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_CFG, -1, -1);
	/* Only RSS support for now TBD: COS & LB */
	req.enables = cpu_to_le32(VNIC_CFG_REQ_ENABLES_DFLT_RING_GRP |
				  VNIC_CFG_REQ_ENABLES_RSS_RULE);
	req.rss_rule = cpu_to_le16(vnic->fw_rss_cos_lb_ctx);
	req.cos_rule = cpu_to_le16(0xffff);
	if (vnic->flags & BNXT_VNIC_RSS_FLAG)
		grp_idx = 0;
	else if (vnic->flags & BNXT_VNIC_RFS_FLAG)
		grp_idx = vnic_id - 1;

	req.vnic_id = cpu_to_le16(vnic->fw_vnic_id);
	req.dflt_ring_grp = cpu_to_le16(bp->grp_info[grp_idx].fw_grp_id);

	req.lb_rule = cpu_to_le16(0xffff);
	req.mru = cpu_to_le16(bp->dev->mtu + ETH_HLEN + ETH_FCS_LEN +
			      VLAN_HLEN);

	if (bp->flags & BNXT_FLAG_STRIP_VLAN)
		req.flags |= cpu_to_le32(VNIC_CFG_REQ_FLAGS_VLAN_STRIP_MODE);

	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_hwrm_vnic_free_one(struct bnxt *bp, u16 vnic_id)
{
	u32 rc = 0;

	if (bp->vnic_info[vnic_id].fw_vnic_id != INVALID_HW_RING_ID) {
		struct hwrm_vnic_free_input req = {0};

		bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_FREE, -1, -1);
		req.vnic_id =
			cpu_to_le32(bp->vnic_info[vnic_id].fw_vnic_id);

		rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
		if (rc)
			return rc;
		bp->vnic_info[vnic_id].fw_vnic_id = INVALID_HW_RING_ID;
	}
	return rc;
}

static void bnxt_hwrm_vnic_free(struct bnxt *bp)
{
	u16 i;

	for (i = 0; i < bp->nr_vnics; i++)
		bnxt_hwrm_vnic_free_one(bp, i);
}

static int bnxt_hwrm_vnic_alloc(struct bnxt *bp, u16 vnic_id, u16 start_grp_id,
				u16 end_grp_id)
{
	u32 rc = 0, i, j;
	struct hwrm_vnic_alloc_input req = {0};
	struct hwrm_vnic_alloc_output *resp = bp->hwrm_cmd_resp_addr;

	/* map ring groups to this vnic */
	for (i = start_grp_id, j = 0; i < end_grp_id; i++, j++) {
		if (bp->grp_info[i].fw_grp_id == INVALID_HW_RING_ID) {
			netdev_err(bp->dev, "Not enough ring groups avail:%x req:%x\n",
				   j, (end_grp_id - start_grp_id));
			break;
		}
		bp->vnic_info[vnic_id].fw_grp_ids[j] =
					bp->grp_info[i].fw_grp_id;
	}

	bp->vnic_info[vnic_id].fw_rss_cos_lb_ctx = INVALID_HW_RING_ID;
	if (vnic_id == 0)
		req.flags = cpu_to_le32(VNIC_ALLOC_REQ_FLAGS_DEFAULT);

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_ALLOC, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		bp->vnic_info[vnic_id].fw_vnic_id = le32_to_cpu(resp->vnic_id);
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_ring_grp_alloc(struct bnxt *bp)
{
	u16 i;
	u32 rc = 0;

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct hwrm_ring_grp_alloc_input req = {0};
		struct hwrm_ring_grp_alloc_output *resp =
					bp->hwrm_cmd_resp_addr;

		bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_RING_GRP_ALLOC, -1, -1);

		req.cr = cpu_to_le16(bp->grp_info[i].cp_fw_ring_id);
		req.rr = cpu_to_le16(bp->grp_info[i].rx_fw_ring_id);
		req.ar = cpu_to_le16(bp->grp_info[i].agg_fw_ring_id);
		req.sc = cpu_to_le16(bp->grp_info[i].fw_stats_ctx);

		rc = _hwrm_send_message(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
		if (rc)
			break;

		bp->grp_info[i].fw_grp_id = le32_to_cpu(resp->ring_group_id);
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_ring_grp_free(struct bnxt *bp)
{
	u16 i;
	u32 rc = 0;
	struct hwrm_ring_grp_free_input req = {0};

	if (!bp->grp_info)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_RING_GRP_FREE, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		if (bp->grp_info[i].fw_grp_id == INVALID_HW_RING_ID)
			continue;
		req.ring_group_id =
			cpu_to_le32(bp->grp_info[i].fw_grp_id);

		rc = _hwrm_send_message(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
		if (rc)
			break;
		bp->grp_info[i].fw_grp_id = INVALID_HW_RING_ID;
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int hwrm_ring_alloc_send_msg(struct bnxt *bp,
				    struct bnxt_ring_struct *ring,
				    u32 ring_type, u32 map_index,
				    u32 stats_ctx_id)
{
	int rc = 0, err = 0;
	struct hwrm_ring_alloc_input req = {0};
	struct hwrm_ring_alloc_output *resp = bp->hwrm_cmd_resp_addr;
	u16 ring_id;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_RING_ALLOC, -1, -1);

	req.enables = 0;
	if (ring->nr_pages > 1) {
		req.page_tbl_addr = cpu_to_le64(ring->pg_tbl_map);
		/* Page size is in log2 units */
		req.page_size = BNXT_PAGE_SHIFT;
		req.page_tbl_depth = 1;
	} else {
		req.page_tbl_addr =  cpu_to_le64(ring->dma_arr[0]);
	}
	req.fbo = 0;
	/* Association of ring index with doorbell index and MSIX number */
	req.logical_id = cpu_to_le16(map_index);

	switch (ring_type) {
	case HWRM_RING_ALLOC_TX:
		req.ring_type = RING_ALLOC_REQ_RING_TYPE_TX;
		/* Association of transmit ring with completion ring */
		req.cmpl_ring_id =
			cpu_to_le16(bp->grp_info[map_index].cp_fw_ring_id);
		req.length = cpu_to_le32(bp->tx_ring_mask + 1);
		req.stat_ctx_id = cpu_to_le32(stats_ctx_id);
		req.queue_id = cpu_to_le16(ring->queue_id);
		break;
	case HWRM_RING_ALLOC_RX:
		req.ring_type = RING_ALLOC_REQ_RING_TYPE_RX;
		req.length = cpu_to_le32(bp->rx_ring_mask + 1);
		break;
	case HWRM_RING_ALLOC_AGG:
		req.ring_type = RING_ALLOC_REQ_RING_TYPE_RX;
		req.length = cpu_to_le32(bp->rx_agg_ring_mask + 1);
		break;
	case HWRM_RING_ALLOC_CMPL:
		req.ring_type = RING_ALLOC_REQ_RING_TYPE_CMPL;
		req.length = cpu_to_le32(bp->cp_ring_mask + 1);
		if (bp->flags & BNXT_FLAG_USING_MSIX)
			req.int_mode = RING_ALLOC_REQ_INT_MODE_MSIX;
		break;
	default:
		netdev_err(bp->dev, "hwrm alloc invalid ring type %d\n",
			   ring_type);
		return -1;
	}

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	err = le16_to_cpu(resp->error_code);
	ring_id = le16_to_cpu(resp->ring_id);
	mutex_unlock(&bp->hwrm_cmd_lock);

	if (rc || err) {
		switch (ring_type) {
		case RING_FREE_REQ_RING_TYPE_CMPL:
			netdev_err(bp->dev, "hwrm_ring_alloc cp failed. rc:%x err:%x\n",
				   rc, err);
			return -1;

		case RING_FREE_REQ_RING_TYPE_RX:
			netdev_err(bp->dev, "hwrm_ring_alloc rx failed. rc:%x err:%x\n",
				   rc, err);
			return -1;

		case RING_FREE_REQ_RING_TYPE_TX:
			netdev_err(bp->dev, "hwrm_ring_alloc tx failed. rc:%x err:%x\n",
				   rc, err);
			return -1;

		default:
			netdev_err(bp->dev, "Invalid ring\n");
			return -1;
		}
	}
	ring->fw_ring_id = ring_id;
	return rc;
}

static int bnxt_hwrm_ring_alloc(struct bnxt *bp)
{
	int i, rc = 0;

	if (bp->cp_nr_rings) {
		for (i = 0; i < bp->cp_nr_rings; i++) {
			struct bnxt_napi *bnapi = bp->bnapi[i];
			struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
			struct bnxt_ring_struct *ring = &cpr->cp_ring_struct;

			rc = hwrm_ring_alloc_send_msg(bp, ring,
						      HWRM_RING_ALLOC_CMPL, i,
						      INVALID_STATS_CTX_ID);
			if (rc)
				goto err_out;
			cpr->cp_doorbell = bp->bar1 + i * 0x80;
			BNXT_CP_DB(cpr->cp_doorbell, cpr->cp_raw_cons);
			bp->grp_info[i].cp_fw_ring_id = ring->fw_ring_id;
		}
	}

	if (bp->tx_nr_rings) {
		for (i = 0; i < bp->tx_nr_rings; i++) {
			struct bnxt_napi *bnapi = bp->bnapi[i];
			struct bnxt_tx_ring_info *txr = &bnapi->tx_ring;
			struct bnxt_ring_struct *ring = &txr->tx_ring_struct;
			u16 fw_stats_ctx = bp->grp_info[i].fw_stats_ctx;

			rc = hwrm_ring_alloc_send_msg(bp, ring,
						      HWRM_RING_ALLOC_TX, i,
						      fw_stats_ctx);
			if (rc)
				goto err_out;
			txr->tx_doorbell = bp->bar1 + i * 0x80;
		}
	}

	if (bp->rx_nr_rings) {
		for (i = 0; i < bp->rx_nr_rings; i++) {
			struct bnxt_napi *bnapi = bp->bnapi[i];
			struct bnxt_rx_ring_info *rxr = &bnapi->rx_ring;
			struct bnxt_ring_struct *ring = &rxr->rx_ring_struct;

			rc = hwrm_ring_alloc_send_msg(bp, ring,
						      HWRM_RING_ALLOC_RX, i,
						      INVALID_STATS_CTX_ID);
			if (rc)
				goto err_out;
			rxr->rx_doorbell = bp->bar1 + i * 0x80;
			writel(DB_KEY_RX | rxr->rx_prod, rxr->rx_doorbell);
			bp->grp_info[i].rx_fw_ring_id = ring->fw_ring_id;
		}
	}

	if (bp->flags & BNXT_FLAG_AGG_RINGS) {
		for (i = 0; i < bp->rx_nr_rings; i++) {
			struct bnxt_napi *bnapi = bp->bnapi[i];
			struct bnxt_rx_ring_info *rxr = &bnapi->rx_ring;
			struct bnxt_ring_struct *ring =
						&rxr->rx_agg_ring_struct;

			rc = hwrm_ring_alloc_send_msg(bp, ring,
						      HWRM_RING_ALLOC_AGG,
						      bp->rx_nr_rings + i,
						      INVALID_STATS_CTX_ID);
			if (rc)
				goto err_out;

			rxr->rx_agg_doorbell =
				bp->bar1 + (bp->rx_nr_rings + i) * 0x80;
			writel(DB_KEY_RX | rxr->rx_agg_prod,
			       rxr->rx_agg_doorbell);
			bp->grp_info[i].agg_fw_ring_id = ring->fw_ring_id;
		}
	}
err_out:
	return rc;
}

static int hwrm_ring_free_send_msg(struct bnxt *bp,
				   struct bnxt_ring_struct *ring,
				   u32 ring_type, int cmpl_ring_id)
{
	int rc;
	struct hwrm_ring_free_input req = {0};
	struct hwrm_ring_free_output *resp = bp->hwrm_cmd_resp_addr;
	u16 error_code;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_RING_FREE, -1, -1);
	req.ring_type = ring_type;
	req.ring_id = cpu_to_le16(ring->fw_ring_id);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	error_code = le16_to_cpu(resp->error_code);
	mutex_unlock(&bp->hwrm_cmd_lock);

	if (rc || error_code) {
		switch (ring_type) {
		case RING_FREE_REQ_RING_TYPE_CMPL:
			netdev_err(bp->dev, "hwrm_ring_free cp failed. rc:%d\n",
				   rc);
			return rc;
		case RING_FREE_REQ_RING_TYPE_RX:
			netdev_err(bp->dev, "hwrm_ring_free rx failed. rc:%d\n",
				   rc);
			return rc;
		case RING_FREE_REQ_RING_TYPE_TX:
			netdev_err(bp->dev, "hwrm_ring_free tx failed. rc:%d\n",
				   rc);
			return rc;
		default:
			netdev_err(bp->dev, "Invalid ring\n");
			return -1;
		}
	}
	return 0;
}

static int bnxt_hwrm_ring_free(struct bnxt *bp, bool close_path)
{
	int i, rc = 0;

	if (!bp->bnapi)
		return 0;

	if (bp->tx_nr_rings) {
		for (i = 0; i < bp->tx_nr_rings; i++) {
			struct bnxt_napi *bnapi = bp->bnapi[i];
			struct bnxt_tx_ring_info *txr = &bnapi->tx_ring;
			struct bnxt_ring_struct *ring = &txr->tx_ring_struct;
			u32 cmpl_ring_id = bp->grp_info[i].cp_fw_ring_id;

			if (ring->fw_ring_id != INVALID_HW_RING_ID) {
				hwrm_ring_free_send_msg(
					bp, ring,
					RING_FREE_REQ_RING_TYPE_TX,
					close_path ? cmpl_ring_id :
					INVALID_HW_RING_ID);
				ring->fw_ring_id = INVALID_HW_RING_ID;
			}
		}
	}

	if (bp->rx_nr_rings) {
		for (i = 0; i < bp->rx_nr_rings; i++) {
			struct bnxt_napi *bnapi = bp->bnapi[i];
			struct bnxt_rx_ring_info *rxr = &bnapi->rx_ring;
			struct bnxt_ring_struct *ring = &rxr->rx_ring_struct;
			u32 cmpl_ring_id = bp->grp_info[i].cp_fw_ring_id;

			if (ring->fw_ring_id != INVALID_HW_RING_ID) {
				hwrm_ring_free_send_msg(
					bp, ring,
					RING_FREE_REQ_RING_TYPE_RX,
					close_path ? cmpl_ring_id :
					INVALID_HW_RING_ID);
				ring->fw_ring_id = INVALID_HW_RING_ID;
				bp->grp_info[i].rx_fw_ring_id =
					INVALID_HW_RING_ID;
			}
		}
	}

	if (bp->rx_agg_nr_pages) {
		for (i = 0; i < bp->rx_nr_rings; i++) {
			struct bnxt_napi *bnapi = bp->bnapi[i];
			struct bnxt_rx_ring_info *rxr = &bnapi->rx_ring;
			struct bnxt_ring_struct *ring =
						&rxr->rx_agg_ring_struct;
			u32 cmpl_ring_id = bp->grp_info[i].cp_fw_ring_id;

			if (ring->fw_ring_id != INVALID_HW_RING_ID) {
				hwrm_ring_free_send_msg(
					bp, ring,
					RING_FREE_REQ_RING_TYPE_RX,
					close_path ? cmpl_ring_id :
					INVALID_HW_RING_ID);
				ring->fw_ring_id = INVALID_HW_RING_ID;
				bp->grp_info[i].agg_fw_ring_id =
					INVALID_HW_RING_ID;
			}
		}
	}

	if (bp->cp_nr_rings) {
		for (i = 0; i < bp->cp_nr_rings; i++) {
			struct bnxt_napi *bnapi = bp->bnapi[i];
			struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
			struct bnxt_ring_struct *ring = &cpr->cp_ring_struct;

			if (ring->fw_ring_id != INVALID_HW_RING_ID) {
				hwrm_ring_free_send_msg(
					bp, ring,
					RING_FREE_REQ_RING_TYPE_CMPL,
					INVALID_HW_RING_ID);
				ring->fw_ring_id = INVALID_HW_RING_ID;
				bp->grp_info[i].cp_fw_ring_id =
							INVALID_HW_RING_ID;
			}
		}
	}

	return rc;
}

int bnxt_hwrm_set_coal(struct bnxt *bp)
{
	int i, rc = 0;
	struct hwrm_ring_cmpl_ring_cfg_aggint_params_input req = {0};
	u16 max_buf, max_buf_irq;
	u16 buf_tmr, buf_tmr_irq;
	u32 flags;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_RING_CMPL_RING_CFG_AGGINT_PARAMS,
			       -1, -1);

	/* Each rx completion (2 records) should be DMAed immediately */
	max_buf = min_t(u16, bp->coal_bufs / 4, 2);
	/* max_buf must not be zero */
	max_buf = clamp_t(u16, max_buf, 1, 63);
	max_buf_irq = clamp_t(u16, bp->coal_bufs_irq, 1, 63);
	buf_tmr = max_t(u16, bp->coal_ticks / 4, 1);
	buf_tmr_irq = max_t(u16, bp->coal_ticks_irq, 1);

	flags = RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_TIMER_RESET;

	/* RING_IDLE generates more IRQs for lower latency.  Enable it only
	 * if coal_ticks is less than 25 us.
	 */
	if (BNXT_COAL_TIMER_TO_USEC(bp->coal_ticks) < 25)
		flags |= RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_RING_IDLE;

	req.flags = cpu_to_le16(flags);
	req.num_cmpl_dma_aggr = cpu_to_le16(max_buf);
	req.num_cmpl_dma_aggr_during_int = cpu_to_le16(max_buf_irq);
	req.cmpl_aggr_dma_tmr = cpu_to_le16(buf_tmr);
	req.cmpl_aggr_dma_tmr_during_int = cpu_to_le16(buf_tmr_irq);
	req.int_lat_tmr_min = cpu_to_le16(buf_tmr);
	req.int_lat_tmr_max = cpu_to_le16(bp->coal_ticks);
	req.num_cmpl_aggr_int = cpu_to_le16(bp->coal_bufs);

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		req.ring_id = cpu_to_le16(bp->grp_info[i].cp_fw_ring_id);

		rc = _hwrm_send_message(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
		if (rc)
			break;
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_stat_ctx_free(struct bnxt *bp)
{
	int rc = 0, i;
	struct hwrm_stat_ctx_free_input req = {0};

	if (!bp->bnapi)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_STAT_CTX_FREE, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;

		if (cpr->hw_stats_ctx_id != INVALID_STATS_CTX_ID) {
			req.stat_ctx_id = cpu_to_le32(cpr->hw_stats_ctx_id);

			rc = _hwrm_send_message(bp, &req, sizeof(req),
						HWRM_CMD_TIMEOUT);
			if (rc)
				break;

			cpr->hw_stats_ctx_id = INVALID_STATS_CTX_ID;
		}
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_stat_ctx_alloc(struct bnxt *bp)
{
	int rc = 0, i;
	struct hwrm_stat_ctx_alloc_input req = {0};
	struct hwrm_stat_ctx_alloc_output *resp = bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_STAT_CTX_ALLOC, -1, -1);

	req.update_period_ms = cpu_to_le32(1000);

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;

		req.stats_dma_addr = cpu_to_le64(cpr->hw_stats_map);

		rc = _hwrm_send_message(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
		if (rc)
			break;

		cpr->hw_stats_ctx_id = le32_to_cpu(resp->stat_ctx_id);

		bp->grp_info[i].fw_stats_ctx = cpr->hw_stats_ctx_id;
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return 0;
}

static int bnxt_hwrm_func_qcaps(struct bnxt *bp)
{
	int rc = 0;
	struct hwrm_func_qcaps_input req = {0};
	struct hwrm_func_qcaps_output *resp = bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_QCAPS, -1, -1);
	req.fid = cpu_to_le16(0xffff);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		goto hwrm_func_qcaps_exit;

	if (BNXT_PF(bp)) {
		struct bnxt_pf_info *pf = &bp->pf;

		pf->fw_fid = le16_to_cpu(resp->fid);
		pf->port_id = le16_to_cpu(resp->port_id);
		memcpy(pf->mac_addr, resp->perm_mac_address, ETH_ALEN);
		pf->max_rsscos_ctxs = le16_to_cpu(resp->max_rsscos_ctx);
		pf->max_cp_rings = le16_to_cpu(resp->max_cmpl_rings);
		pf->max_tx_rings = le16_to_cpu(resp->max_tx_rings);
		pf->max_pf_tx_rings = pf->max_tx_rings;
		pf->max_rx_rings = le16_to_cpu(resp->max_rx_rings);
		pf->max_pf_rx_rings = pf->max_rx_rings;
		pf->max_l2_ctxs = le16_to_cpu(resp->max_l2_ctxs);
		pf->max_vnics = le16_to_cpu(resp->max_vnics);
		pf->max_stat_ctxs = le16_to_cpu(resp->max_stat_ctx);
		pf->first_vf_id = le16_to_cpu(resp->first_vf_id);
		pf->max_vfs = le16_to_cpu(resp->max_vfs);
		pf->max_encap_records = le32_to_cpu(resp->max_encap_records);
		pf->max_decap_records = le32_to_cpu(resp->max_decap_records);
		pf->max_tx_em_flows = le32_to_cpu(resp->max_tx_em_flows);
		pf->max_tx_wm_flows = le32_to_cpu(resp->max_tx_wm_flows);
		pf->max_rx_em_flows = le32_to_cpu(resp->max_rx_em_flows);
		pf->max_rx_wm_flows = le32_to_cpu(resp->max_rx_wm_flows);
	} else {
#ifdef CONFIG_BNXT_SRIOV
		struct bnxt_vf_info *vf = &bp->vf;

		vf->fw_fid = le16_to_cpu(resp->fid);
		memcpy(vf->mac_addr, resp->perm_mac_address, ETH_ALEN);
		if (!is_valid_ether_addr(vf->mac_addr))
			random_ether_addr(vf->mac_addr);

		vf->max_rsscos_ctxs = le16_to_cpu(resp->max_rsscos_ctx);
		vf->max_cp_rings = le16_to_cpu(resp->max_cmpl_rings);
		vf->max_tx_rings = le16_to_cpu(resp->max_tx_rings);
		vf->max_rx_rings = le16_to_cpu(resp->max_rx_rings);
		vf->max_l2_ctxs = le16_to_cpu(resp->max_l2_ctxs);
		vf->max_vnics = le16_to_cpu(resp->max_vnics);
		vf->max_stat_ctxs = le16_to_cpu(resp->max_stat_ctx);
#endif
	}

	bp->tx_push_thresh = 0;
	if (resp->flags &
	    cpu_to_le32(FUNC_QCAPS_RESP_FLAGS_PUSH_MODE_SUPPORTED))
		bp->tx_push_thresh = BNXT_TX_PUSH_THRESH;

hwrm_func_qcaps_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_func_reset(struct bnxt *bp)
{
	struct hwrm_func_reset_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_RESET, -1, -1);
	req.enables = 0;

	return hwrm_send_message(bp, &req, sizeof(req), HWRM_RESET_TIMEOUT);
}

static int bnxt_hwrm_queue_qportcfg(struct bnxt *bp)
{
	int rc = 0;
	struct hwrm_queue_qportcfg_input req = {0};
	struct hwrm_queue_qportcfg_output *resp = bp->hwrm_cmd_resp_addr;
	u8 i, *qptr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_QUEUE_QPORTCFG, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		goto qportcfg_exit;

	if (!resp->max_configurable_queues) {
		rc = -EINVAL;
		goto qportcfg_exit;
	}
	bp->max_tc = resp->max_configurable_queues;
	if (bp->max_tc > BNXT_MAX_QUEUE)
		bp->max_tc = BNXT_MAX_QUEUE;

	qptr = &resp->queue_id0;
	for (i = 0; i < bp->max_tc; i++) {
		bp->q_info[i].queue_id = *qptr++;
		bp->q_info[i].queue_profile = *qptr++;
	}

qportcfg_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_ver_get(struct bnxt *bp)
{
	int rc;
	struct hwrm_ver_get_input req = {0};
	struct hwrm_ver_get_output *resp = bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VER_GET, -1, -1);
	req.hwrm_intf_maj = HWRM_VERSION_MAJOR;
	req.hwrm_intf_min = HWRM_VERSION_MINOR;
	req.hwrm_intf_upd = HWRM_VERSION_UPDATE;
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		goto hwrm_ver_get_exit;

	memcpy(&bp->ver_resp, resp, sizeof(struct hwrm_ver_get_output));

	if (req.hwrm_intf_maj != resp->hwrm_intf_maj ||
	    req.hwrm_intf_min != resp->hwrm_intf_min ||
	    req.hwrm_intf_upd != resp->hwrm_intf_upd) {
		netdev_warn(bp->dev, "HWRM interface %d.%d.%d does not match driver interface %d.%d.%d.\n",
			    resp->hwrm_intf_maj, resp->hwrm_intf_min,
			    resp->hwrm_intf_upd, req.hwrm_intf_maj,
			    req.hwrm_intf_min, req.hwrm_intf_upd);
		netdev_warn(bp->dev, "Please update driver or firmware with matching interface versions.\n");
	}
	snprintf(bp->fw_ver_str, BC_HWRM_STR_LEN, "bc %d.%d.%d rm %d.%d.%d",
		 resp->hwrm_fw_maj, resp->hwrm_fw_min, resp->hwrm_fw_bld,
		 resp->hwrm_intf_maj, resp->hwrm_intf_min, resp->hwrm_intf_upd);

hwrm_ver_get_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static void bnxt_hwrm_free_tunnel_ports(struct bnxt *bp)
{
	if (bp->vxlan_port_cnt) {
		bnxt_hwrm_tunnel_dst_port_free(
			bp, TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN);
	}
	bp->vxlan_port_cnt = 0;
	if (bp->nge_port_cnt) {
		bnxt_hwrm_tunnel_dst_port_free(
			bp, TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_GENEVE);
	}
	bp->nge_port_cnt = 0;
}

static int bnxt_set_tpa(struct bnxt *bp, bool set_tpa)
{
	int rc, i;
	u32 tpa_flags = 0;

	if (set_tpa)
		tpa_flags = bp->flags & BNXT_FLAG_TPA;
	for (i = 0; i < bp->nr_vnics; i++) {
		rc = bnxt_hwrm_vnic_set_tpa(bp, i, tpa_flags);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic set tpa failure rc for vnic %d: %x\n",
				   rc, i);
			return rc;
		}
	}
	return 0;
}

static void bnxt_hwrm_clear_vnic_rss(struct bnxt *bp)
{
	int i;

	for (i = 0; i < bp->nr_vnics; i++)
		bnxt_hwrm_vnic_set_rss(bp, i, false);
}

static void bnxt_hwrm_resource_free(struct bnxt *bp, bool close_path,
				    bool irq_re_init)
{
	if (bp->vnic_info) {
		bnxt_hwrm_clear_vnic_filter(bp);
		/* clear all RSS setting before free vnic ctx */
		bnxt_hwrm_clear_vnic_rss(bp);
		bnxt_hwrm_vnic_ctx_free(bp);
		/* before free the vnic, undo the vnic tpa settings */
		if (bp->flags & BNXT_FLAG_TPA)
			bnxt_set_tpa(bp, false);
		bnxt_hwrm_vnic_free(bp);
	}
	bnxt_hwrm_ring_free(bp, close_path);
	bnxt_hwrm_ring_grp_free(bp);
	if (irq_re_init) {
		bnxt_hwrm_stat_ctx_free(bp);
		bnxt_hwrm_free_tunnel_ports(bp);
	}
}

static int bnxt_setup_vnic(struct bnxt *bp, u16 vnic_id)
{
	int rc;

	/* allocate context for vnic */
	rc = bnxt_hwrm_vnic_ctx_alloc(bp, vnic_id);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic %d alloc failure rc: %x\n",
			   vnic_id, rc);
		goto vnic_setup_err;
	}
	bp->rsscos_nr_ctxs++;

	/* configure default vnic, ring grp */
	rc = bnxt_hwrm_vnic_cfg(bp, vnic_id);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic %d cfg failure rc: %x\n",
			   vnic_id, rc);
		goto vnic_setup_err;
	}

	/* Enable RSS hashing on vnic */
	rc = bnxt_hwrm_vnic_set_rss(bp, vnic_id, true);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic %d set rss failure rc: %x\n",
			   vnic_id, rc);
		goto vnic_setup_err;
	}

	if (bp->flags & BNXT_FLAG_AGG_RINGS) {
		rc = bnxt_hwrm_vnic_set_hds(bp, vnic_id);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic %d set hds failure rc: %x\n",
				   vnic_id, rc);
		}
	}

vnic_setup_err:
	return rc;
}

static int bnxt_alloc_rfs_vnics(struct bnxt *bp)
{
#ifdef CONFIG_RFS_ACCEL
	int i, rc = 0;

	for (i = 0; i < bp->rx_nr_rings; i++) {
		u16 vnic_id = i + 1;
		u16 ring_id = i;

		if (vnic_id >= bp->nr_vnics)
			break;

		bp->vnic_info[vnic_id].flags |= BNXT_VNIC_RFS_FLAG;
		rc = bnxt_hwrm_vnic_alloc(bp, vnic_id, ring_id, ring_id + 1);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic %d alloc failure rc: %x\n",
				   vnic_id, rc);
			break;
		}
		rc = bnxt_setup_vnic(bp, vnic_id);
		if (rc)
			break;
	}
	return rc;
#else
	return 0;
#endif
}

static int bnxt_init_chip(struct bnxt *bp, bool irq_re_init)
{
	int rc = 0;

	if (irq_re_init) {
		rc = bnxt_hwrm_stat_ctx_alloc(bp);
		if (rc) {
			netdev_err(bp->dev, "hwrm stat ctx alloc failure rc: %x\n",
				   rc);
			goto err_out;
		}
	}

	rc = bnxt_hwrm_ring_alloc(bp);
	if (rc) {
		netdev_err(bp->dev, "hwrm ring alloc failure rc: %x\n", rc);
		goto err_out;
	}

	rc = bnxt_hwrm_ring_grp_alloc(bp);
	if (rc) {
		netdev_err(bp->dev, "hwrm_ring_grp alloc failure: %x\n", rc);
		goto err_out;
	}

	/* default vnic 0 */
	rc = bnxt_hwrm_vnic_alloc(bp, 0, 0, bp->rx_nr_rings);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic alloc failure rc: %x\n", rc);
		goto err_out;
	}

	rc = bnxt_setup_vnic(bp, 0);
	if (rc)
		goto err_out;

	if (bp->flags & BNXT_FLAG_RFS) {
		rc = bnxt_alloc_rfs_vnics(bp);
		if (rc)
			goto err_out;
	}

	if (bp->flags & BNXT_FLAG_TPA) {
		rc = bnxt_set_tpa(bp, true);
		if (rc)
			goto err_out;
	}

	if (BNXT_VF(bp))
		bnxt_update_vf_mac(bp);

	/* Filter for default vnic 0 */
	rc = bnxt_hwrm_set_vnic_filter(bp, 0, 0, bp->dev->dev_addr);
	if (rc) {
		netdev_err(bp->dev, "HWRM vnic filter failure rc: %x\n", rc);
		goto err_out;
	}
	bp->vnic_info[0].uc_filter_count = 1;

	bp->vnic_info[0].rx_mask = CFA_L2_SET_RX_MASK_REQ_MASK_UNICAST |
				   CFA_L2_SET_RX_MASK_REQ_MASK_BCAST;

	if ((bp->dev->flags & IFF_PROMISC) && BNXT_PF(bp))
		bp->vnic_info[0].rx_mask |=
				CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS;

	rc = bnxt_hwrm_cfa_l2_set_rx_mask(bp, 0);
	if (rc) {
		netdev_err(bp->dev, "HWRM cfa l2 rx mask failure rc: %x\n", rc);
		goto err_out;
	}

	rc = bnxt_hwrm_set_coal(bp);
	if (rc)
		netdev_warn(bp->dev, "HWRM set coalescing failure rc: %x\n",
			    rc);

	return 0;

err_out:
	bnxt_hwrm_resource_free(bp, 0, true);

	return rc;
}

static int bnxt_shutdown_nic(struct bnxt *bp, bool irq_re_init)
{
	bnxt_hwrm_resource_free(bp, 1, irq_re_init);
	return 0;
}

static int bnxt_init_nic(struct bnxt *bp, bool irq_re_init)
{
	bnxt_init_rx_rings(bp);
	bnxt_init_tx_rings(bp);
	bnxt_init_ring_grps(bp, irq_re_init);
	bnxt_init_vnics(bp);

	return bnxt_init_chip(bp, irq_re_init);
}

static void bnxt_disable_int(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;

		BNXT_CP_DB(cpr->cp_doorbell, cpr->cp_raw_cons);
	}
}

static void bnxt_enable_int(struct bnxt *bp)
{
	int i;

	atomic_set(&bp->intr_sem, 0);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;

		BNXT_CP_DB_REARM(cpr->cp_doorbell, cpr->cp_raw_cons);
	}
}

static int bnxt_set_real_num_queues(struct bnxt *bp)
{
	int rc;
	struct net_device *dev = bp->dev;

	rc = netif_set_real_num_tx_queues(dev, bp->tx_nr_rings);
	if (rc)
		return rc;

	rc = netif_set_real_num_rx_queues(dev, bp->rx_nr_rings);
	if (rc)
		return rc;

#ifdef CONFIG_RFS_ACCEL
	if (bp->rx_nr_rings)
		dev->rx_cpu_rmap = alloc_irq_cpu_rmap(bp->rx_nr_rings);
	if (!dev->rx_cpu_rmap)
		rc = -ENOMEM;
#endif

	return rc;
}

static int bnxt_setup_msix(struct bnxt *bp)
{
	struct msix_entry *msix_ent;
	struct net_device *dev = bp->dev;
	int i, total_vecs, rc = 0;
	const int len = sizeof(bp->irq_tbl[0].name);

	bp->flags &= ~BNXT_FLAG_USING_MSIX;
	total_vecs = bp->cp_nr_rings;

	msix_ent = kcalloc(total_vecs, sizeof(struct msix_entry), GFP_KERNEL);
	if (!msix_ent)
		return -ENOMEM;

	for (i = 0; i < total_vecs; i++) {
		msix_ent[i].entry = i;
		msix_ent[i].vector = 0;
	}

	total_vecs = pci_enable_msix_range(bp->pdev, msix_ent, 1, total_vecs);
	if (total_vecs < 0) {
		rc = -ENODEV;
		goto msix_setup_exit;
	}

	bp->irq_tbl = kcalloc(total_vecs, sizeof(struct bnxt_irq), GFP_KERNEL);
	if (bp->irq_tbl) {
		int tcs;

		/* Trim rings based upon num of vectors allocated */
		bp->rx_nr_rings = min_t(int, total_vecs, bp->rx_nr_rings);
		bp->tx_nr_rings = min_t(int, total_vecs, bp->tx_nr_rings);
		bp->tx_nr_rings_per_tc = bp->tx_nr_rings;
		tcs = netdev_get_num_tc(dev);
		if (tcs > 1) {
			bp->tx_nr_rings_per_tc = bp->tx_nr_rings / tcs;
			if (bp->tx_nr_rings_per_tc == 0) {
				netdev_reset_tc(dev);
				bp->tx_nr_rings_per_tc = bp->tx_nr_rings;
			} else {
				int i, off, count;

				bp->tx_nr_rings = bp->tx_nr_rings_per_tc * tcs;
				for (i = 0; i < tcs; i++) {
					count = bp->tx_nr_rings_per_tc;
					off = i * count;
					netdev_set_tc_queue(dev, i, count, off);
				}
			}
		}
		bp->cp_nr_rings = max_t(int, bp->rx_nr_rings, bp->tx_nr_rings);

		for (i = 0; i < bp->cp_nr_rings; i++) {
			bp->irq_tbl[i].vector = msix_ent[i].vector;
			snprintf(bp->irq_tbl[i].name, len,
				 "%s-%s-%d", dev->name, "TxRx", i);
			bp->irq_tbl[i].handler = bnxt_msix;
		}
		rc = bnxt_set_real_num_queues(bp);
		if (rc)
			goto msix_setup_exit;
	} else {
		rc = -ENOMEM;
		goto msix_setup_exit;
	}
	bp->flags |= BNXT_FLAG_USING_MSIX;
	kfree(msix_ent);
	return 0;

msix_setup_exit:
	netdev_err(bp->dev, "bnxt_setup_msix err: %x\n", rc);
	pci_disable_msix(bp->pdev);
	kfree(msix_ent);
	return rc;
}

static int bnxt_setup_inta(struct bnxt *bp)
{
	int rc;
	const int len = sizeof(bp->irq_tbl[0].name);

	if (netdev_get_num_tc(bp->dev))
		netdev_reset_tc(bp->dev);

	bp->irq_tbl = kcalloc(1, sizeof(struct bnxt_irq), GFP_KERNEL);
	if (!bp->irq_tbl) {
		rc = -ENOMEM;
		return rc;
	}
	bp->rx_nr_rings = 1;
	bp->tx_nr_rings = 1;
	bp->cp_nr_rings = 1;
	bp->tx_nr_rings_per_tc = bp->tx_nr_rings;
	bp->irq_tbl[0].vector = bp->pdev->irq;
	snprintf(bp->irq_tbl[0].name, len,
		 "%s-%s-%d", bp->dev->name, "TxRx", 0);
	bp->irq_tbl[0].handler = bnxt_inta;
	rc = bnxt_set_real_num_queues(bp);
	return rc;
}

static int bnxt_setup_int_mode(struct bnxt *bp)
{
	int rc = 0;

	if (bp->flags & BNXT_FLAG_MSIX_CAP)
		rc = bnxt_setup_msix(bp);

	if (!(bp->flags & BNXT_FLAG_USING_MSIX)) {
		/* fallback to INTA */
		rc = bnxt_setup_inta(bp);
	}
	return rc;
}

static void bnxt_free_irq(struct bnxt *bp)
{
	struct bnxt_irq *irq;
	int i;

#ifdef CONFIG_RFS_ACCEL
	free_irq_cpu_rmap(bp->dev->rx_cpu_rmap);
	bp->dev->rx_cpu_rmap = NULL;
#endif
	if (!bp->irq_tbl)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		irq = &bp->irq_tbl[i];
		if (irq->requested)
			free_irq(irq->vector, bp->bnapi[i]);
		irq->requested = 0;
	}
	if (bp->flags & BNXT_FLAG_USING_MSIX)
		pci_disable_msix(bp->pdev);
	kfree(bp->irq_tbl);
	bp->irq_tbl = NULL;
}

static int bnxt_request_irq(struct bnxt *bp)
{
	int i, rc = 0;
	unsigned long flags = 0;
#ifdef CONFIG_RFS_ACCEL
	struct cpu_rmap *rmap = bp->dev->rx_cpu_rmap;
#endif

	if (!(bp->flags & BNXT_FLAG_USING_MSIX))
		flags = IRQF_SHARED;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_irq *irq = &bp->irq_tbl[i];
#ifdef CONFIG_RFS_ACCEL
		if (rmap && (i < bp->rx_nr_rings)) {
			rc = irq_cpu_rmap_add(rmap, irq->vector);
			if (rc)
				netdev_warn(bp->dev, "failed adding irq rmap for ring %d\n",
					    i);
		}
#endif
		rc = request_irq(irq->vector, irq->handler, flags, irq->name,
				 bp->bnapi[i]);
		if (rc)
			break;

		irq->requested = 1;
	}
	return rc;
}

static void bnxt_del_napi(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];

		napi_hash_del(&bnapi->napi);
		netif_napi_del(&bnapi->napi);
	}
}

static void bnxt_init_napi(struct bnxt *bp)
{
	int i;
	struct bnxt_napi *bnapi;

	if (bp->flags & BNXT_FLAG_USING_MSIX) {
		for (i = 0; i < bp->cp_nr_rings; i++) {
			bnapi = bp->bnapi[i];
			netif_napi_add(bp->dev, &bnapi->napi,
				       bnxt_poll, 64);
			napi_hash_add(&bnapi->napi);
		}
	} else {
		bnapi = bp->bnapi[0];
		netif_napi_add(bp->dev, &bnapi->napi, bnxt_poll, 64);
		napi_hash_add(&bnapi->napi);
	}
}

static void bnxt_disable_napi(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		napi_disable(&bp->bnapi[i]->napi);
		bnxt_disable_poll(bp->bnapi[i]);
	}
}

static void bnxt_enable_napi(struct bnxt *bp)
{
	int i;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		bnxt_enable_poll(bp->bnapi[i]);
		napi_enable(&bp->bnapi[i]->napi);
	}
}

static void bnxt_tx_disable(struct bnxt *bp)
{
	int i;
	struct bnxt_napi *bnapi;
	struct bnxt_tx_ring_info *txr;
	struct netdev_queue *txq;

	if (bp->bnapi) {
		for (i = 0; i < bp->tx_nr_rings; i++) {
			bnapi = bp->bnapi[i];
			txr = &bnapi->tx_ring;
			txq = netdev_get_tx_queue(bp->dev, i);
			__netif_tx_lock(txq, smp_processor_id());
			txr->dev_state = BNXT_DEV_STATE_CLOSING;
			__netif_tx_unlock(txq);
		}
	}
	/* Stop all TX queues */
	netif_tx_disable(bp->dev);
	netif_carrier_off(bp->dev);
}

static void bnxt_tx_enable(struct bnxt *bp)
{
	int i;
	struct bnxt_napi *bnapi;
	struct bnxt_tx_ring_info *txr;
	struct netdev_queue *txq;

	for (i = 0; i < bp->tx_nr_rings; i++) {
		bnapi = bp->bnapi[i];
		txr = &bnapi->tx_ring;
		txq = netdev_get_tx_queue(bp->dev, i);
		txr->dev_state = 0;
	}
	netif_tx_wake_all_queues(bp->dev);
	if (bp->link_info.link_up)
		netif_carrier_on(bp->dev);
}

static void bnxt_report_link(struct bnxt *bp)
{
	if (bp->link_info.link_up) {
		const char *duplex;
		const char *flow_ctrl;
		u16 speed;

		netif_carrier_on(bp->dev);
		if (bp->link_info.duplex == BNXT_LINK_DUPLEX_FULL)
			duplex = "full";
		else
			duplex = "half";
		if (bp->link_info.pause == BNXT_LINK_PAUSE_BOTH)
			flow_ctrl = "ON - receive & transmit";
		else if (bp->link_info.pause == BNXT_LINK_PAUSE_TX)
			flow_ctrl = "ON - transmit";
		else if (bp->link_info.pause == BNXT_LINK_PAUSE_RX)
			flow_ctrl = "ON - receive";
		else
			flow_ctrl = "none";
		speed = bnxt_fw_to_ethtool_speed(bp->link_info.link_speed);
		netdev_info(bp->dev, "NIC Link is Up, %d Mbps %s duplex, Flow control: %s\n",
			    speed, duplex, flow_ctrl);
	} else {
		netif_carrier_off(bp->dev);
		netdev_err(bp->dev, "NIC Link is Down\n");
	}
}

static int bnxt_update_link(struct bnxt *bp, bool chng_link_state)
{
	int rc = 0;
	struct bnxt_link_info *link_info = &bp->link_info;
	struct hwrm_port_phy_qcfg_input req = {0};
	struct hwrm_port_phy_qcfg_output *resp = bp->hwrm_cmd_resp_addr;
	u8 link_up = link_info->link_up;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_PHY_QCFG, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc) {
		mutex_unlock(&bp->hwrm_cmd_lock);
		return rc;
	}

	memcpy(&link_info->phy_qcfg_resp, resp, sizeof(*resp));
	link_info->phy_link_status = resp->link;
	link_info->duplex =  resp->duplex;
	link_info->pause = resp->pause;
	link_info->auto_mode = resp->auto_mode;
	link_info->auto_pause_setting = resp->auto_pause;
	link_info->force_pause_setting = resp->force_pause;
	link_info->duplex_setting = resp->duplex_setting;
	if (link_info->phy_link_status == BNXT_LINK_LINK)
		link_info->link_speed = le16_to_cpu(resp->link_speed);
	else
		link_info->link_speed = 0;
	link_info->force_link_speed = le16_to_cpu(resp->force_link_speed);
	link_info->auto_link_speed = le16_to_cpu(resp->auto_link_speed);
	link_info->support_speeds = le16_to_cpu(resp->support_speeds);
	link_info->auto_link_speeds = le16_to_cpu(resp->auto_link_speed_mask);
	link_info->preemphasis = le32_to_cpu(resp->preemphasis);
	link_info->phy_ver[0] = resp->phy_maj;
	link_info->phy_ver[1] = resp->phy_min;
	link_info->phy_ver[2] = resp->phy_bld;
	link_info->media_type = resp->media_type;
	link_info->transceiver = resp->transceiver_type;
	link_info->phy_addr = resp->phy_addr;

	/* TODO: need to add more logic to report VF link */
	if (chng_link_state) {
		if (link_info->phy_link_status == BNXT_LINK_LINK)
			link_info->link_up = 1;
		else
			link_info->link_up = 0;
		if (link_up != link_info->link_up)
			bnxt_report_link(bp);
	} else {
		/* alwasy link down if not require to update link state */
		link_info->link_up = 0;
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return 0;
}

static void
bnxt_hwrm_set_pause_common(struct bnxt *bp, struct hwrm_port_phy_cfg_input *req)
{
	if (bp->link_info.autoneg & BNXT_AUTONEG_FLOW_CTRL) {
		if (bp->link_info.req_flow_ctrl & BNXT_LINK_PAUSE_RX)
			req->auto_pause |= PORT_PHY_CFG_REQ_AUTO_PAUSE_RX;
		if (bp->link_info.req_flow_ctrl & BNXT_LINK_PAUSE_TX)
			req->auto_pause |= PORT_PHY_CFG_REQ_AUTO_PAUSE_RX;
		req->enables |=
			cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_AUTO_PAUSE);
	} else {
		if (bp->link_info.req_flow_ctrl & BNXT_LINK_PAUSE_RX)
			req->force_pause |= PORT_PHY_CFG_REQ_FORCE_PAUSE_RX;
		if (bp->link_info.req_flow_ctrl & BNXT_LINK_PAUSE_TX)
			req->force_pause |= PORT_PHY_CFG_REQ_FORCE_PAUSE_TX;
		req->enables |=
			cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_FORCE_PAUSE);
	}
}

static void bnxt_hwrm_set_link_common(struct bnxt *bp,
				      struct hwrm_port_phy_cfg_input *req)
{
	u8 autoneg = bp->link_info.autoneg;
	u16 fw_link_speed = bp->link_info.req_link_speed;
	u32 advertising = bp->link_info.advertising;

	if (autoneg & BNXT_AUTONEG_SPEED) {
		req->auto_mode |=
			PORT_PHY_CFG_REQ_AUTO_MODE_MASK;

		req->enables |= cpu_to_le32(
			PORT_PHY_CFG_REQ_ENABLES_AUTO_LINK_SPEED_MASK);
		req->auto_link_speed_mask = cpu_to_le16(advertising);

		req->enables |= cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_AUTO_MODE);
		req->flags |=
			cpu_to_le32(PORT_PHY_CFG_REQ_FLAGS_RESTART_AUTONEG);
	} else {
		req->force_link_speed = cpu_to_le16(fw_link_speed);
		req->flags |= cpu_to_le32(PORT_PHY_CFG_REQ_FLAGS_FORCE);
	}

	/* currently don't support half duplex */
	req->auto_duplex = PORT_PHY_CFG_REQ_AUTO_DUPLEX_FULL;
	req->enables |= cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_AUTO_DUPLEX);
	/* tell chimp that the setting takes effect immediately */
	req->flags |= cpu_to_le32(PORT_PHY_CFG_REQ_FLAGS_RESET_PHY);
}

int bnxt_hwrm_set_pause(struct bnxt *bp)
{
	struct hwrm_port_phy_cfg_input req = {0};
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_PHY_CFG, -1, -1);
	bnxt_hwrm_set_pause_common(bp, &req);

	if ((bp->link_info.autoneg & BNXT_AUTONEG_FLOW_CTRL) ||
	    bp->link_info.force_link_chng)
		bnxt_hwrm_set_link_common(bp, &req);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc && !(bp->link_info.autoneg & BNXT_AUTONEG_FLOW_CTRL)) {
		/* since changing of pause setting doesn't trigger any link
		 * change event, the driver needs to update the current pause
		 * result upon successfully return of the phy_cfg command
		 */
		bp->link_info.pause =
		bp->link_info.force_pause_setting = bp->link_info.req_flow_ctrl;
		bp->link_info.auto_pause_setting = 0;
		if (!bp->link_info.force_link_chng)
			bnxt_report_link(bp);
	}
	bp->link_info.force_link_chng = false;
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

int bnxt_hwrm_set_link_setting(struct bnxt *bp, bool set_pause)
{
	struct hwrm_port_phy_cfg_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_PHY_CFG, -1, -1);
	if (set_pause)
		bnxt_hwrm_set_pause_common(bp, &req);

	bnxt_hwrm_set_link_common(bp, &req);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_update_phy_setting(struct bnxt *bp)
{
	int rc;
	bool update_link = false;
	bool update_pause = false;
	struct bnxt_link_info *link_info = &bp->link_info;

	rc = bnxt_update_link(bp, true);
	if (rc) {
		netdev_err(bp->dev, "failed to update link (rc: %x)\n",
			   rc);
		return rc;
	}
	if ((link_info->autoneg & BNXT_AUTONEG_FLOW_CTRL) &&
	    link_info->auto_pause_setting != link_info->req_flow_ctrl)
		update_pause = true;
	if (!(link_info->autoneg & BNXT_AUTONEG_FLOW_CTRL) &&
	    link_info->force_pause_setting != link_info->req_flow_ctrl)
		update_pause = true;
	if (link_info->req_duplex != link_info->duplex_setting)
		update_link = true;
	if (!(link_info->autoneg & BNXT_AUTONEG_SPEED)) {
		if (BNXT_AUTO_MODE(link_info->auto_mode))
			update_link = true;
		if (link_info->req_link_speed != link_info->force_link_speed)
			update_link = true;
	} else {
		if (link_info->auto_mode == BNXT_LINK_AUTO_NONE)
			update_link = true;
		if (link_info->advertising != link_info->auto_link_speeds)
			update_link = true;
		if (link_info->req_link_speed != link_info->auto_link_speed)
			update_link = true;
	}

	if (update_link)
		rc = bnxt_hwrm_set_link_setting(bp, update_pause);
	else if (update_pause)
		rc = bnxt_hwrm_set_pause(bp);
	if (rc) {
		netdev_err(bp->dev, "failed to update phy setting (rc: %x)\n",
			   rc);
		return rc;
	}

	return rc;
}

static int __bnxt_open_nic(struct bnxt *bp, bool irq_re_init, bool link_re_init)
{
	int rc = 0;

	netif_carrier_off(bp->dev);
	if (irq_re_init) {
		rc = bnxt_setup_int_mode(bp);
		if (rc) {
			netdev_err(bp->dev, "bnxt_setup_int_mode err: %x\n",
				   rc);
			return rc;
		}
	}
	if ((bp->flags & BNXT_FLAG_RFS) &&
	    !(bp->flags & BNXT_FLAG_USING_MSIX)) {
		/* disable RFS if falling back to INTA */
		bp->dev->hw_features &= ~NETIF_F_NTUPLE;
		bp->flags &= ~BNXT_FLAG_RFS;
	}

	rc = bnxt_alloc_mem(bp, irq_re_init);
	if (rc) {
		netdev_err(bp->dev, "bnxt_alloc_mem err: %x\n", rc);
		goto open_err_free_mem;
	}

	if (irq_re_init) {
		bnxt_init_napi(bp);
		rc = bnxt_request_irq(bp);
		if (rc) {
			netdev_err(bp->dev, "bnxt_request_irq err: %x\n", rc);
			goto open_err;
		}
	}

	bnxt_enable_napi(bp);

	rc = bnxt_init_nic(bp, irq_re_init);
	if (rc) {
		netdev_err(bp->dev, "bnxt_init_nic err: %x\n", rc);
		goto open_err;
	}

	if (link_re_init) {
		rc = bnxt_update_phy_setting(bp);
		if (rc)
			goto open_err;
	}

	if (irq_re_init) {
#if defined(CONFIG_VXLAN) || defined(CONFIG_VXLAN_MODULE)
		vxlan_get_rx_port(bp->dev);
#endif
		if (!bnxt_hwrm_tunnel_dst_port_alloc(
				bp, htons(0x17c1),
				TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_GENEVE))
			bp->nge_port_cnt = 1;
	}

	bp->state = BNXT_STATE_OPEN;
	bnxt_enable_int(bp);
	/* Enable TX queues */
	bnxt_tx_enable(bp);
	mod_timer(&bp->timer, jiffies + bp->current_interval);

	return 0;

open_err:
	bnxt_disable_napi(bp);
	bnxt_del_napi(bp);

open_err_free_mem:
	bnxt_free_skbs(bp);
	bnxt_free_irq(bp);
	bnxt_free_mem(bp, true);
	return rc;
}

/* rtnl_lock held */
int bnxt_open_nic(struct bnxt *bp, bool irq_re_init, bool link_re_init)
{
	int rc = 0;

	rc = __bnxt_open_nic(bp, irq_re_init, link_re_init);
	if (rc) {
		netdev_err(bp->dev, "nic open fail (rc: %x)\n", rc);
		dev_close(bp->dev);
	}
	return rc;
}

static int bnxt_open(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc = 0;

	rc = bnxt_hwrm_func_reset(bp);
	if (rc) {
		netdev_err(bp->dev, "hwrm chip reset failure rc: %x\n",
			   rc);
		rc = -1;
		return rc;
	}
	return __bnxt_open_nic(bp, true, true);
}

static void bnxt_disable_int_sync(struct bnxt *bp)
{
	int i;

	atomic_inc(&bp->intr_sem);
	if (!netif_running(bp->dev))
		return;

	bnxt_disable_int(bp);
	for (i = 0; i < bp->cp_nr_rings; i++)
		synchronize_irq(bp->irq_tbl[i].vector);
}

int bnxt_close_nic(struct bnxt *bp, bool irq_re_init, bool link_re_init)
{
	int rc = 0;

#ifdef CONFIG_BNXT_SRIOV
	if (bp->sriov_cfg) {
		rc = wait_event_interruptible_timeout(bp->sriov_cfg_wait,
						      !bp->sriov_cfg,
						      BNXT_SRIOV_CFG_WAIT_TMO);
		if (rc)
			netdev_warn(bp->dev, "timeout waiting for SRIOV config operation to complete!\n");
	}
#endif
	/* Change device state to avoid TX queue wake up's */
	bnxt_tx_disable(bp);

	bp->state = BNXT_STATE_CLOSED;
	cancel_work_sync(&bp->sp_task);

	/* Flush rings before disabling interrupts */
	bnxt_shutdown_nic(bp, irq_re_init);

	/* TODO CHIMP_FW: Link/PHY related cleanup if (link_re_init) */

	bnxt_disable_napi(bp);
	bnxt_disable_int_sync(bp);
	del_timer_sync(&bp->timer);
	bnxt_free_skbs(bp);

	if (irq_re_init) {
		bnxt_free_irq(bp);
		bnxt_del_napi(bp);
	}
	bnxt_free_mem(bp, irq_re_init);
	return rc;
}

static int bnxt_close(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);

	bnxt_close_nic(bp, true, true);
	return 0;
}

/* rtnl_lock held */
static int bnxt_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCGMIIPHY:
		/* fallthru */
	case SIOCGMIIREG: {
		if (!netif_running(dev))
			return -EAGAIN;

		return 0;
	}

	case SIOCSMIIREG:
		if (!netif_running(dev))
			return -EAGAIN;

		return 0;

	default:
		/* do nothing */
		break;
	}
	return -EOPNOTSUPP;
}

static struct rtnl_link_stats64 *
bnxt_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	u32 i;
	struct bnxt *bp = netdev_priv(dev);

	memset(stats, 0, sizeof(struct rtnl_link_stats64));

	if (!bp->bnapi)
		return stats;

	/* TODO check if we need to synchronize with bnxt_close path */
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
		struct ctx_hw_stats *hw_stats = cpr->hw_stats;

		stats->rx_packets += le64_to_cpu(hw_stats->rx_ucast_pkts);
		stats->rx_packets += le64_to_cpu(hw_stats->rx_mcast_pkts);
		stats->rx_packets += le64_to_cpu(hw_stats->rx_bcast_pkts);

		stats->tx_packets += le64_to_cpu(hw_stats->tx_ucast_pkts);
		stats->tx_packets += le64_to_cpu(hw_stats->tx_mcast_pkts);
		stats->tx_packets += le64_to_cpu(hw_stats->tx_bcast_pkts);

		stats->rx_bytes += le64_to_cpu(hw_stats->rx_ucast_bytes);
		stats->rx_bytes += le64_to_cpu(hw_stats->rx_mcast_bytes);
		stats->rx_bytes += le64_to_cpu(hw_stats->rx_bcast_bytes);

		stats->tx_bytes += le64_to_cpu(hw_stats->tx_ucast_bytes);
		stats->tx_bytes += le64_to_cpu(hw_stats->tx_mcast_bytes);
		stats->tx_bytes += le64_to_cpu(hw_stats->tx_bcast_bytes);

		stats->rx_missed_errors +=
			le64_to_cpu(hw_stats->rx_discard_pkts);

		stats->multicast += le64_to_cpu(hw_stats->rx_mcast_pkts);

		stats->rx_dropped += le64_to_cpu(hw_stats->rx_drop_pkts);

		stats->tx_dropped += le64_to_cpu(hw_stats->tx_drop_pkts);
	}

	return stats;
}

static bool bnxt_mc_list_updated(struct bnxt *bp, u32 *rx_mask)
{
	struct net_device *dev = bp->dev;
	struct bnxt_vnic_info *vnic = &bp->vnic_info[0];
	struct netdev_hw_addr *ha;
	u8 *haddr;
	int mc_count = 0;
	bool update = false;
	int off = 0;

	netdev_for_each_mc_addr(ha, dev) {
		if (mc_count >= BNXT_MAX_MC_ADDRS) {
			*rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST;
			vnic->mc_list_count = 0;
			return false;
		}
		haddr = ha->addr;
		if (!ether_addr_equal(haddr, vnic->mc_list + off)) {
			memcpy(vnic->mc_list + off, haddr, ETH_ALEN);
			update = true;
		}
		off += ETH_ALEN;
		mc_count++;
	}
	if (mc_count)
		*rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_MCAST;

	if (mc_count != vnic->mc_list_count) {
		vnic->mc_list_count = mc_count;
		update = true;
	}
	return update;
}

static bool bnxt_uc_list_updated(struct bnxt *bp)
{
	struct net_device *dev = bp->dev;
	struct bnxt_vnic_info *vnic = &bp->vnic_info[0];
	struct netdev_hw_addr *ha;
	int off = 0;

	if (netdev_uc_count(dev) != (vnic->uc_filter_count - 1))
		return true;

	netdev_for_each_uc_addr(ha, dev) {
		if (!ether_addr_equal(ha->addr, vnic->uc_list + off))
			return true;

		off += ETH_ALEN;
	}
	return false;
}

static void bnxt_set_rx_mode(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_vnic_info *vnic = &bp->vnic_info[0];
	u32 mask = vnic->rx_mask;
	bool mc_update = false;
	bool uc_update;

	if (!netif_running(dev))
		return;

	mask &= ~(CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS |
		  CFA_L2_SET_RX_MASK_REQ_MASK_MCAST |
		  CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST);

	/* Only allow PF to be in promiscuous mode */
	if ((dev->flags & IFF_PROMISC) && BNXT_PF(bp))
		mask |= CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS;

	uc_update = bnxt_uc_list_updated(bp);

	if (dev->flags & IFF_ALLMULTI) {
		mask |= CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST;
		vnic->mc_list_count = 0;
	} else {
		mc_update = bnxt_mc_list_updated(bp, &mask);
	}

	if (mask != vnic->rx_mask || uc_update || mc_update) {
		vnic->rx_mask = mask;

		set_bit(BNXT_RX_MASK_SP_EVENT, &bp->sp_event);
		schedule_work(&bp->sp_task);
	}
}

static void bnxt_cfg_rx_mode(struct bnxt *bp)
{
	struct net_device *dev = bp->dev;
	struct bnxt_vnic_info *vnic = &bp->vnic_info[0];
	struct netdev_hw_addr *ha;
	int i, off = 0, rc;
	bool uc_update;

	netif_addr_lock_bh(dev);
	uc_update = bnxt_uc_list_updated(bp);
	netif_addr_unlock_bh(dev);

	if (!uc_update)
		goto skip_uc;

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 1; i < vnic->uc_filter_count; i++) {
		struct hwrm_cfa_l2_filter_free_input req = {0};

		bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_L2_FILTER_FREE, -1,
				       -1);

		req.l2_filter_id = vnic->fw_l2_filter_id[i];

		rc = _hwrm_send_message(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
	}
	mutex_unlock(&bp->hwrm_cmd_lock);

	vnic->uc_filter_count = 1;

	netif_addr_lock_bh(dev);
	if (netdev_uc_count(dev) > (BNXT_MAX_UC_ADDRS - 1)) {
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS;
	} else {
		netdev_for_each_uc_addr(ha, dev) {
			memcpy(vnic->uc_list + off, ha->addr, ETH_ALEN);
			off += ETH_ALEN;
			vnic->uc_filter_count++;
		}
	}
	netif_addr_unlock_bh(dev);

	for (i = 1, off = 0; i < vnic->uc_filter_count; i++, off += ETH_ALEN) {
		rc = bnxt_hwrm_set_vnic_filter(bp, 0, i, vnic->uc_list + off);
		if (rc) {
			netdev_err(bp->dev, "HWRM vnic filter failure rc: %x\n",
				   rc);
			vnic->uc_filter_count = i;
		}
	}

skip_uc:
	rc = bnxt_hwrm_cfa_l2_set_rx_mask(bp, 0);
	if (rc)
		netdev_err(bp->dev, "HWRM cfa l2 rx mask failure rc: %x\n",
			   rc);
}

static netdev_features_t bnxt_fix_features(struct net_device *dev,
					   netdev_features_t features)
{
	return features;
}

static int bnxt_set_features(struct net_device *dev, netdev_features_t features)
{
	struct bnxt *bp = netdev_priv(dev);
	u32 flags = bp->flags;
	u32 changes;
	int rc = 0;
	bool re_init = false;
	bool update_tpa = false;

	flags &= ~BNXT_FLAG_ALL_CONFIG_FEATS;
	if ((features & NETIF_F_GRO) && (bp->pdev->revision > 0))
		flags |= BNXT_FLAG_GRO;
	if (features & NETIF_F_LRO)
		flags |= BNXT_FLAG_LRO;

	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		flags |= BNXT_FLAG_STRIP_VLAN;

	if (features & NETIF_F_NTUPLE)
		flags |= BNXT_FLAG_RFS;

	changes = flags ^ bp->flags;
	if (changes & BNXT_FLAG_TPA) {
		update_tpa = true;
		if ((bp->flags & BNXT_FLAG_TPA) == 0 ||
		    (flags & BNXT_FLAG_TPA) == 0)
			re_init = true;
	}

	if (changes & ~BNXT_FLAG_TPA)
		re_init = true;

	if (flags != bp->flags) {
		u32 old_flags = bp->flags;

		bp->flags = flags;

		if (!netif_running(dev)) {
			if (update_tpa)
				bnxt_set_ring_params(bp);
			return rc;
		}

		if (re_init) {
			bnxt_close_nic(bp, false, false);
			if (update_tpa)
				bnxt_set_ring_params(bp);

			return bnxt_open_nic(bp, false, false);
		}
		if (update_tpa) {
			rc = bnxt_set_tpa(bp,
					  (flags & BNXT_FLAG_TPA) ?
					  true : false);
			if (rc)
				bp->flags = old_flags;
		}
	}
	return rc;
}

static void bnxt_dbg_dump_states(struct bnxt *bp)
{
	int i;
	struct bnxt_napi *bnapi;
	struct bnxt_tx_ring_info *txr;
	struct bnxt_rx_ring_info *rxr;
	struct bnxt_cp_ring_info *cpr;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		bnapi = bp->bnapi[i];
		txr = &bnapi->tx_ring;
		rxr = &bnapi->rx_ring;
		cpr = &bnapi->cp_ring;
		if (netif_msg_drv(bp)) {
			netdev_info(bp->dev, "[%d]: tx{fw_ring: %d prod: %x cons: %x}\n",
				    i, txr->tx_ring_struct.fw_ring_id,
				    txr->tx_prod, txr->tx_cons);
			netdev_info(bp->dev, "[%d]: rx{fw_ring: %d prod: %x} rx_agg{fw_ring: %d agg_prod: %x sw_agg_prod: %x}\n",
				    i, rxr->rx_ring_struct.fw_ring_id,
				    rxr->rx_prod,
				    rxr->rx_agg_ring_struct.fw_ring_id,
				    rxr->rx_agg_prod, rxr->rx_sw_agg_prod);
			netdev_info(bp->dev, "[%d]: cp{fw_ring: %d raw_cons: %x}\n",
				    i, cpr->cp_ring_struct.fw_ring_id,
				    cpr->cp_raw_cons);
		}
	}
}

static void bnxt_reset_task(struct bnxt *bp)
{
	bnxt_dbg_dump_states(bp);
	if (netif_running(bp->dev))
		bnxt_tx_disable(bp); /* prevent tx timout again */
}

static void bnxt_tx_timeout(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);

	netdev_err(bp->dev,  "TX timeout detected, starting reset task!\n");
	set_bit(BNXT_RESET_TASK_SP_EVENT, &bp->sp_event);
	schedule_work(&bp->sp_task);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void bnxt_poll_controller(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);
	int i;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_irq *irq = &bp->irq_tbl[i];

		disable_irq(irq->vector);
		irq->handler(irq->vector, bp->bnapi[i]);
		enable_irq(irq->vector);
	}
}
#endif

static void bnxt_timer(unsigned long data)
{
	struct bnxt *bp = (struct bnxt *)data;
	struct net_device *dev = bp->dev;

	if (!netif_running(dev))
		return;

	if (atomic_read(&bp->intr_sem) != 0)
		goto bnxt_restart_timer;

bnxt_restart_timer:
	mod_timer(&bp->timer, jiffies + bp->current_interval);
}

static void bnxt_cfg_ntp_filters(struct bnxt *);

static void bnxt_sp_task(struct work_struct *work)
{
	struct bnxt *bp = container_of(work, struct bnxt, sp_task);
	int rc;

	if (bp->state != BNXT_STATE_OPEN)
		return;

	if (test_and_clear_bit(BNXT_RX_MASK_SP_EVENT, &bp->sp_event))
		bnxt_cfg_rx_mode(bp);

	if (test_and_clear_bit(BNXT_RX_NTP_FLTR_SP_EVENT, &bp->sp_event))
		bnxt_cfg_ntp_filters(bp);
	if (test_and_clear_bit(BNXT_LINK_CHNG_SP_EVENT, &bp->sp_event)) {
		rc = bnxt_update_link(bp, true);
		if (rc)
			netdev_err(bp->dev, "SP task can't update link (rc: %x)\n",
				   rc);
	}
	if (test_and_clear_bit(BNXT_HWRM_EXEC_FWD_REQ_SP_EVENT, &bp->sp_event))
		bnxt_hwrm_exec_fwd_req(bp);
	if (test_and_clear_bit(BNXT_VXLAN_ADD_PORT_SP_EVENT, &bp->sp_event)) {
		bnxt_hwrm_tunnel_dst_port_alloc(
			bp, bp->vxlan_port,
			TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN);
	}
	if (test_and_clear_bit(BNXT_VXLAN_DEL_PORT_SP_EVENT, &bp->sp_event)) {
		bnxt_hwrm_tunnel_dst_port_free(
			bp, TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN);
	}
	if (test_and_clear_bit(BNXT_RESET_TASK_SP_EVENT, &bp->sp_event))
		bnxt_reset_task(bp);
}

static int bnxt_init_board(struct pci_dev *pdev, struct net_device *dev)
{
	int rc;
	struct bnxt *bp = netdev_priv(dev);

	SET_NETDEV_DEV(dev, &pdev->dev);

	/* enable device (incl. PCI PM wakeup), and bus-mastering */
	rc = pci_enable_device(pdev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot enable PCI device, aborting\n");
		goto init_err;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev,
			"Cannot find PCI device base address, aborting\n");
		rc = -ENODEV;
		goto init_err_disable;
	}

	rc = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (rc) {
		dev_err(&pdev->dev, "Cannot obtain PCI resources, aborting\n");
		goto init_err_disable;
	}

	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64)) != 0 &&
	    dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32)) != 0) {
		dev_err(&pdev->dev, "System does not support DMA, aborting\n");
		goto init_err_disable;
	}

	pci_set_master(pdev);

	bp->dev = dev;
	bp->pdev = pdev;

	bp->bar0 = pci_ioremap_bar(pdev, 0);
	if (!bp->bar0) {
		dev_err(&pdev->dev, "Cannot map device registers, aborting\n");
		rc = -ENOMEM;
		goto init_err_release;
	}

	bp->bar1 = pci_ioremap_bar(pdev, 2);
	if (!bp->bar1) {
		dev_err(&pdev->dev, "Cannot map doorbell registers, aborting\n");
		rc = -ENOMEM;
		goto init_err_release;
	}

	bp->bar2 = pci_ioremap_bar(pdev, 4);
	if (!bp->bar2) {
		dev_err(&pdev->dev, "Cannot map bar4 registers, aborting\n");
		rc = -ENOMEM;
		goto init_err_release;
	}

	INIT_WORK(&bp->sp_task, bnxt_sp_task);

	spin_lock_init(&bp->ntp_fltr_lock);

	bp->rx_ring_size = BNXT_DEFAULT_RX_RING_SIZE;
	bp->tx_ring_size = BNXT_DEFAULT_TX_RING_SIZE;

	bp->coal_ticks = BNXT_USEC_TO_COAL_TIMER(4);
	bp->coal_bufs = 20;
	bp->coal_ticks_irq = BNXT_USEC_TO_COAL_TIMER(1);
	bp->coal_bufs_irq = 2;

	init_timer(&bp->timer);
	bp->timer.data = (unsigned long)bp;
	bp->timer.function = bnxt_timer;
	bp->current_interval = BNXT_TIMER_INTERVAL;

	bp->state = BNXT_STATE_CLOSED;

	return 0;

init_err_release:
	if (bp->bar2) {
		pci_iounmap(pdev, bp->bar2);
		bp->bar2 = NULL;
	}

	if (bp->bar1) {
		pci_iounmap(pdev, bp->bar1);
		bp->bar1 = NULL;
	}

	if (bp->bar0) {
		pci_iounmap(pdev, bp->bar0);
		bp->bar0 = NULL;
	}

	pci_release_regions(pdev);

init_err_disable:
	pci_disable_device(pdev);

init_err:
	return rc;
}

/* rtnl_lock held */
static int bnxt_change_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	return 0;
}

/* rtnl_lock held */
static int bnxt_change_mtu(struct net_device *dev, int new_mtu)
{
	struct bnxt *bp = netdev_priv(dev);

	if (new_mtu < 60 || new_mtu > 9000)
		return -EINVAL;

	if (netif_running(dev))
		bnxt_close_nic(bp, false, false);

	dev->mtu = new_mtu;
	bnxt_set_ring_params(bp);

	if (netif_running(dev))
		return bnxt_open_nic(bp, false, false);

	return 0;
}

static int bnxt_setup_tc(struct net_device *dev, u8 tc)
{
	struct bnxt *bp = netdev_priv(dev);

	if (tc > bp->max_tc) {
		netdev_err(dev, "too many traffic classes requested: %d Max supported is %d\n",
			   tc, bp->max_tc);
		return -EINVAL;
	}

	if (netdev_get_num_tc(dev) == tc)
		return 0;

	if (tc) {
		int max_rx_rings, max_tx_rings;

		bnxt_get_max_rings(bp, &max_rx_rings, &max_tx_rings);
		if (bp->tx_nr_rings_per_tc * tc > max_tx_rings)
			return -ENOMEM;
	}

	/* Needs to close the device and do hw resource re-allocations */
	if (netif_running(bp->dev))
		bnxt_close_nic(bp, true, false);

	if (tc) {
		bp->tx_nr_rings = bp->tx_nr_rings_per_tc * tc;
		netdev_set_num_tc(dev, tc);
	} else {
		bp->tx_nr_rings = bp->tx_nr_rings_per_tc;
		netdev_reset_tc(dev);
	}
	bp->cp_nr_rings = max_t(int, bp->tx_nr_rings, bp->rx_nr_rings);
	bp->num_stat_ctxs = bp->cp_nr_rings;

	if (netif_running(bp->dev))
		return bnxt_open_nic(bp, true, false);

	return 0;
}

#ifdef CONFIG_RFS_ACCEL
static bool bnxt_fltr_match(struct bnxt_ntuple_filter *f1,
			    struct bnxt_ntuple_filter *f2)
{
	struct flow_keys *keys1 = &f1->fkeys;
	struct flow_keys *keys2 = &f2->fkeys;

	if (keys1->addrs.v4addrs.src == keys2->addrs.v4addrs.src &&
	    keys1->addrs.v4addrs.dst == keys2->addrs.v4addrs.dst &&
	    keys1->ports.ports == keys2->ports.ports &&
	    keys1->basic.ip_proto == keys2->basic.ip_proto &&
	    keys1->basic.n_proto == keys2->basic.n_proto &&
	    ether_addr_equal(f1->src_mac_addr, f2->src_mac_addr))
		return true;

	return false;
}

static int bnxt_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
			      u16 rxq_index, u32 flow_id)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_ntuple_filter *fltr, *new_fltr;
	struct flow_keys *fkeys;
	struct ethhdr *eth = (struct ethhdr *)skb_mac_header(skb);
	int rc = 0, idx;
	struct hlist_head *head;

	if (skb->encapsulation)
		return -EPROTONOSUPPORT;

	new_fltr = kzalloc(sizeof(*new_fltr), GFP_ATOMIC);
	if (!new_fltr)
		return -ENOMEM;

	fkeys = &new_fltr->fkeys;
	if (!skb_flow_dissect_flow_keys(skb, fkeys, 0)) {
		rc = -EPROTONOSUPPORT;
		goto err_free;
	}

	if ((fkeys->basic.n_proto != htons(ETH_P_IP)) ||
	    ((fkeys->basic.ip_proto != IPPROTO_TCP) &&
	     (fkeys->basic.ip_proto != IPPROTO_UDP))) {
		rc = -EPROTONOSUPPORT;
		goto err_free;
	}

	memcpy(new_fltr->src_mac_addr, eth->h_source, ETH_ALEN);

	idx = skb_get_hash_raw(skb) & BNXT_NTP_FLTR_HASH_MASK;
	head = &bp->ntp_fltr_hash_tbl[idx];
	rcu_read_lock();
	hlist_for_each_entry_rcu(fltr, head, hash) {
		if (bnxt_fltr_match(fltr, new_fltr)) {
			rcu_read_unlock();
			rc = 0;
			goto err_free;
		}
	}
	rcu_read_unlock();

	spin_lock_bh(&bp->ntp_fltr_lock);
	new_fltr->sw_id = bitmap_find_free_region(bp->ntp_fltr_bmap,
						  BNXT_NTP_FLTR_MAX_FLTR, 0);
	if (new_fltr->sw_id < 0) {
		spin_unlock_bh(&bp->ntp_fltr_lock);
		rc = -ENOMEM;
		goto err_free;
	}

	new_fltr->flow_id = flow_id;
	new_fltr->rxq = rxq_index;
	hlist_add_head_rcu(&new_fltr->hash, head);
	bp->ntp_fltr_count++;
	spin_unlock_bh(&bp->ntp_fltr_lock);

	set_bit(BNXT_RX_NTP_FLTR_SP_EVENT, &bp->sp_event);
	schedule_work(&bp->sp_task);

	return new_fltr->sw_id;

err_free:
	kfree(new_fltr);
	return rc;
}

static void bnxt_cfg_ntp_filters(struct bnxt *bp)
{
	int i;

	for (i = 0; i < BNXT_NTP_FLTR_HASH_SIZE; i++) {
		struct hlist_head *head;
		struct hlist_node *tmp;
		struct bnxt_ntuple_filter *fltr;
		int rc;

		head = &bp->ntp_fltr_hash_tbl[i];
		hlist_for_each_entry_safe(fltr, tmp, head, hash) {
			bool del = false;

			if (test_bit(BNXT_FLTR_VALID, &fltr->state)) {
				if (rps_may_expire_flow(bp->dev, fltr->rxq,
							fltr->flow_id,
							fltr->sw_id)) {
					bnxt_hwrm_cfa_ntuple_filter_free(bp,
									 fltr);
					del = true;
				}
			} else {
				rc = bnxt_hwrm_cfa_ntuple_filter_alloc(bp,
								       fltr);
				if (rc)
					del = true;
				else
					set_bit(BNXT_FLTR_VALID, &fltr->state);
			}

			if (del) {
				spin_lock_bh(&bp->ntp_fltr_lock);
				hlist_del_rcu(&fltr->hash);
				bp->ntp_fltr_count--;
				spin_unlock_bh(&bp->ntp_fltr_lock);
				synchronize_rcu();
				clear_bit(fltr->sw_id, bp->ntp_fltr_bmap);
				kfree(fltr);
			}
		}
	}
}

#else

static void bnxt_cfg_ntp_filters(struct bnxt *bp)
{
}

#endif /* CONFIG_RFS_ACCEL */

static void bnxt_add_vxlan_port(struct net_device *dev, sa_family_t sa_family,
				__be16 port)
{
	struct bnxt *bp = netdev_priv(dev);

	if (!netif_running(dev))
		return;

	if (sa_family != AF_INET6 && sa_family != AF_INET)
		return;

	if (bp->vxlan_port_cnt && bp->vxlan_port != port)
		return;

	bp->vxlan_port_cnt++;
	if (bp->vxlan_port_cnt == 1) {
		bp->vxlan_port = port;
		set_bit(BNXT_VXLAN_ADD_PORT_SP_EVENT, &bp->sp_event);
		schedule_work(&bp->sp_task);
	}
}

static void bnxt_del_vxlan_port(struct net_device *dev, sa_family_t sa_family,
				__be16 port)
{
	struct bnxt *bp = netdev_priv(dev);

	if (!netif_running(dev))
		return;

	if (sa_family != AF_INET6 && sa_family != AF_INET)
		return;

	if (bp->vxlan_port_cnt && bp->vxlan_port == port) {
		bp->vxlan_port_cnt--;

		if (bp->vxlan_port_cnt == 0) {
			set_bit(BNXT_VXLAN_DEL_PORT_SP_EVENT, &bp->sp_event);
			schedule_work(&bp->sp_task);
		}
	}
}

static const struct net_device_ops bnxt_netdev_ops = {
	.ndo_open		= bnxt_open,
	.ndo_start_xmit		= bnxt_start_xmit,
	.ndo_stop		= bnxt_close,
	.ndo_get_stats64	= bnxt_get_stats64,
	.ndo_set_rx_mode	= bnxt_set_rx_mode,
	.ndo_do_ioctl		= bnxt_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= bnxt_change_mac_addr,
	.ndo_change_mtu		= bnxt_change_mtu,
	.ndo_fix_features	= bnxt_fix_features,
	.ndo_set_features	= bnxt_set_features,
	.ndo_tx_timeout		= bnxt_tx_timeout,
#ifdef CONFIG_BNXT_SRIOV
	.ndo_get_vf_config	= bnxt_get_vf_config,
	.ndo_set_vf_mac		= bnxt_set_vf_mac,
	.ndo_set_vf_vlan	= bnxt_set_vf_vlan,
	.ndo_set_vf_rate	= bnxt_set_vf_bw,
	.ndo_set_vf_link_state	= bnxt_set_vf_link_state,
	.ndo_set_vf_spoofchk	= bnxt_set_vf_spoofchk,
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= bnxt_poll_controller,
#endif
	.ndo_setup_tc           = bnxt_setup_tc,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer	= bnxt_rx_flow_steer,
#endif
	.ndo_add_vxlan_port	= bnxt_add_vxlan_port,
	.ndo_del_vxlan_port	= bnxt_del_vxlan_port,
#ifdef CONFIG_NET_RX_BUSY_POLL
	.ndo_busy_poll		= bnxt_busy_poll,
#endif
};

static void bnxt_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnxt *bp = netdev_priv(dev);

	if (BNXT_PF(bp))
		bnxt_sriov_disable(bp);

	unregister_netdev(dev);
	cancel_work_sync(&bp->sp_task);
	bp->sp_event = 0;

	bnxt_free_hwrm_resources(bp);
	pci_iounmap(pdev, bp->bar2);
	pci_iounmap(pdev, bp->bar1);
	pci_iounmap(pdev, bp->bar0);
	free_netdev(dev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static int bnxt_probe_phy(struct bnxt *bp)
{
	int rc = 0;
	struct bnxt_link_info *link_info = &bp->link_info;
	char phy_ver[PHY_VER_STR_LEN];

	rc = bnxt_update_link(bp, false);
	if (rc) {
		netdev_err(bp->dev, "Probe phy can't update link (rc: %x)\n",
			   rc);
		return rc;
	}

	/*initialize the ethool setting copy with NVM settings */
	if (BNXT_AUTO_MODE(link_info->auto_mode))
		link_info->autoneg |= BNXT_AUTONEG_SPEED;

	if (link_info->auto_pause_setting & BNXT_LINK_PAUSE_BOTH) {
		if (link_info->auto_pause_setting == BNXT_LINK_PAUSE_BOTH)
			link_info->autoneg |= BNXT_AUTONEG_FLOW_CTRL;
		link_info->req_flow_ctrl = link_info->auto_pause_setting;
	} else if (link_info->force_pause_setting & BNXT_LINK_PAUSE_BOTH) {
		link_info->req_flow_ctrl = link_info->force_pause_setting;
	}
	link_info->req_duplex = link_info->duplex_setting;
	if (link_info->autoneg & BNXT_AUTONEG_SPEED)
		link_info->req_link_speed = link_info->auto_link_speed;
	else
		link_info->req_link_speed = link_info->force_link_speed;
	link_info->advertising = link_info->auto_link_speeds;
	snprintf(phy_ver, PHY_VER_STR_LEN, " ph %d.%d.%d",
		 link_info->phy_ver[0],
		 link_info->phy_ver[1],
		 link_info->phy_ver[2]);
	strcat(bp->fw_ver_str, phy_ver);
	return rc;
}

static int bnxt_get_max_irq(struct pci_dev *pdev)
{
	u16 ctrl;

	if (!pdev->msix_cap)
		return 1;

	pci_read_config_word(pdev, pdev->msix_cap + PCI_MSIX_FLAGS, &ctrl);
	return (ctrl & PCI_MSIX_FLAGS_QSIZE) + 1;
}

void bnxt_get_max_rings(struct bnxt *bp, int *max_rx, int *max_tx)
{
	int max_rings = 0;

	if (BNXT_PF(bp)) {
		*max_tx = bp->pf.max_pf_tx_rings;
		*max_rx = bp->pf.max_pf_rx_rings;
		max_rings = min_t(int, bp->pf.max_irqs, bp->pf.max_cp_rings);
		max_rings = min_t(int, max_rings, bp->pf.max_stat_ctxs);
	} else {
#ifdef CONFIG_BNXT_SRIOV
		*max_tx = bp->vf.max_tx_rings;
		*max_rx = bp->vf.max_rx_rings;
		max_rings = min_t(int, bp->vf.max_irqs, bp->vf.max_cp_rings);
		max_rings = min_t(int, max_rings, bp->vf.max_stat_ctxs);
#endif
	}
	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		*max_rx >>= 1;

	*max_rx = min_t(int, *max_rx, max_rings);
	*max_tx = min_t(int, *max_tx, max_rings);
}

static int bnxt_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int version_printed;
	struct net_device *dev;
	struct bnxt *bp;
	int rc, max_rx_rings, max_tx_rings, max_irqs, dflt_rings;

	if (version_printed++ == 0)
		pr_info("%s", version);

	max_irqs = bnxt_get_max_irq(pdev);
	dev = alloc_etherdev_mq(sizeof(*bp), max_irqs);
	if (!dev)
		return -ENOMEM;

	bp = netdev_priv(dev);

	if (bnxt_vf_pciid(ent->driver_data))
		bp->flags |= BNXT_FLAG_VF;

	if (pdev->msix_cap) {
		bp->flags |= BNXT_FLAG_MSIX_CAP;
		if (BNXT_PF(bp))
			bp->flags |= BNXT_FLAG_RFS;
	}

	rc = bnxt_init_board(pdev, dev);
	if (rc < 0)
		goto init_err_free;

	dev->netdev_ops = &bnxt_netdev_ops;
	dev->watchdog_timeo = BNXT_TX_TIMEOUT;
	dev->ethtool_ops = &bnxt_ethtool_ops;

	pci_set_drvdata(pdev, dev);

	dev->hw_features = NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_SG |
			   NETIF_F_TSO | NETIF_F_TSO6 |
			   NETIF_F_GSO_UDP_TUNNEL | NETIF_F_GSO_GRE |
			   NETIF_F_GSO_IPIP | NETIF_F_GSO_SIT |
			   NETIF_F_RXHASH |
			   NETIF_F_RXCSUM | NETIF_F_LRO | NETIF_F_GRO;

	if (bp->flags & BNXT_FLAG_RFS)
		dev->hw_features |= NETIF_F_NTUPLE;

	dev->hw_enc_features =
			NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_SG |
			NETIF_F_TSO | NETIF_F_TSO6 |
			NETIF_F_GSO_UDP_TUNNEL | NETIF_F_GSO_GRE |
			NETIF_F_GSO_IPIP | NETIF_F_GSO_SIT;
	dev->vlan_features = dev->hw_features | NETIF_F_HIGHDMA;
	dev->hw_features |= NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HW_VLAN_CTAG_TX |
			    NETIF_F_HW_VLAN_STAG_RX | NETIF_F_HW_VLAN_STAG_TX;
	dev->features |= dev->hw_features | NETIF_F_HIGHDMA;
	dev->priv_flags |= IFF_UNICAST_FLT;

#ifdef CONFIG_BNXT_SRIOV
	init_waitqueue_head(&bp->sriov_cfg_wait);
#endif
	rc = bnxt_alloc_hwrm_resources(bp);
	if (rc)
		goto init_err;

	mutex_init(&bp->hwrm_cmd_lock);
	bnxt_hwrm_ver_get(bp);

	rc = bnxt_hwrm_func_drv_rgtr(bp);
	if (rc)
		goto init_err;

	/* Get the MAX capabilities for this function */
	rc = bnxt_hwrm_func_qcaps(bp);
	if (rc) {
		netdev_err(bp->dev, "hwrm query capability failure rc: %x\n",
			   rc);
		rc = -1;
		goto init_err;
	}

	rc = bnxt_hwrm_queue_qportcfg(bp);
	if (rc) {
		netdev_err(bp->dev, "hwrm query qportcfg failure rc: %x\n",
			   rc);
		rc = -1;
		goto init_err;
	}

	bnxt_set_tpa_flags(bp);
	bnxt_set_ring_params(bp);
	dflt_rings = netif_get_num_default_rss_queues();
	if (BNXT_PF(bp)) {
		memcpy(dev->dev_addr, bp->pf.mac_addr, ETH_ALEN);
		bp->pf.max_irqs = max_irqs;
	} else {
#if defined(CONFIG_BNXT_SRIOV)
		memcpy(dev->dev_addr, bp->vf.mac_addr, ETH_ALEN);
		bp->vf.max_irqs = max_irqs;
#endif
	}
	bnxt_get_max_rings(bp, &max_rx_rings, &max_tx_rings);
	bp->rx_nr_rings = min_t(int, dflt_rings, max_rx_rings);
	bp->tx_nr_rings_per_tc = min_t(int, dflt_rings, max_tx_rings);
	bp->tx_nr_rings = bp->tx_nr_rings_per_tc;
	bp->cp_nr_rings = max_t(int, bp->rx_nr_rings, bp->tx_nr_rings);
	bp->num_stat_ctxs = bp->cp_nr_rings;

	if (dev->hw_features & NETIF_F_HW_VLAN_CTAG_RX)
		bp->flags |= BNXT_FLAG_STRIP_VLAN;

	rc = bnxt_probe_phy(bp);
	if (rc)
		goto init_err;

	rc = register_netdev(dev);
	if (rc)
		goto init_err;

	netdev_info(dev, "%s found at mem %lx, node addr %pM\n",
		    board_info[ent->driver_data].name,
		    (long)pci_resource_start(pdev, 0), dev->dev_addr);

	return 0;

init_err:
	pci_iounmap(pdev, bp->bar0);
	pci_release_regions(pdev);
	pci_disable_device(pdev);

init_err_free:
	free_netdev(dev);
	return rc;
}

static struct pci_driver bnxt_pci_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= bnxt_pci_tbl,
	.probe		= bnxt_init_one,
	.remove		= bnxt_remove_one,
#if defined(CONFIG_BNXT_SRIOV)
	.sriov_configure = bnxt_sriov_configure,
#endif
};

module_pci_driver(bnxt_pci_driver);
