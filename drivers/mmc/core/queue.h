/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MMC_QUEUE_H
#define MMC_QUEUE_H

#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>

enum mmc_issued {
	MMC_REQ_STARTED,
	MMC_REQ_BUSY,
	MMC_REQ_FAILED_TO_START,
	MMC_REQ_FINISHED,
};

enum mmc_issue_type {
	MMC_ISSUE_SYNC,
	MMC_ISSUE_ASYNC,
	MMC_ISSUE_MAX,
};

static inline struct mmc_queue_req *req_to_mmc_queue_req(struct request *rq)
{
	return blk_mq_rq_to_pdu(rq);
}

struct mmc_queue_req;

static inline struct request *mmc_queue_req_to_req(struct mmc_queue_req *mqr)
{
	return blk_mq_rq_from_pdu(mqr);
}

struct task_struct;
struct mmc_blk_data;
struct mmc_blk_ioc_data;

struct mmc_blk_request {
	struct mmc_request	mrq;
	struct mmc_command	sbc;
	struct mmc_command	cmd;
	struct mmc_command	stop;
	struct mmc_data		data;
	int			retune_retry_done;
};

/**
 * enum mmc_drv_op - enumerates the operations in the mmc_queue_req
 * @MMC_DRV_OP_IOCTL: ioctl operation
 * @MMC_DRV_OP_IOCTL_RPMB: RPMB-oriented ioctl operation
 * @MMC_DRV_OP_BOOT_WP: write protect boot partitions
 * @MMC_DRV_OP_GET_CARD_STATUS: get card status
 * @MMC_DRV_OP_GET_EXT_CSD: get the EXT CSD from an eMMC card
 */
enum mmc_drv_op {
	MMC_DRV_OP_IOCTL,
	MMC_DRV_OP_IOCTL_RPMB,
	MMC_DRV_OP_BOOT_WP,
	MMC_DRV_OP_GET_CARD_STATUS,
	MMC_DRV_OP_GET_EXT_CSD,
};

struct mmc_queue_req {
	struct mmc_blk_request	brq;
	struct scatterlist	*sg;
	struct mmc_async_req	areq;
	enum mmc_drv_op		drv_op;
	int			drv_op_result;
	void			*drv_op_data;
	unsigned int		ioc_count;
	int			retries;
};

struct mmc_queue {
	struct mmc_card		*card;
	struct task_struct	*thread;
	struct semaphore	thread_sem;
	struct mmc_ctx		ctx;
	struct blk_mq_tag_set	tag_set;
	bool			suspended;
	bool			asleep;
	struct mmc_blk_data	*blkdata;
	struct request_queue	*queue;
	/*
	 * FIXME: this counter is not a very reliable way of keeping
	 * track of how many requests that are ongoing. Switch to just
	 * letting the block core keep track of requests and per-request
	 * associated mmc_queue_req data.
	 */
	int			qcnt;

	int			in_flight[MMC_ISSUE_MAX];
	bool			rw_wait;
	bool			waiting;
	wait_queue_head_t	wait;
	struct request		*complete_req;
	struct mutex		complete_lock;
	struct work_struct	complete_work;
};

extern int mmc_init_queue(struct mmc_queue *, struct mmc_card *, spinlock_t *,
			  const char *);
extern void mmc_cleanup_queue(struct mmc_queue *);
extern void mmc_queue_suspend(struct mmc_queue *);
extern void mmc_queue_resume(struct mmc_queue *);
extern unsigned int mmc_queue_map_sg(struct mmc_queue *,
				     struct mmc_queue_req *);

enum mmc_issue_type mmc_issue_type(struct mmc_queue *mq, struct request *req);

static inline int mmc_tot_in_flight(struct mmc_queue *mq)
{
	return mq->in_flight[MMC_ISSUE_SYNC] +
	       mq->in_flight[MMC_ISSUE_ASYNC];
}

#endif
