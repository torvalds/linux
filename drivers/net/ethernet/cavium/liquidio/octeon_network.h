/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2015 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium, Inc. for more information
 **********************************************************************/

/*!  \file  octeon_network.h
 *   \brief Host NIC Driver: Structure and Macro definitions used by NIC Module.
 */

#ifndef __OCTEON_NETWORK_H__
#define __OCTEON_NETWORK_H__
#include <linux/version.h>
#include <linux/dma-mapping.h>
#include <linux/ptp_clock_kernel.h>

#define LIO_MAX_MTU_SIZE (OCTNET_MAX_FRM_SIZE - OCTNET_FRM_HEADER_SIZE)
#define LIO_MIN_MTU_SIZE 68

struct oct_nic_stats_resp {
	u64     rh;
	struct oct_link_stats stats;
	u64     status;
};

struct oct_nic_stats_ctrl {
	struct completion complete;
	struct net_device *netdev;
};

/** LiquidIO per-interface network private data */
struct lio {
	/** State of the interface. Rx/Tx happens only in the RUNNING state.  */
	atomic_t ifstate;

	/** Octeon Interface index number. This device will be represented as
	 *  oct<ifidx> in the system.
	 */
	int ifidx;

	/** Octeon Input queue to use to transmit for this network interface. */
	int txq;

	/** Octeon Output queue from which pkts arrive
	 * for this network interface.
	 */
	int rxq;

	/** Guards each glist */
	spinlock_t *glist_lock;

	/** Array of gather component linked lists */
	struct list_head *glist;

	/** Pointer to the NIC properties for the Octeon device this network
	 *  interface is associated with.
	 */
	struct octdev_props *octprops;

	/** Pointer to the octeon device structure. */
	struct octeon_device *oct_dev;

	struct net_device *netdev;

	/** Link information sent by the core application for this interface. */
	struct oct_link_info linfo;

	/** counter of link changes */
	u64 link_changes;

	/** Size of Tx queue for this octeon device. */
	u32 tx_qsize;

	/** Size of Rx queue for this octeon device. */
	u32 rx_qsize;

	/** Size of MTU this octeon device. */
	u32 mtu;

	/** msg level flag per interface. */
	u32 msg_enable;

	/** Copy of Interface capabilities: TSO, TSO6, LRO, Chescksums . */
	u64 dev_capability;

	/* Copy of transmit encapsulation capabilities:
	 * TSO, TSO6, Checksums for this device for Kernel
	 * 3.10.0 onwards
	 */
	u64 enc_dev_capability;

	/** Copy of beacaon reg in phy */
	u32 phy_beacon_val;

	/** Copy of ctrl reg in phy */
	u32 led_ctrl_val;

	/* PTP clock information */
	struct ptp_clock_info ptp_info;
	struct ptp_clock *ptp_clock;
	s64 ptp_adjust;

	/* for atomic access to Octeon PTP reg and data struct */
	spinlock_t ptp_lock;

	/* Interface info */
	u32	intf_open;

	/* work queue for  txq status */
	struct cavium_wq	txq_status_wq;
};

#define LIO_SIZE         (sizeof(struct lio))
#define GET_LIO(netdev)  ((struct lio *)netdev_priv(netdev))

/**
 * \brief Enable or disable feature
 * @param netdev    pointer to network device
 * @param cmd       Command that just requires acknowledgment
 * @param param1    Parameter to command
 */
int liquidio_set_feature(struct net_device *netdev, int cmd, u16 param1);

/**
 * \brief Link control command completion callback
 * @param nctrl_ptr pointer to control packet structure
 *
 * This routine is called by the callback function when a ctrl pkt sent to
 * core app completes. The nctrl_ptr contains a copy of the command type
 * and data sent to the core app. This routine is only called if the ctrl
 * pkt was sent successfully to the core app.
 */
void liquidio_link_ctrl_cmd_completion(void *nctrl_ptr);

/**
 * \brief Register ethtool operations
 * @param netdev    pointer to network device
 */
void liquidio_set_ethtool_ops(struct net_device *netdev);

#define SKB_ADJ_MASK  0x3F
#define SKB_ADJ       (SKB_ADJ_MASK + 1)

