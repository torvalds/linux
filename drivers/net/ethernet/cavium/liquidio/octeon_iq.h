/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
/*!  \file  octeon_iq.h
 *   \brief Host Driver: Implementation of Octeon input queues. "Input" is
 *   with respect to the Octeon device on the NIC. From this driver's
 *   point of view they are egress queues.
 */

#ifndef __OCTEON_IQ_H__
#define  __OCTEON_IQ_H__

#define IQ_STATUS_RUNNING   1

#define IQ_SEND_OK          0
#define IQ_SEND_STOP        1
#define IQ_SEND_FAILED     -1

/*-------------------------  INSTRUCTION QUEUE --------------------------*/

/* \cond */

#define REQTYPE_NONE                 0
#define REQTYPE_NORESP_NET           1
#define REQTYPE_NORESP_NET_SG        2
#define REQTYPE_RESP_NET             3
#define REQTYPE_RESP_NET_SG          4
#define REQTYPE_SOFT_COMMAND         5
#define REQTYPE_LAST                 5

struct octeon_request_list {
	u32 reqtype;
	void *buf;
};

/* \endcond */

/** Input Queue statistics. Each input queue has four stats fields. */
struct oct_iq_stats {
	u64 instr_posted; /**< Instructions posted to this queue. */
	u64 instr_processed; /**< Instructions processed in this queue. */
	u64 instr_dropped; /**< Instructions that could not be processed */
	u64 bytes_sent;  /**< Bytes sent through this queue. */
	u64 sgentry_sent;/**< Gather entries sent through this queue. */
	u64 tx_done;/**< Num of packets sent to network. */
	u64 tx_iq_busy;/**< Numof times this iq was found to be full. */
	u64 tx_dropped;/**< Numof pkts dropped dueto xmitpath errors. */
	u64 tx_tot_bytes;/**< Total count of bytes sento to network. */
	u64 tx_gso;  /* count of tso */
	u64 tx_vxlan; /* tunnel */
	u64 tx_dmamap_fail;
	u64 tx_restart;
};

#define OCT_IQ_STATS_SIZE   (sizeof(struct oct_iq_stats))

/** The instruction (input) queue.
 *  The input queue is used to post raw (instruction) mode data or packet
 *  data to Octeon device from the host. Each input queue (upto 4) for
 *  a Octeon device has one such structure to represent it.
 */
struct octeon_instr_queue {
	struct octeon_device *oct_dev;

	/** A spinlock to protect access to the input ring.  */
	spinlock_t lock;

	/** A spinlock to protect while posting on the ring.  */
	spinlock_t post_lock;

	u32 pkt_in_done;

	/** A spinlock to protect access to the input ring.*/
	spinlock_t iq_flush_running_lock;

	/** Flag that indicates if the queue uses 64 byte commands. */
	u32 iqcmd_64B:1;

	/** Queue info. */
	union oct_txpciq txpciq;

	u32 rsvd:17;

	/* Controls whether extra flushing of IQ is done on Tx */
	u32 do_auto_flush:1;

	u32 status:8;

	/** Maximum no. of instructions in this queue. */
	u32 max_count;

	/** Index in input ring where the driver should write the next packet */
	u32 host_write_index;

	/** Index in input ring where Octeon is expected to read the next
	 * packet.
	 */
	u32 octeon_read_index;

	/** This index aids in finding the window in the queue where Octeon
	 *  has read the commands.
	 */
	u32 flush_index;

	/** This field keeps track of the instructions pending in this queue. */
	atomic_t instr_pending;

	u32 reset_instr_cnt;

	/** Pointer to the Virtual Base addr of the input ring. */
	u8 *base_addr;

	struct octeon_request_list *request_list;

	/** Octeon doorbell register for the ring. */
	void __iomem *doorbell_reg;

	/** Octeon instruction count register for this ring. */
	void __iomem *inst_cnt_reg;

	/** Number of instructions pending to be posted to Octeon. */
	u32 fill_cnt;

	/** The max. number of instructions that can be held pending by the
	 * driver.
	 */
	u32 fill_threshold;

	/** The last time that the doorbell was rung. */
	u64 last_db_time;

	/** The doorbell timeout. If the doorbell was not rung for this time and
	 * fill_cnt is non-zero, ring the doorbell again.
	 */
	u32 db_timeout;

	/** Statistics for this input queue. */
	struct oct_iq_stats stats;

	/** DMA mapped base address of the input descriptor ring. */
	dma_addr_t base_addr_dma;

	/** Application context */
	void *app_ctx;

	/* network stack queue index */
	int q_index;

	/*os ifidx associated with this queue */
	int ifidx;

};

/*----------------------  INSTRUCTION FORMAT ----------------------------*/

/** 32-byte instruction format.
 *  Format of instruction for a 32-byte mode input queue.
 */
struct octeon_instr_32B {
	/** Pointer where the input data is available. */
	u64 dptr;

	/** Instruction Header.  */
	u64 ih;

	/** Pointer where the response for a RAW mode packet will be written
	 * by Octeon.
	 */
	u64 rptr;

	/** Input Request Header. Additional info about the input. */
	u64 irh;

};

#define OCT_32B_INSTR_SIZE     (sizeof(struct octeon_instr_32B))

/** 64-byte instruction format.
 *  Format of instruction for a 64-byte mode input queue.
 */
