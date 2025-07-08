/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2020 Google, Inc */

#ifndef __MSM_GEM_SYNCOBJ_H__
#define __MSM_GEM_SYNCOBJ_H__

#include "drm/drm_device.h"
#include "drm/drm_syncobj.h"
#include "drm/gpu_scheduler.h"

struct msm_syncobj_post_dep {
	struct drm_syncobj *syncobj;
	uint64_t point;
	struct dma_fence_chain *chain;
};

struct drm_syncobj **
msm_syncobj_parse_deps(struct drm_device *dev,
		       struct drm_sched_job *job,
		       struct drm_file *file,
		       uint64_t in_syncobjs_addr,
		       uint32_t nr_in_syncobjs,
		       size_t syncobj_stride);

void msm_syncobj_reset(struct drm_syncobj **syncobjs, uint32_t nr_syncobjs);

struct msm_syncobj_post_dep *
msm_syncobj_parse_post_deps(struct drm_device *dev,
			    struct drm_file *file,
			    uint64_t syncobjs_addr,
			    uint32_t nr_syncobjs,
			    size_t syncobj_stride);

void msm_syncobj_process_post_deps(struct msm_syncobj_post_dep *post_deps,
				   uint32_t count, struct dma_fence *fence);

#endif /* __MSM_GEM_SYNCOBJ_H__ */
