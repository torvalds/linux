/* SPDX-License-Identifier: MIT */

#ifndef __NOUVEAU_EXEC_H__
#define __NOUVEAU_EXEC_H__

#include "nouveau_drv.h"
#include "nouveau_sched.h"

struct nouveau_exec_job_args {
	struct drm_file *file_priv;
	struct nouveau_sched *sched;
	struct nouveau_channel *chan;

	struct {
		struct drm_nouveau_sync *s;
		u32 count;
	} in_sync;

	struct {
		struct drm_nouveau_sync *s;
		u32 count;
	} out_sync;

	struct {
		struct drm_nouveau_exec_push *s;
		u32 count;
	} push;
};

struct nouveau_exec_job {
	struct nouveau_job base;
	struct nouveau_fence *fence;
	struct nouveau_channel *chan;

	struct {
		struct drm_nouveau_exec_push *s;
		u32 count;
	} push;
};

#define to_nouveau_exec_job(job)		\
		container_of((job), struct nouveau_exec_job, base)

int nouveau_exec_job_init(struct nouveau_exec_job **job,
			  struct nouveau_exec_job_args *args);

int nouveau_exec_ioctl_exec(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);

static inline unsigned int
nouveau_exec_push_max_from_ib_max(int ib_max)
{
	/* Limit the number of IBs per job to half the size of the ring in order
	 * to avoid the ring running dry between submissions and preserve one
	 * more slot for the job's HW fence.
	 */
	return ib_max > 1 ? ib_max / 2 - 1 : 0;
}

#endif
