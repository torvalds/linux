#ifndef MMC_QUEUE_H
#define MMC_QUEUE_H

#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>

static inline bool mmc_req_is_special(struct request *req)
{
	return req &&
		(req_op(req) == REQ_OP_FLUSH ||
		 req_op(req) == REQ_OP_DISCARD ||
		 req_op(req) == REQ_OP_SECURE_ERASE);
}

struct task_struct;
struct mmc_blk_data;

struct mmc_blk_request {
	struct mmc_request	mrq;
	struct mmc_command	sbc;
	struct mmc_command	cmd;
	struct mmc_command	stop;
	struct mmc_data		data;
	int			retune_retry_done;
};

struct mmc_queue_req {
	struct request		*req;
	struct mmc_blk_request	brq;
	struct scatterlist	*sg;
	char			*bounce_buf;
	struct scatterlist	*bounce_sg;
	unsigned int		bounce_sg_len;
	struct mmc_async_req	areq;
};

struct mmc_queue {
	struct mmc_card		*card;
	struct task_struct	*thread;
	struct semaphore	thread_sem;
	bool			new_request;
	bool			suspended;
	bool			asleep;
	struct mmc_blk_data	*blkdata;
	struct request_queue	*queue;
	struct mmc_queue_req	*mqrq;
	struct mmc_queue_req	*mqrq_cur;
	struct mmc_queue_req	*mqrq_prev;
	int			qdepth;
};

extern int mmc_init_queue(struct mmc_queue *, struct mmc_card *, spinlock_t *,
			  const char *);
extern void mmc_cleanup_queue(struct mmc_queue *);
extern void mmc_queue_suspend(struct mmc_queue *);
extern void mmc_queue_resume(struct mmc_queue *);

extern unsigned int mmc_queue_map_sg(struct mmc_queue *,
				     struct mmc_queue_req *);
extern void mmc_queue_bounce_pre(struct mmc_queue_req *);
extern void mmc_queue_bounce_post(struct mmc_queue_req *);

extern int mmc_access_rpmb(struct mmc_queue *);

#endif