#define MIN_SKB_SIZE       256 /* 8 bytes and more - 8 bytes for PTP */
#define LIO_RXBUFFER_SZ    2048

static inline void
*recv_buffer_alloc(struct octeon_device *oct,
		   struct octeon_skb_page_info *pg_info)
{
	struct page *page;
	struct sk_buff *skb;
	struct octeon_skb_page_info *skb_pg_info;

	page = alloc_page(GFP_ATOMIC | __GFP_COLD);
	if (unlikely(!page))
		return NULL;

	skb = dev_alloc_skb(MIN_SKB_SIZE + SKB_ADJ);
	if (unlikely(!skb)) {
		__free_page(page);
		pg_info->page = NULL;
		return NULL;
	}

	if ((unsigned long)skb->data & SKB_ADJ_MASK) {
		u32 r = SKB_ADJ - ((unsigned long)skb->data & SKB_ADJ_MASK);

		skb_reserve(skb, r);
	}

	skb_pg_info = ((struct octeon_skb_page_info *)(skb->cb));
	/* Get DMA info */
	pg_info->dma = dma_map_page(&oct->pci_dev->dev, page, 0,
				    PAGE_SIZE, DMA_FROM_DEVICE);

	/* Mapping failed!! */
	if (dma_mapping_error(&oct->pci_dev->dev, pg_info->dma)) {
		__free_page(page);
		dev_kfree_skb_any((struct sk_buff *)skb);
		pg_info->page = NULL;
		return NULL;
	}

	pg_info->page = page;
	pg_info->page_offset = 0;
	skb_pg_info->page = page;
	skb_pg_info->page_offset = 0;
	skb_pg_info->dma = pg_info->dma;

	return (void *)skb;
}

static inline void
*recv_buffer_fast_alloc(u32 size)
{
	struct sk_buff *skb;
	struct octeon_skb_page_info *skb_pg_info;

	skb = dev_alloc_skb(size + SKB_ADJ);
	if (unlikely(!skb))
		return NULL;

	if ((unsigned long)skb->data & SKB_ADJ_MASK) {
		u32 r = SKB_ADJ - ((unsigned long)skb->data & SKB_ADJ_MASK);

		skb_reserve(skb, r);
	}

	skb_pg_info = ((struct octeon_skb_page_info *)(skb->cb));
	skb_pg_info->page = NULL;
	skb_pg_info->page_offset = 0;
	skb_pg_info->dma = 0;

	return skb;
}

static inline int
recv_buffer_recycle(struct octeon_device *oct, void *buf)
{
	struct octeon_skb_page_info *pg_info = buf;

	if (!pg_info->page) {
		dev_err(&oct->pci_dev->dev, "%s: pg_info->page NULL\n",
			__func__);
		return -ENOMEM;
	}

	if (unlikely(page_count(pg_info->page) != 1) ||
	    unlikely(page_to_nid(pg_info->page)	!= numa_node_id())) {
		dma_unmap_page(&oct->pci_dev->dev,
			       pg_info->dma, (PAGE_SIZE << 0),
			       DMA_FROM_DEVICE);
		pg_info->dma = 0;
		pg_info->page = NULL;
		pg_info->page_offset = 0;
		return -ENOMEM;
	}

	/* Flip to other half of the buffer */
	if (pg_info->page_offset == 0)
		pg_info->page_offset = LIO_RXBUFFER_SZ;
	else
		pg_info->page_offset = 0;
	page_ref_inc(pg_info->page);

	return 0;
}

static inline void
*recv_buffer_reuse(struct octeon_device *oct, void *buf)
{
	struct octeon_skb_page_info *pg_info = buf, *skb_pg_info;
	struct sk_buff *skb;

	skb = dev_alloc_skb(MIN_SKB_SIZE + SKB_ADJ);
	if (unlikely(!skb)) {
		dma_unmap_page(&oct->pci_dev->dev,
			       pg_info->dma, (PAGE_SIZE << 0),
			       DMA_FROM_DEVICE);
		return NULL;
	}

	if ((unsigned long)skb->data & SKB_ADJ_MASK) {
		u32 r = SKB_ADJ - ((unsigned long)skb->data & SKB_ADJ_MASK);

		skb_reserve(skb, r);
	}

	skb_pg_info = ((struct octeon_skb_page_info *)(skb->cb));
	skb_pg_info->page = pg_info->page;
	skb_pg_info->page_offset = pg_info->page_offset;
	skb_pg_info->dma = pg_info->dma;

	return skb;
}

