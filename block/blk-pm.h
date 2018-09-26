/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _BLOCK_BLK_PM_H_
#define _BLOCK_BLK_PM_H_

#include <linux/pm_runtime.h>

#ifdef CONFIG_PM
static inline void blk_pm_requeue_request(struct request *rq)
{
	if (rq->q->dev && !(rq->rq_flags & RQF_PM))
		rq->q->nr_pending--;
}

static inline void blk_pm_add_request(struct request_queue *q,
				      struct request *rq)
{
	if (q->dev && !(rq->rq_flags & RQF_PM) && q->nr_pending++ == 0 &&
	    (q->rpm_status == RPM_SUSPENDED || q->rpm_status == RPM_SUSPENDING))
		pm_request_resume(q->dev);
}

static inline void blk_pm_put_request(struct request *rq)
{
	if (rq->q->dev && !(rq->rq_flags & RQF_PM) && !--rq->q->nr_pending)
		pm_runtime_mark_last_busy(rq->q->dev);
}
#else
static inline void blk_pm_requeue_request(struct request *rq)
{
}

static inline void blk_pm_add_request(struct request_queue *q,
				      struct request *rq)
{
}

static inline void blk_pm_put_request(struct request *rq)
{
}
#endif

#endif /* _BLOCK_BLK_PM_H_ */
