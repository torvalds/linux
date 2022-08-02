/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#ifndef _OCTEP_TX_H_
#define _OCTEP_TX_H_

#define IQ_SEND_OK          0
#define IQ_SEND_STOP        1
#define IQ_SEND_FAILED     -1

#define TX_BUFTYPE_NONE          0
#define TX_BUFTYPE_NET           1
#define TX_BUFTYPE_NET_SG        2
#define NUM_TX_BUFTYPES          3

/* Hardware format for Scatter/Gather list */
struct octep_tx_sglist_desc {
	u16 len[4];
	dma_addr_t dma_ptr[4];
};

/* Each Scatter/Gather entry sent to hardwar hold four pointers.
 * So, number of entries required is (MAX_SKB_FRAGS + 1)/4, where '+1'
 * is for main skb which also goes as a gather buffer to Octeon hardware.
 * To allocate sufficient SGLIST entries for a packet with max fragments,
 * align by adding 3 before calcuating max SGLIST entries per packet.
 */
#define OCTEP_SGLIST_ENTRIES_PER_PKT ((MAX_SKB_FRAGS + 1 + 3) / 4)
#define OCTEP_SGLIST_SIZE_PER_PKT \
	(OCTEP_SGLIST_ENTRIES_PER_PKT * sizeof(struct octep_tx_sglist_desc))

struct octep_tx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	struct octep_tx_sglist_desc *sglist;
	dma_addr_t sglist_dma;
	u8 gather;
};

#define OCTEP_IQ_TXBUFF_INFO_SIZE (sizeof(struct octep_tx_buffer))

/* Hardware interface Tx statistics */
struct octep_iface_tx_stats {
	/* Packets dropped due to excessive collisions */
	u64 xscol;

	/* Packets dropped due to excessive deferral */
	u64 xsdef;

	/* Packets sent that experienced multiple collisions before successful
	 * transmission
	 */
	u64 mcol;

	/* Packets sent that experienced a single collision before successful
	 * transmission
	 */
	u64 scol;

	/* Total octets sent on the interface */
	u64 octs;

	/* Total frames sent on the interface */
	u64 pkts;

	/* Packets sent with an octet count < 64 */
	u64 hist_lt64;

	/* Packets sent with an octet count == 64 */
	u64 hist_eq64;

	/* Packets sent with an octet count of 65–127 */
	u64 hist_65to127;

	/* Packets sent with an octet count of 128–255 */
	u64 hist_128to255;

	/* Packets sent with an octet count of 256–511 */
	u64 hist_256to511;

	/* Packets sent with an octet count of 512–1023 */
	u64 hist_512to1023;

	/* Packets sent with an octet count of 1024-1518 */
	u64 hist_1024to1518;

	/* Packets sent with an octet count of > 1518 */
	u64 hist_gt1518;

	/* Packets sent to a broadcast DMAC */
	u64 bcst;

	/* Packets sent to the multicast DMAC */
	u64 mcst;

	/* Packets sent that experienced a transmit underflow and were
	 * truncated
	 */
	u64 undflw;

	/* Control/PAUSE packets sent */
	u64 ctl;
};

/* Input Queue statistics. Each input queue has four stats fields. */
struct octep_iq_stats {
	/* Instructions posted to this queue. */
	u64 instr_posted;

	/* Instructions copied by hardware for processing. */
	u64 instr_completed;

	/* Instructions that could not be processed. */
	u64 instr_dropped;

	/* Bytes sent through this queue. */
	u64 bytes_sent;

	/* Gather entries sent through this queue. */
	u64 sgentry_sent;

	/* Number of transmit failures due to TX_BUSY */
	u64 tx_busy;

	/* Number of times the queue is restarted */
	u64 restart_cnt;
};

/* The instruction (input) queue.
 * The input queue is used to post raw (instruction) mode data or packet
 * data to Octeon device from the host. Each input queue (up to 4) for
 * a Octeon device has one such structure to represent it.
 */
