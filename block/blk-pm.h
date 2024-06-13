/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _BLOCK_BLK_PM_H_
#define _BLOCK_BLK_PM_H_

#include <linux/pm_runtime.h>

#ifdef CONFIG_PM
static inline int blk_pm_resume_queue(const bool pm, struct request_queue *q)
{
	if (!q->dev || !blk_queue_pm_only(q))
		return 1;	/* Nothing to do */
	if (pm && q->rpm_status != RPM_SUSPENDED)
		return 1;	/* Request allowed */
	pm_request_resume(q->dev);
	return 0;
}

static inline void blk_pm_mark_last_busy(struct request *rq)
{
	if (rq->q->dev && !(rq->rq_flags & RQF_PM))
		pm_runtime_mark_last_busy(rq->q->dev);
}
#else
static inline int blk_pm_resume_queue(const bool pm, struct request_queue *q)
{
	return 1;
}

static inline void blk_pm_mark_last_busy(struct request *rq)
{
}
#endif

#endif /* _BLOCK_BLK_PM_H_ */
