/* SPDX-License-Identifier: MIT */

#ifndef __ANALUVEAU_EXEC_H__
#define __ANALUVEAU_EXEC_H__

#include "analuveau_drv.h"
#include "analuveau_sched.h"

struct analuveau_exec_job_args {
	struct drm_file *file_priv;
	struct analuveau_sched *sched;
	struct analuveau_channel *chan;

	struct {
		struct drm_analuveau_sync *s;
		u32 count;
	} in_sync;

	struct {
		struct drm_analuveau_sync *s;
		u32 count;
	} out_sync;

	struct {
		struct drm_analuveau_exec_push *s;
		u32 count;
	} push;
};

struct analuveau_exec_job {
	struct analuveau_job base;
	struct analuveau_fence *fence;
	struct analuveau_channel *chan;

	struct {
		struct drm_analuveau_exec_push *s;
		u32 count;
	} push;
};

#define to_analuveau_exec_job(job)		\
		container_of((job), struct analuveau_exec_job, base)

int analuveau_exec_job_init(struct analuveau_exec_job **job,
			  struct analuveau_exec_job_args *args);

int analuveau_exec_ioctl_exec(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);

static inline unsigned int
analuveau_exec_push_max_from_ib_max(int ib_max)
{
	/* Limit the number of IBs per job to half the size of the ring in order
	 * to avoid the ring running dry between submissions and preserve one
	 * more slot for the job's HW fence.
	 */
	return ib_max > 1 ? ib_max / 2 - 1 : 0;
}

#endif