static inline void
recv_buffer_destroy(void *buffer, struct octeon_skb_page_info *pg_info)
{
	struct sk_buff *skb = (struct sk_buff *)buffer;

	put_page(pg_info->page);
	pg_info->dma = 0;
	pg_info->page = NULL;
	pg_info->page_offset = 0;

	if (skb)
		dev_kfree_skb_any(skb);
}

static inline void recv_buffer_free(void *buffer)
{
	struct sk_buff *skb = (struct sk_buff *)buffer;
	struct octeon_skb_page_info *pg_info;

	pg_info = ((struct octeon_skb_page_info *)(skb->cb));

	if (pg_info->page) {
		put_page(pg_info->page);
		pg_info->dma = 0;
		pg_info->page = NULL;
		pg_info->page_offset = 0;
	}

	dev_kfree_skb_any((struct sk_buff *)buffer);
}

static inline void
recv_buffer_fast_free(void *buffer)
{
	dev_kfree_skb_any((struct sk_buff *)buffer);
}

static inline void tx_buffer_free(void *buffer)
{
	dev_kfree_skb_any((struct sk_buff *)buffer);
}

#define lio_dma_alloc(oct, size, dma_addr) \
	dma_alloc_coherent(&oct->pci_dev->dev, size, dma_addr, GFP_KERNEL)
#define lio_dma_free(oct, size, virt_addr, dma_addr) \
	dma_free_coherent(&oct->pci_dev->dev, size, virt_addr, dma_addr)

static inline
void *get_rbd(struct sk_buff *skb)
{
	struct octeon_skb_page_info *pg_info;
	unsigned char *va;

	pg_info = ((struct octeon_skb_page_info *)(skb->cb));
	va = page_address(pg_info->page) + pg_info->page_offset;

	return va;
}

static inline u64
lio_map_ring_info(struct octeon_droq *droq, u32 i)
{
	dma_addr_t dma_addr;
	struct octeon_device *oct = droq->oct_dev;

	dma_addr = dma_map_single(&oct->pci_dev->dev, &droq->info_list[i],
				  OCT_DROQ_INFO_SIZE, DMA_FROM_DEVICE);

	WARN_ON(dma_mapping_error(&oct->pci_dev->dev, dma_addr));

	return (u64)dma_addr;
}

static inline void
lio_unmap_ring_info(struct pci_dev *pci_dev,
		    u64 info_ptr, u32 size)
{
	dma_unmap_single(&pci_dev->dev, info_ptr, size, DMA_FROM_DEVICE);
}

static inline u64
lio_map_ring(void *buf)
{
	dma_addr_t dma_addr;

	struct sk_buff *skb = (struct sk_buff *)buf;
	struct octeon_skb_page_info *pg_info;

	pg_info = ((struct octeon_skb_page_info *)(skb->cb));
	if (!pg_info->page) {
		pr_err("%s: pg_info->page NULL\n", __func__);
		WARN_ON(1);
	}

	/* Get DMA info */
	dma_addr = pg_info->dma;
	if (!pg_info->dma) {
		pr_err("%s: ERROR it should be already available\n",
		       __func__);
		WARN_ON(1);
	}
	dma_addr += pg_info->page_offset;

	return (u64)dma_addr;
}

static inline void
lio_unmap_ring(struct pci_dev *pci_dev,
	       u64 buf_ptr)

{
	dma_unmap_page(&pci_dev->dev,
		       buf_ptr, (PAGE_SIZE << 0),
		       DMA_FROM_DEVICE);
}

static inline void *octeon_fast_packet_alloc(u32 size)
{
	return recv_buffer_fast_alloc(size);
}

static inline void octeon_fast_packet_next(struct octeon_droq *droq,
					   struct sk_buff *nicbuf,
					   int copy_len,
					   int idx)
{
	memcpy(skb_put(nicbuf, copy_len),
	       get_rbd(droq->recv_buf_list[idx].buffer), copy_len);
}

#endif
