#ifndef BLK_WB_H
#define BLK_WB_H

#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/timer.h>

struct rq_wb {
	/*
	 * Settings that govern how we throttle
	 */
	unsigned int wb_background;		/* background writeback */
	unsigned int wb_normal;			/* normal writeback */
	unsigned int wb_max;			/* max throughput writeback */
	unsigned int scale_step;

	u64 win_nsec;				/* default window size */
	u64 cur_win_nsec;			/* current window size */

	unsigned int unknown_cnt;

	struct timer_list window_timer;

	s64 sync_issue;
	void *sync_cookie;

	unsigned long last_issue;		/* last non-throttled issue */
	unsigned long last_comp;		/* last non-throttled comp */
	unsigned long min_lat_nsec;
	atomic_t *bdp_wait;
	struct request_queue *q;
	wait_queue_head_t wait;
	atomic_t inflight;
};

void __blk_wb_done(struct rq_wb *);
void blk_wb_done(struct rq_wb *, struct request *);
bool blk_wb_wait(struct rq_wb *, struct bio *, spinlock_t *);
void blk_wb_init(struct request_queue *);
void blk_wb_exit(struct request_queue *);
void blk_wb_update_limits(struct rq_wb *);
void blk_wb_requeue(struct rq_wb *, struct request *);
void blk_wb_issue(struct rq_wb *, struct request *);

#endif
