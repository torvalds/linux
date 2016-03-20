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

	/** Guards the glist */
	spinlock_t lock;

	/** Linked list of gather components */
	struct list_head glist;

	/** Pointer to the NIC properties for the Octeon device this network
	 *  interface is associated with.
	 */
	struct octdev_props *octprops;

	/** Pointer to the octeon device structure. */
	struct octeon_device *oct_dev;

	struct net_device *netdev;

	/** Link information sent by the core application for this interface. */
	struct oct_link_info linfo;

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
 */
int liquidio_set_feature(struct net_device *netdev, int cmd);

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

static inline void
*recv_buffer_alloc(struct octeon_device *oct __attribute__((unused)),
		   u32 q_no __attribute__((unused)), u32 size)
{
#define SKB_ADJ_MASK  0x3F
#define SKB_ADJ       (SKB_ADJ_MASK + 1)

	struct sk_buff *skb = dev_alloc_skb(size + SKB_ADJ);

	if ((unsigned long)skb->data & SKB_ADJ_MASK) {
		u32 r = SKB_ADJ - ((unsigned long)skb->data & SKB_ADJ_MASK);

		skb_reserve(skb, r);
	}

	return (void *)skb;
}

static inline void recv_buffer_free(void *buffer)
{
	dev_kfree_skb_any((struct sk_buff *)buffer);
}

#define lio_dma_alloc(oct, size, dma_addr) \
	dma_alloc_coherent(&oct->pci_dev->dev, size, dma_addr, GFP_KERNEL)
#define lio_dma_free(oct, size, virt_addr, dma_addr) \
	dma_free_coherent(&oct->pci_dev->dev, size, virt_addr, dma_addr)

#define   get_rbd(ptr)      (((struct sk_buff *)(ptr))->data)

static inline u64
lio_map_ring_info(struct octeon_droq *droq, u32 i)
{
	dma_addr_t dma_addr;
	struct octeon_device *oct = droq->oct_dev;

	dma_addr = dma_map_single(&oct->pci_dev->dev, &droq->info_list[i],
				  OCT_DROQ_INFO_SIZE, DMA_FROM_DEVICE);

	BUG_ON(dma_mapping_error(&oct->pci_dev->dev, dma_addr));

	return (u64)dma_addr;
}

static inline void
lio_unmap_ring_info(struct pci_dev *pci_dev,
		    u64 info_ptr, u32 size)
{
	dma_unmap_single(&pci_dev->dev, info_ptr, size, DMA_FROM_DEVICE);
}

static inline u64
lio_map_ring(struct pci_dev *pci_dev,
	     void *buf, u32 size)
{
	dma_addr_t dma_addr;

	dma_addr = dma_map_single(&pci_dev->dev, get_rbd(buf), size,
				  DMA_FROM_DEVICE);

	BUG_ON(dma_mapping_error(&pci_dev->dev, dma_addr));

	return (u64)dma_addr;
}

static inline void
lio_unmap_ring(struct pci_dev *pci_dev,
	       u64 buf_ptr, u32 size)
{
	dma_unmap_single(&pci_dev->dev,
			 buf_ptr, size,
			 DMA_FROM_DEVICE);
}

static inline void *octeon_fast_packet_alloc(struct octeon_device *oct,
					     struct octeon_droq *droq,
					     u32 q_no, u32 size)
{
	return recv_buffer_alloc(oct, q_no, size);
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