struct octep_iq {
	u32 q_no;

	struct octep_device *octep_dev;
	struct net_device *netdev;
	struct device *dev;
	struct netdev_queue *netdev_q;

	/* Index in input ring where driver should write the next packet */
	u16 host_write_index;

	/* Index in input ring where Octeon is expected to read next packet */
	u16 octep_read_index;

	/* This index aids in finding the window in the queue where Octeon
	 * has read the commands.
	 */
	u16 flush_index;

	/* Statistics for this input queue. */
	struct octep_iq_stats stats;

	/* This field keeps track of the instructions pending in this queue. */
	atomic_t instr_pending;

	/* Pointer to the Virtual Base addr of the input ring. */
	struct octep_tx_desc_hw *desc_ring;

	/* DMA mapped base address of the input descriptor ring. */
	dma_addr_t desc_ring_dma;

	/* Info of Tx buffers pending completion. */
	struct octep_tx_buffer *buff_info;

	/* Base pointer to Scatter/Gather lists for all ring descriptors. */
	struct octep_tx_sglist_desc *sglist;

	/* DMA mapped addr of Scatter Gather Lists */
	dma_addr_t sglist_dma;

	/* Octeon doorbell register for the ring. */
	u8 __iomem *doorbell_reg;

	/* Octeon instruction count register for this ring. */
	u8 __iomem *inst_cnt_reg;

	/* interrupt level register for this ring */
	u8 __iomem *intr_lvl_reg;

	/* Maximum no. of instructions in this queue. */
	u32 max_count;
	u32 ring_size_mask;

	u32 pkt_in_done;
	u32 pkts_processed;

	u32 status;

	/* Number of instructions pending to be posted to Octeon. */
	u32 fill_cnt;

	/* The max. number of instructions that can be held pending by the
	 * driver before ringing doorbell.
	 */
	u32 fill_threshold;
};

/* Hardware Tx Instruction Header */
struct octep_instr_hdr {
	/* Data Len */
	u64 tlen:16;

	/* Reserved */
	u64 rsvd:20;

	/* PKIND for SDP */
	u64 pkind:6;

	/* Front Data size */
	u64 fsz:6;

	/* No. of entries in gather list */
	u64 gsz:14;

	/* Gather indicator 1=gather*/
	u64 gather:1;

	/* Reserved3 */
	u64 reserved3:1;
};

/* Hardware Tx completion response header */
struct octep_instr_resp_hdr {
	/* Request ID  */
	u64 rid:16;

	/* PCIe port to use for response */
	u64 pcie_port:3;

	/* Scatter indicator  1=scatter */
	u64 scatter:1;

	/* Size of Expected result OR no. of entries in scatter list */
	u64 rlenssz:14;

	/* Desired destination port for result */
	u64 dport:6;

	/* Opcode Specific parameters */
	u64 param:8;

	/* Opcode for the return packet  */
	u64 opcode:16;
};

/* 64-byte Tx instruction format.
 * Format of instruction for a 64-byte mode input queue.
 *
 * only first 16-bytes (dptr and ih) are mandatory; rest are optional
 * and filled by the driver based on firmware/hardware capabilities.
 * These optional headers together called Front Data and its size is
 * described by ih->fsz.
 */
struct octep_tx_desc_hw {
	/* Pointer where the input data is available. */
	u64 dptr;

	/* Instruction Header. */
	union {
		struct octep_instr_hdr ih;
		u64 ih64;
	};

	/* Pointer where the response for a RAW mode packet will be written
	 * by Octeon.
	 */
	u64 rptr;

	/* Input Instruction Response Header. */
	struct octep_instr_resp_hdr irh;

	/* Additional headers available in a 64-byte instruction. */
	u64 exhdr[4];
};

#define OCTEP_IQ_DESC_SIZE (sizeof(struct octep_tx_desc_hw))
#endif /* _OCTEP_TX_H_ */
