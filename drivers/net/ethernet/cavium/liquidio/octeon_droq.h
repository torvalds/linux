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

/*!  \file  octeon_droq.h
 *   \brief Implementation of Octeon Output queues. "Output" is with
 *   respect to the Octeon device on the NIC. From this driver's point of
 *   view they are ingress queues.
 */

#ifndef __OCTEON_DROQ_H__
#define __OCTEON_DROQ_H__

/* Default number of packets that will be processed in one iteration. */
#define MAX_PACKET_BUDGET 0xFFFFFFFF

/** Octeon descriptor format.
 *  The descriptor ring is made of descriptors which have 2 64-bit values:
 *  -# Physical (bus) address of the data buffer.
 *  -# Physical (bus) address of a octeon_droq_info structure.
 *  The Octeon device DMA's incoming packets and its information at the address
 *  given by these descriptor fields.
 */
struct octeon_droq_desc {
	/** The buffer pointer */
	u64 buffer_ptr;

	/** The Info pointer */
	u64 info_ptr;
};

#define OCT_DROQ_DESC_SIZE    (sizeof(struct octeon_droq_desc))

/** Information about packet DMA'ed by Octeon.
 *  The format of the information available at Info Pointer after Octeon
 *  has posted a packet. Not all descriptors have valid information. Only
 *  the Info field of the first descriptor for a packet has information
 *  about the packet.
 */
struct octeon_droq_info {
	/** The Output Receive Header. */
	union octeon_rh rh;

	/** The Length of the packet. */
	u64 length;
};

#define OCT_DROQ_INFO_SIZE   (sizeof(struct octeon_droq_info))

struct octeon_skb_page_info {
	/* DMA address for the page */
	dma_addr_t dma;

	/* Page for the rx dma  **/
	struct page *page;

	/** which offset into page */
	unsigned int page_offset;
};

/** Pointer to data buffer.
 *  Driver keeps a pointer to the data buffer that it made available to
 *  the Octeon device. Since the descriptor ring keeps physical (bus)
 *  addresses, this field is required for the driver to keep track of
 *  the virtual address pointers.
*/
struct octeon_recv_buffer {
	/** Packet buffer, including metadata. */
	void *buffer;

	/** Data in the packet buffer.  */
	u8 *data;

	/** pg_info **/
	struct octeon_skb_page_info pg_info;
};

#define OCT_DROQ_RECVBUF_SIZE    (sizeof(struct octeon_recv_buffer))

/** Output Queue statistics. Each output queue has four stats fields. */
struct oct_droq_stats {
	/** Number of packets received in this queue. */
	u64 pkts_received;

	/** Bytes received by this queue. */
	u64 bytes_received;

	/** Packets dropped due to no dispatch function. */
	u64 dropped_nodispatch;

	/** Packets dropped due to no memory available. */
	u64 dropped_nomem;

	/** Packets dropped due to large number of pkts to process. */
	u64 dropped_toomany;

	/** Number of packets  sent to stack from this queue. */
	u64 rx_pkts_received;

	/** Number of Bytes sent to stack from this queue. */
	u64 rx_bytes_received;

	/** Num of Packets dropped due to receive path failures. */
	u64 rx_dropped;

	/** Num of vxlan packets received; */
	u64 rx_vxlan;

	/** Num of failures of recv_buffer_alloc() */
	u64 rx_alloc_failure;

};

#define POLL_EVENT_INTR_ARRIVED  1
#define POLL_EVENT_PROCESS_PKTS  2
#define POLL_EVENT_PENDING_PKTS  3
#define POLL_EVENT_ENABLE_INTR   4

/* The maximum number of buffers that can be dispatched from the
 * output/dma queue. Set to 64 assuming 1K buffers in DROQ and the fact that
 * max packet size from DROQ is 64K.
 */
#define    MAX_RECV_BUFS    64

/** Receive Packet format used when dispatching output queue packets
 *  with non-raw opcodes.
 *  The received packet will be sent to the upper layers using this
 *  structure which is passed as a parameter to the dispatch function
 */
struct octeon_recv_pkt {
	/**  Number of buffers in this received packet */
	u16 buffer_count;

	/** Id of the device that is sending the packet up */
	u16 octeon_id;

	/** Length of data in the packet buffer */
	u32 length;

	/** The receive header */
	union octeon_rh rh;

	/** Pointer to the OS-specific packet buffer */
	void *buffer_ptr[MAX_RECV_BUFS];

	/** Size of the buffers pointed to by ptr's in buffer_ptr */
	u32 buffer_size[MAX_RECV_BUFS];
};

