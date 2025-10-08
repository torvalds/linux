/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2020 Google, Inc */

#include "drm/drm_drv.h"

#include "msm_drv.h"
#include "msm_syncobj.h"

struct drm_syncobj **
msm_syncobj_parse_deps(struct drm_device *dev,
		       struct drm_sched_job *job,
		       struct drm_file *file,
		       uint64_t in_syncobjs_addr,
		       uint32_t nr_in_syncobjs,
		       size_t syncobj_stride)
{
	struct drm_syncobj **syncobjs = NULL;
	struct drm_msm_syncobj syncobj_desc = {0};
	int ret = 0;
	uint32_t i, j;

	syncobjs = kcalloc(nr_in_syncobjs, sizeof(*syncobjs),
	                   GFP_KERNEL | __GFP_NOWARN | __GFP_NORETRY);
	if (!syncobjs)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < nr_in_syncobjs; ++i) {
		uint64_t address = in_syncobjs_addr + i * syncobj_stride;

		if (copy_from_user(&syncobj_desc,
			           u64_to_user_ptr(address),
			           min(syncobj_stride, sizeof(syncobj_desc)))) {
			ret = -EFAULT;
			break;
		}

		if (syncobj_desc.point &&
		    !drm_core_check_feature(dev, DRIVER_SYNCOBJ_TIMELINE)) {
			ret = UERR(EOPNOTSUPP, dev, "syncobj timeline unsupported");
			break;
		}

		if (syncobj_desc.flags & ~MSM_SYNCOBJ_FLAGS) {
			ret = UERR(EINVAL, dev, "invalid syncobj flags: %x", syncobj_desc.flags);
			break;
		}

		ret = drm_sched_job_add_syncobj_dependency(job, file,
						   syncobj_desc.handle,
						   syncobj_desc.point);
		if (ret)
			break;

		if (syncobj_desc.flags & MSM_SYNCOBJ_RESET) {
			syncobjs[i] = drm_syncobj_find(file, syncobj_desc.handle);
			if (!syncobjs[i]) {
				ret = UERR(EINVAL, dev, "invalid syncobj handle: %u", i);
				break;
			}
		}
	}

	if (ret) {
		for (j = 0; j <= i; ++j) {
			if (syncobjs[j])
				drm_syncobj_put(syncobjs[j]);
		}
		kfree(syncobjs);
		return ERR_PTR(ret);
	}
	return syncobjs;
}

void
msm_syncobj_reset(struct drm_syncobj **syncobjs, uint32_t nr_syncobjs)
{
	uint32_t i;

	for (i = 0; syncobjs && i < nr_syncobjs; ++i) {
		if (syncobjs[i])
			drm_syncobj_replace_fence(syncobjs[i], NULL);
	}
}

struct msm_syncobj_post_dep *
msm_syncobj_parse_post_deps(struct drm_device *dev,
			    struct drm_file *file,
			    uint64_t syncobjs_addr,
			    uint32_t nr_syncobjs,
			    size_t syncobj_stride)
{
	struct msm_syncobj_post_dep *post_deps;
	struct drm_msm_syncobj syncobj_desc = {0};
	int ret = 0;
	uint32_t i, j;

	post_deps = kcalloc(nr_syncobjs, sizeof(*post_deps),
			    GFP_KERNEL | __GFP_NOWARN | __GFP_NORETRY);
	if (!post_deps)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < nr_syncobjs; ++i) {
		uint64_t address = syncobjs_addr + i * syncobj_stride;

		if (copy_from_user(&syncobj_desc,
			           u64_to_user_ptr(address),
			           min(syncobj_stride, sizeof(syncobj_desc)))) {
			ret = -EFAULT;
			break;
		}

		post_deps[i].point = syncobj_desc.point;

		if (syncobj_desc.flags) {
			ret = UERR(EINVAL, dev, "invalid syncobj flags");
			break;
		}

		if (syncobj_desc.point) {
			if (!drm_core_check_feature(dev,
			                            DRIVER_SYNCOBJ_TIMELINE)) {
				ret = UERR(EOPNOTSUPP, dev, "syncobj timeline unsupported");
				break;
			}

			post_deps[i].chain = dma_fence_chain_alloc();
			if (!post_deps[i].chain) {
				ret = -ENOMEM;
				break;
			}
		}

		post_deps[i].syncobj =
			drm_syncobj_find(file, syncobj_desc.handle);
		if (!post_deps[i].syncobj) {
			ret = UERR(EINVAL, dev, "invalid syncobj handle");
			break;
		}
	}

	if (ret) {
		for (j = 0; j <= i; ++j) {
			dma_fence_chain_free(post_deps[j].chain);
			if (post_deps[j].syncobj)
				drm_syncobj_put(post_deps[j].syncobj);
		}

		kfree(post_deps);
		return ERR_PTR(ret);
	}

	return post_deps;
}

void
msm_syncobj_process_post_deps(struct msm_syncobj_post_dep *post_deps,
			      uint32_t count, struct dma_fence *fence)
{
	uint32_t i;

	for (i = 0; post_deps && i < count; ++i) {
		if (post_deps[i].chain) {
			drm_syncobj_add_point(post_deps[i].syncobj,
			                      post_deps[i].chain,
			                      fence, post_deps[i].point);
			post_deps[i].chain = NULL;
		} else {
			drm_syncobj_replace_fence(post_deps[i].syncobj,
			                          fence);
		}
	}
}