struct octeon_instr2_64B {
	/** Pointer where the input data is available. */
	u64 dptr;

	/** Instruction Header. */
	u64 ih2;

	/** Input Request Header. */
	u64 irh;

	/** opcode/subcode specific parameters */
	u64 ossp[2];

	/** Return Data Parameters */
	u64 rdp;

	/** Pointer where the response for a RAW mode packet will be written
	 * by Octeon.
	 */
	u64 rptr;

	u64 reserved;
};

struct octeon_instr3_64B {
	/** Pointer where the input data is available. */
	u64 dptr;

	/** Instruction Header. */
	u64 ih3;

	/** Instruction Header. */
	u64 pki_ih3;

	/** Input Request Header. */
	u64 irh;

	/** opcode/subcode specific parameters */
	u64 ossp[2];

	/** Return Data Parameters */
	u64 rdp;

	/** Pointer where the response for a RAW mode packet will be written
	 * by Octeon.
	 */
	u64 rptr;

};

union octeon_instr_64B {
	struct octeon_instr2_64B cmd2;
	struct octeon_instr3_64B cmd3;
};

#define OCT_64B_INSTR_SIZE     (sizeof(union octeon_instr_64B))

/** The size of each buffer in soft command buffer pool
 */
#define  SOFT_COMMAND_BUFFER_SIZE	2048

struct octeon_soft_command {
	/** Soft command buffer info. */
	struct list_head node;
	u64 dma_addr;
	u32 size;

	/** Command and return status */
	union octeon_instr_64B cmd;

#define COMPLETION_WORD_INIT    0xffffffffffffffffULL
	u64 *status_word;

	/** Data buffer info */
	void *virtdptr;
	u64 dmadptr;
	u32 datasize;

	/** Return buffer info */
	void *virtrptr;
	u64 dmarptr;
	u32 rdatasize;

	/** Context buffer info */
	void *ctxptr;
	u32  ctxsize;

	/** Time out and callback */
	size_t wait_time;
	size_t timeout;
	u32 iq_no;
	void (*callback)(struct octeon_device *, u32, void *);
	void *callback_arg;
};

/** Maximum number of buffers to allocate into soft command buffer pool
 */
#define  MAX_SOFT_COMMAND_BUFFERS	256

/** Head of a soft command buffer pool.
 */
struct octeon_sc_buffer_pool {
	/** List structure to add delete pending entries to */
	struct list_head head;

	/** A lock for this response list */
	spinlock_t lock;

	atomic_t alloc_buf_count;
};

#define INCR_INSTRQUEUE_PKT_COUNT(octeon_dev_ptr, iq_no, field, count)  \
		(((octeon_dev_ptr)->instr_queue[iq_no]->stats.field) += count)

int octeon_setup_sc_buffer_pool(struct octeon_device *oct);
int octeon_free_sc_buffer_pool(struct octeon_device *oct);
struct octeon_soft_command *
	octeon_alloc_soft_command(struct octeon_device *oct,
				  u32 datasize, u32 rdatasize,
				  u32 ctxsize);
void octeon_free_soft_command(struct octeon_device *oct,
			      struct octeon_soft_command *sc);

/**
 *  octeon_init_instr_queue()
 *  @param octeon_dev      - pointer to the octeon device structure.
 *  @param txpciq          - queue to be initialized (0 <= q_no <= 3).
 *
 *  Called at driver init time for each input queue. iq_conf has the
 *  configuration parameters for the queue.
 *
 *  @return  Success: 0   Failure: 1
 */
int octeon_init_instr_queue(struct octeon_device *octeon_dev,
			    union oct_txpciq txpciq,
			    u32 num_descs);

/**
 *  octeon_delete_instr_queue()
 *  @param octeon_dev      - pointer to the octeon device structure.
 *  @param iq_no           - queue to be deleted (0 <= q_no <= 3).
 *
 *  Called at driver unload time for each input queue. Deletes all
 *  allocated resources for the input queue.
 *
 *  @return  Success: 0   Failure: 1
 */
int octeon_delete_instr_queue(struct octeon_device *octeon_dev, u32 iq_no);

int lio_wait_for_instr_fetch(struct octeon_device *oct);

void
octeon_ring_doorbell_locked(struct octeon_device *oct, u32 iq_no);

int
octeon_register_reqtype_free_fn(struct octeon_device *oct, int reqtype,
				void (*fn)(void *));

int
lio_process_iq_request_list(struct octeon_device *oct,
			    struct octeon_instr_queue *iq, u32 napi_budget);

int octeon_send_command(struct octeon_device *oct, u32 iq_no,
			u32 force_db, void *cmd, void *buf,
			u32 datasize, u32 reqtype);

void octeon_prepare_soft_command(struct octeon_device *oct,
				 struct octeon_soft_command *sc,
				 u8 opcode, u8 subcode,
				 u32 irh_ossp, u64 ossp0,
				 u64 ossp1);

int octeon_send_soft_command(struct octeon_device *oct,
			     struct octeon_soft_command *sc);

int octeon_setup_iq(struct octeon_device *oct, int ifidx,
		    int q_index, union oct_txpciq iq_no, u32 num_descs,
		    void *app_ctx);
int
octeon_flush_iq(struct octeon_device *oct, struct octeon_instr_queue *iq,
		u32 napi_budget);
#endif				/* __OCTEON_IQ_H__ */