#define OCT_RECV_PKT_SIZE    (sizeof(struct octeon_recv_pkt))

/** The first parameter of a dispatch function.
 *  For a raw mode opcode, the driver dispatches with the device
 *  pointer in this structure.
 *  For non-raw mode opcode, the driver dispatches the recv_pkt
 *  created to contain the buffers with data received from Octeon.
 *  ---------------------
 *  |     *recv_pkt ----|---
 *  |-------------------|   |
 *  | 0 or more bytes   |   |
 *  | reserved by driver|   |
 *  |-------------------|<-/
 *  | octeon_recv_pkt   |
 *  |                   |
 *  |___________________|
 */
struct octeon_recv_info {
	void *rsvd;
	struct octeon_recv_pkt *recv_pkt;
};

#define  OCT_RECV_INFO_SIZE    (sizeof(struct octeon_recv_info))

/** Allocate a recv_info structure. The recv_pkt pointer in the recv_info
 *  structure is filled in before this call returns.
 *  @param extra_bytes - extra bytes to be allocated at the end of the recv info
 *                       structure.
 *  @return - pointer to a newly allocated recv_info structure.
 */
static inline struct octeon_recv_info *octeon_alloc_recv_info(int extra_bytes)
{
	struct octeon_recv_info *recv_info;
	u8 *buf;

	buf = kmalloc(OCT_RECV_PKT_SIZE + OCT_RECV_INFO_SIZE +
		      extra_bytes, GFP_ATOMIC);
	if (!buf)
		return NULL;

	recv_info = (struct octeon_recv_info *)buf;
	recv_info->recv_pkt =
		(struct octeon_recv_pkt *)(buf + OCT_RECV_INFO_SIZE);
	recv_info->rsvd = NULL;
	if (extra_bytes)
		recv_info->rsvd = buf + OCT_RECV_INFO_SIZE + OCT_RECV_PKT_SIZE;

	return recv_info;
}

/** Free a recv_info structure.
 *  @param recv_info - Pointer to receive_info to be freed
 */
static inline void octeon_free_recv_info(struct octeon_recv_info *recv_info)
{
	kfree(recv_info);
}

typedef int (*octeon_dispatch_fn_t)(struct octeon_recv_info *, void *);

/** Used by NIC module to register packet handler and to get device
 * information for each octeon device.
 */
struct octeon_droq_ops {
	/** This registered function will be called by the driver with
	 *  the octeon id, pointer to buffer from droq and length of
	 *  data in the buffer. The receive header gives the port
	 *  number to the caller.  Function pointer is set by caller.
	 */
	void (*fptr)(u32, void *, u32, union octeon_rh *, void *, void *);
	void *farg;

	/* This function will be called by the driver for all NAPI related
	 * events. The first param is the octeon id. The second param is the
	 * output queue number. The third is the NAPI event that occurred.
	 */
	void (*napi_fn)(void *);

	u32 poll_mode;

	/** Flag indicating if the DROQ handler should drop packets that
	 *  it cannot handle in one iteration. Set by caller.
	 */
	u32 drop_on_max;
};

/** The Descriptor Ring Output Queue structure.
 *  This structure has all the information required to implement a
 *  Octeon DROQ.
 */
struct octeon_droq {
	/** A spinlock to protect access to this ring. */
	spinlock_t lock;

	u32 q_no;

	u32 pkt_count;

	struct octeon_droq_ops ops;

	struct octeon_device *oct_dev;

	/** The 8B aligned descriptor ring starts at this address. */
	struct octeon_droq_desc *desc_ring;

	/** Index in the ring where the driver should read the next packet */
	u32 read_idx;

	/** Index in the ring where Octeon will write the next packet */
	u32 write_idx;

	/** Index in the ring where the driver will refill the descriptor's
	 * buffer
	 */
	u32 refill_idx;

	/** Packets pending to be processed */
	atomic_t pkts_pending;

	/** Number of  descriptors in this ring. */
	u32 max_count;

	/** The number of descriptors pending refill. */
	u32 refill_count;

	u32 pkts_per_intr;
	u32 refill_threshold;

	/** The max number of descriptors in DROQ without a buffer.
	 * This field is used to keep track of empty space threshold. If the
	 * refill_count reaches this value, the DROQ cannot accept a max-sized
	 * (64K) packet.
	 */
	u32 max_empty_descs;

	/** The 8B aligned info ptrs begin from this address. */
	struct octeon_droq_info *info_list;

	/** The receive buffer list. This list has the virtual addresses of the
	 * buffers.
	 */
	struct octeon_recv_buffer *recv_buf_list;

