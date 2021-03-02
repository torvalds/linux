/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018-2020 Broadcom.
 */

#ifndef BCM_VK_MSG_H
#define BCM_VK_MSG_H

#include <uapi/linux/misc/bcm_vk.h>
#include "bcm_vk_sg.h"

/* Single message queue control structure */
struct bcm_vk_msgq {
	u16 type;	/* queue type */
	u16 num;	/* queue number */
	u32 start;	/* offset in BAR1 where the queue memory starts */

	u32 rd_idx; /* read idx */
	u32 wr_idx; /* write idx */

	u32 size;	/*
			 * size, which is in number of 16byte blocks,
			 * to align with the message data structure.
			 */
	u32 nxt;	/*
			 * nxt offset to the next msg queue struct.
			 * This is to provide flexibity for alignment purposes.
			 */

/* Least significant 16 bits in below field hold doorbell register offset */
#define DB_SHIFT 16

	u32 db_offset; /* queue doorbell register offset in BAR0 */

	u32 rsvd;
};

/*
 * Structure to record static info from the msgq sync.  We keep local copy
 * for some of these variables for both performance + checking purpose.
 */
struct bcm_vk_sync_qinfo {
	void __iomem *q_start;
	u32 q_size;
	u32 q_mask;
	u32 q_low;
	u32 q_db_offset;
};

#define VK_MSGQ_MAX_NR 4 /* Maximum number of message queues */

/*
 * message block - basic unit in the message where a message's size is always
 *		   N x sizeof(basic_block)
 */
struct vk_msg_blk {
	u8 function_id;
#define VK_FID_TRANS_BUF	5
#define VK_FID_SHUTDOWN		8
#define VK_FID_INIT		9
	u8 size; /* size of the message in number of vk_msg_blk's */
	u16 trans_id; /* transport id, queue & msg_id */
	u32 context_id;
#define VK_NEW_CTX		0
	u32 cmd;
#define VK_CMD_PLANES_MASK	0x000f /* number of planes to up/download */
#define VK_CMD_UPLOAD		0x0400 /* memory transfer to vk */
#define VK_CMD_DOWNLOAD		0x0500 /* memory transfer from vk */
#define VK_CMD_MASK		0x0f00 /* command mask */
	u32 arg;
};

/* vk_msg_blk is 16 bytes fixed */
#define VK_MSGQ_BLK_SIZE   (sizeof(struct vk_msg_blk))
/* shift for fast division of basic msg blk size */
#define VK_MSGQ_BLK_SZ_SHIFT 4

/* use msg_id 0 for any simplex host2vk communication */
#define VK_SIMPLEX_MSG_ID 0

/* context per session opening of sysfs */
struct bcm_vk_ctx {
	struct list_head node; /* use for linkage in Hash Table */
	unsigned int idx;
	bool in_use;
	pid_t pid;
	u32 hash_idx;
	u32 q_num; /* queue number used by the stream */
	struct miscdevice *miscdev;
	atomic_t pend_cnt; /* number of items pending to be read from host */
	atomic_t dma_cnt; /* any dma transaction outstanding */
	wait_queue_head_t rd_wq;
};

/* pid hash table entry */
struct bcm_vk_ht_entry {
	struct list_head head;
};

#define VK_DMA_MAX_ADDRS 4 /* Max 4 DMA Addresses */
/* structure for house keeping a single work entry */
struct bcm_vk_wkent {
	struct list_head node; /* for linking purpose */
	struct bcm_vk_ctx *ctx;

	/* Store up to 4 dma pointers */
	struct bcm_vk_dma dma[VK_DMA_MAX_ADDRS];

	u32 to_h_blks; /* response */
	struct vk_msg_blk *to_h_msg;

	/*
	 * put the to_v_msg at the end so that we could simply append to_v msg
	 * to the end of the allocated block
	 */
	u32 usr_msg_id;
	u32 to_v_blks;
	u32 seq_num;
	struct vk_msg_blk to_v_msg[0];
};

/* queue stats counters */
struct bcm_vk_qs_cnts {
	u32 cnt; /* general counter, used to limit output */
	u32 acc_sum;
	u32 max_occ; /* max during a sampling period */
	u32 max_abs; /* the abs max since reset */
};

/* control channel structure for either to_v or to_h communication */
struct bcm_vk_msg_chan {
	u32 q_nr;
	/* Mutex to access msgq */
	struct mutex msgq_mutex;
	/* pointing to BAR locations */
	struct bcm_vk_msgq __iomem *msgq[VK_MSGQ_MAX_NR];
	/* Spinlock to access pending queue */
	spinlock_t pendq_lock;
	/* for temporary storing pending items, one for each queue */
	struct list_head pendq[VK_MSGQ_MAX_NR];
	/* static queue info from the sync */
	struct bcm_vk_sync_qinfo sync_qinfo[VK_MSGQ_MAX_NR];
};

/* totol number of message q allowed by the driver */
#define VK_MSGQ_PER_CHAN_MAX	3
#define VK_MSGQ_NUM_DEFAULT	(VK_MSGQ_PER_CHAN_MAX - 1)

/* total number of supported ctx, 32 ctx each for 5 components */
#define VK_CMPT_CTX_MAX		(32 * 5)

/* hash table defines to store the opened FDs */
#define VK_PID_HT_SHIFT_BIT	7 /* 128 */
#define VK_PID_HT_SZ		BIT(VK_PID_HT_SHIFT_BIT)

/* The following are offsets of DDR info provided by the vk card */
#define VK_BAR0_SEG_SIZE	(4 * SZ_1K) /* segment size for BAR0 */

/* shutdown types supported */
#define VK_SHUTDOWN_PID		1
#define VK_SHUTDOWN_GRACEFUL	2

#endif
