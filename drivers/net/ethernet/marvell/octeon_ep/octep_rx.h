/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#ifndef _OCTEP_RX_H_
#define _OCTEP_RX_H_

/* struct octep_oq_desc_hw - Octeon Hardware OQ descriptor format.
 *
 * The descriptor ring is made of descriptors which have 2 64-bit values:
 *
 *   @buffer_ptr: DMA address of the skb->data
 *   @info_ptr:  DMA address of host memory, used to update pkt count by hw.
 *               This is currently unused to save pci writes.
 */
struct octep_oq_desc_hw {
	dma_addr_t buffer_ptr;
	u64 info_ptr;
};
static_assert(sizeof(struct octep_oq_desc_hw) == 16);

#define OCTEP_OQ_DESC_SIZE    (sizeof(struct octep_oq_desc_hw))

#define OCTEP_CSUM_L4_VERIFIED 0x1
#define OCTEP_CSUM_IP_VERIFIED 0x2
#define OCTEP_CSUM_VERIFIED (OCTEP_CSUM_L4_VERIFIED | OCTEP_CSUM_IP_VERIFIED)

/* Extended Response Header in packet data received from Hardware.
 * Includes metadata like checksum status.
 * this is valid only if hardware/firmware published support for this.
 * This is at offset 0 of packet data (skb->data).
 */
struct octep_oq_resp_hw_ext {
	/* Reserved. */
	u64 reserved:62;

	/* checksum verified. */
	u64 csum_verified:2;
};
static_assert(sizeof(struct octep_oq_resp_hw_ext) == 8);

#define  OCTEP_OQ_RESP_HW_EXT_SIZE   (sizeof(struct octep_oq_resp_hw_ext))

/* Length of Rx packet DMA'ed by Octeon to Host.
 * this is in bigendian; so need to be converted to cpu endian.
 * Octeon writes this at the beginning of Rx buffer (skb->data).
 */
struct octep_oq_resp_hw {
	/* The Length of the packet. */
	__be64 length;
};
static_assert(sizeof(struct octep_oq_resp_hw) == 8);

#define OCTEP_OQ_RESP_HW_SIZE   (sizeof(struct octep_oq_resp_hw))

/* Pointer to data buffer.
 * Driver keeps a pointer to the data buffer that it made available to
 * the Octeon device. Since the descriptor ring keeps physical (bus)
 * addresses, this field is required for the driver to keep track of
 * the virtual address pointers. The fields are operated by
 * OS-dependent routines.
 */
struct octep_rx_buffer {
	struct page *page;

	/* length from rx hardware descriptor after converting to cpu endian */
	u64 len;
};

#define OCTEP_OQ_RECVBUF_SIZE    (sizeof(struct octep_rx_buffer))

/* Output Queue statistics. Each output queue has four stats fields. */
struct octep_oq_stats {
	/* Number of packets received from the Device. */
	u64 packets;

	/* Number of bytes received from the Device. */
	u64 bytes;

	/* Number of times failed to allocate buffers. */
	u64 alloc_failures;
};

#define OCTEP_OQ_STATS_SIZE   (sizeof(struct octep_oq_stats))

/* Hardware interface Rx statistics */
struct octep_iface_rx_stats {
	/* Received packets */
	u64 pkts;

	/* Octets of received packets */
	u64 octets;

	/* Received PAUSE and Control packets */
	u64 pause_pkts;

	/* Received PAUSE and Control octets */
	u64 pause_octets;

	/* Filtered DMAC0 packets */
	u64 dmac0_pkts;

	/* Filtered DMAC0 octets */
	u64 dmac0_octets;

	/* Packets dropped due to RX FIFO full */
	u64 dropped_pkts_fifo_full;

	/* Octets dropped due to RX FIFO full */
	u64 dropped_octets_fifo_full;

	/* Error packets */
	u64 err_pkts;

	/* Filtered DMAC1 packets */
	u64 dmac1_pkts;

	/* Filtered DMAC1 octets */
	u64 dmac1_octets;

	/* NCSI-bound packets dropped */
	u64 ncsi_dropped_pkts;

	/* NCSI-bound octets dropped */
	u64 ncsi_dropped_octets;

	/* Multicast packets received. */
	u64 mcast_pkts;

	/* Broadcast packets received. */
	u64 bcast_pkts;

};

/* The Descriptor Ring Output Queue structure.
 * This structure has all the information required to implement a
 * Octeon OQ.
 */
struct octep_oq {
	u32 q_no;

	struct octep_device *octep_dev;
	struct net_device *netdev;
	struct device *dev;

	struct napi_struct *napi;

	/* The receive buffer list. This list has the virtual addresses
	 * of the buffers.
	 */
	struct octep_rx_buffer *buff_info;

	/* Pointer to the mapped packet credit register.
	 * Host writes number of info/buffer ptrs available to this register
	 */
	u8 __iomem *pkts_credit_reg;

	/* Pointer to the mapped packet sent register.
	 * Octeon writes the number of packets DMA'ed to host memory
	 * in this register.
	 */
	u8 __iomem *pkts_sent_reg;

	/* Statistics for this OQ. */
	struct octep_oq_stats stats;

	/* Packets pending to be processed */
	u32 pkts_pending;
	u32 last_pkt_count;

	/* Index in the ring where the driver should read the next packet */
	u32 host_read_idx;

	/* Number of  descriptors in this ring. */
	u32 max_count;
	u32 ring_size_mask;

	/* The number of descriptors pending refill. */
	u32 refill_count;

	/* Index in the ring where the driver will refill the
	 * descriptor's buffer
	 */
	u32 host_refill_idx;
	u32 refill_threshold;

	/* The size of each buffer pointed by the buffer pointer. */
	u32 buffer_size;
	u32 max_single_buffer_size;

	/* The 8B aligned descriptor ring starts at this address. */
	struct octep_oq_desc_hw *desc_ring;

	/* DMA mapped address of the OQ descriptor ring. */
	dma_addr_t desc_ring_dma;
};

#define OCTEP_OQ_SIZE   (sizeof(struct octep_oq))
#endif	/* _OCTEP_RX_H_ */