	/** The size of each buffer pointed by the buffer pointer. */
	u32 buffer_size;

	/** Pointer to the mapped packet credit register.
	 * Host writes number of info/buffer ptrs available to this register
	 */
	void  __iomem *pkts_credit_reg;

	/** Pointer to the mapped packet sent register.
	 * Octeon writes the number of packets DMA'ed to host memory
	 * in this register.
	 */
	void __iomem *pkts_sent_reg;

	struct list_head dispatch_list;

	/** Statistics for this DROQ. */
	struct oct_droq_stats stats;

	/** DMA mapped address of the DROQ descriptor ring. */
	size_t desc_ring_dma;

	/** Info ptr list are allocated at this virtual address. */
	size_t info_base_addr;

	/** DMA mapped address of the info list */
	size_t info_list_dma;

	/** Allocated size of info list. */
	u32 info_alloc_size;

	/** application context */
	void *app_ctx;

	struct napi_struct napi;

	u32 cpu_id;

	struct call_single_data csd;
};

#define OCT_DROQ_SIZE   (sizeof(struct octeon_droq))

/**
 *  Allocates space for the descriptor ring for the droq and sets the
 *   base addr, num desc etc in Octeon registers.
 *
 * @param  oct_dev    - pointer to the octeon device structure
 * @param  q_no       - droq no. ranges from 0 - 3.
 * @param app_ctx     - pointer to application context
 * @return Success: 0    Failure: 1
*/
int octeon_init_droq(struct octeon_device *oct_dev,
		     u32 q_no,
		     u32 num_descs,
		     u32 desc_size,
		     void *app_ctx);

/**
 *  Frees the space for descriptor ring for the droq.
 *
 *  @param oct_dev - pointer to the octeon device structure
 *  @param q_no    - droq no. ranges from 0 - 3.
 *  @return:    Success: 0    Failure: 1
*/
int octeon_delete_droq(struct octeon_device *oct_dev, u32 q_no);

/** Register a change in droq operations. The ops field has a pointer to a
 * function which will called by the DROQ handler for all packets arriving
 * on output queues given by q_no irrespective of the type of packet.
 * The ops field also has a flag which if set tells the DROQ handler to
 * drop packets if it receives more than what it can process in one
 * invocation of the handler.
 * @param oct       - octeon device
 * @param q_no      - octeon output queue number (0 <= q_no <= MAX_OCTEON_DROQ-1
 * @param ops       - the droq_ops settings for this queue
 * @return          - 0 on success, -ENODEV or -EINVAL on error.
 */
int
octeon_register_droq_ops(struct octeon_device *oct,
			 u32 q_no,
			 struct octeon_droq_ops *ops);

/** Resets the function pointer and flag settings made by
 * octeon_register_droq_ops(). After this routine is called, the DROQ handler
 * will lookup dispatch function for each arriving packet on the output queue
 * given by q_no.
 * @param oct       - octeon device
 * @param q_no      - octeon output queue number (0 <= q_no <= MAX_OCTEON_DROQ-1
 * @return          - 0 on success, -ENODEV or -EINVAL on error.
 */
int octeon_unregister_droq_ops(struct octeon_device *oct, u32 q_no);

/**   Register a dispatch function for a opcode/subcode. The driver will call
 *    this dispatch function when it receives a packet with the given
 *    opcode/subcode in its output queues along with the user specified
 *    argument.
 *    @param  oct        - the octeon device to register with.
 *    @param  opcode     - the opcode for which the dispatch will be registered.
 *    @param  subcode    - the subcode for which the dispatch will be registered
 *    @param  fn         - the dispatch function.
 *    @param  fn_arg     - user specified that will be passed along with the
 *                         dispatch function by the driver.
 *    @return Success: 0; Failure: 1
 */
int octeon_register_dispatch_fn(struct octeon_device *oct,
				u16 opcode,
				u16 subcode,
				octeon_dispatch_fn_t fn, void *fn_arg);

void octeon_droq_print_stats(void);

u32 octeon_droq_check_hw_for_pkts(struct octeon_droq *droq);

int octeon_create_droq(struct octeon_device *oct, u32 q_no,
		       u32 num_descs, u32 desc_size, void *app_ctx);

int octeon_droq_process_packets(struct octeon_device *oct,
				struct octeon_droq *droq,
				u32 budget);

int octeon_process_droq_poll_cmd(struct octeon_device *oct, u32 q_no,
				 int cmd, u32 arg);

#endif	/*__OCTEON_DROQ_H__ */
