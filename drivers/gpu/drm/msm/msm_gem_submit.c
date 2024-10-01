// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/file.h>
#include <linux/sync_file.h>
#include <linux/uaccess.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_syncobj.h>

#include "msm_drv.h"
#include "msm_gpu.h"
#include "msm_gem.h"
#include "msm_gpu_trace.h"

/* For userspace errors, use DRM_UT_DRIVER.. so that userspace can enable
 * error msgs for debugging, but we don't spam dmesg by default
 */
#define SUBMIT_ERROR(submit, fmt, ...) \
	DRM_DEV_DEBUG_DRIVER((submit)->dev->dev, fmt, ##__VA_ARGS__)

/*
 * Cmdstream submission:
 */

static struct msm_gem_submit *submit_create(struct drm_device *dev,
		struct msm_gpu *gpu,
		struct msm_gpu_submitqueue *queue, uint32_t nr_bos,
		uint32_t nr_cmds)
{
	static atomic_t ident = ATOMIC_INIT(0);
	struct msm_gem_submit *submit;
	uint64_t sz;
	int ret;

	sz = struct_size(submit, bos, nr_bos) +
			((u64)nr_cmds * sizeof(submit->cmd[0]));

	if (sz > SIZE_MAX)
		return ERR_PTR(-ENOMEM);

	submit = kzalloc(sz, GFP_KERNEL | __GFP_NOWARN);
	if (!submit)
		return ERR_PTR(-ENOMEM);

	submit->hw_fence = msm_fence_alloc();
	if (IS_ERR(submit->hw_fence)) {
		ret = PTR_ERR(submit->hw_fence);
		kfree(submit);
		return ERR_PTR(ret);
	}

	ret = drm_sched_job_init(&submit->base, queue->entity, 1, queue);
	if (ret) {
		kfree(submit->hw_fence);
		kfree(submit);
		return ERR_PTR(ret);
	}

	kref_init(&submit->ref);
	submit->dev = dev;
	submit->aspace = queue->ctx->aspace;
	submit->gpu = gpu;
	submit->cmd = (void *)&submit->bos[nr_bos];
	submit->queue = queue;
	submit->pid = get_pid(task_pid(current));
	submit->ring = gpu->rb[queue->ring_nr];
	submit->fault_dumped = false;

	/* Get a unique identifier for the submission for logging purposes */
	submit->ident = atomic_inc_return(&ident) - 1;

	INIT_LIST_HEAD(&submit->node);

	return submit;
}

void __msm_gem_submit_destroy(struct kref *kref)
{
	struct msm_gem_submit *submit =
			container_of(kref, struct msm_gem_submit, ref);
	unsigned i;

	if (submit->fence_id) {
		spin_lock(&submit->queue->idr_lock);
		idr_remove(&submit->queue->fence_idr, submit->fence_id);
		spin_unlock(&submit->queue->idr_lock);
	}

	dma_fence_put(submit->user_fence);

	/*
	 * If the submit is freed before msm_job_run(), then hw_fence is
	 * just some pre-allocated memory, not a reference counted fence.
	 * Once the job runs and the hw_fence is initialized, it will
	 * have a refcount of at least one, since the submit holds a ref
	 * to the hw_fence.
	 */
	if (kref_read(&submit->hw_fence->refcount) == 0) {
		kfree(submit->hw_fence);
	} else {
		dma_fence_put(submit->hw_fence);
	}

	put_pid(submit->pid);
	msm_submitqueue_put(submit->queue);

	for (i = 0; i < submit->nr_cmds; i++)
		kfree(submit->cmd[i].relocs);

	kfree(submit);
}

static int submit_lookup_objects(struct msm_gem_submit *submit,
		struct drm_msm_gem_submit *args, struct drm_file *file)
{
	unsigned i;
	int ret = 0;

	for (i = 0; i < args->nr_bos; i++) {
		struct drm_msm_gem_submit_bo submit_bo;
		void __user *userptr =
			u64_to_user_ptr(args->bos + (i * sizeof(submit_bo)));

		/* make sure we don't have garbage flags, in case we hit
		 * error path before flags is initialized:
		 */
		submit->bos[i].flags = 0;

		if (copy_from_user(&submit_bo, userptr, sizeof(submit_bo))) {
			ret = -EFAULT;
			i = 0;
			goto out;
		}

/* at least one of READ and/or WRITE flags should be set: */
#define MANDATORY_FLAGS (MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE)

		if ((submit_bo.flags & ~MSM_SUBMIT_BO_FLAGS) ||
			!(submit_bo.flags & MANDATORY_FLAGS)) {
			SUBMIT_ERROR(submit, "invalid flags: %x\n", submit_bo.flags);
			ret = -EINVAL;
			i = 0;
			goto out;
		}

		submit->bos[i].handle = submit_bo.handle;
		submit->bos[i].flags = submit_bo.flags;
	}

	spin_lock(&file->table_lock);

	for (i = 0; i < args->nr_bos; i++) {
		struct drm_gem_object *obj;

		/* normally use drm_gem_object_lookup(), but for bulk lookup
		 * all under single table_lock just hit object_idr directly:
		 */
		obj = idr_find(&file->object_idr, submit->bos[i].handle);
		if (!obj) {
			SUBMIT_ERROR(submit, "invalid handle %u at index %u\n", submit->bos[i].handle, i);
			ret = -EINVAL;
			goto out_unlock;
		}

		drm_gem_object_get(obj);

		submit->bos[i].obj = obj;
	}

out_unlock:
	spin_unlock(&file->table_lock);

out:
	submit->nr_bos = i;

	return ret;
}

static int submit_lookup_cmds(struct msm_gem_submit *submit,
		struct drm_msm_gem_submit *args, struct drm_file *file)
{
	unsigned i;
	size_t sz;
	int ret = 0;

	for (i = 0; i < args->nr_cmds; i++) {
		struct drm_msm_gem_submit_cmd submit_cmd;
		void __user *userptr =
			u64_to_user_ptr(args->cmds + (i * sizeof(submit_cmd)));

		ret = copy_from_user(&submit_cmd, userptr, sizeof(submit_cmd));
		if (ret) {
			ret = -EFAULT;
			goto out;
		}

		/* validate input from userspace: */
		switch (submit_cmd.type) {
		case MSM_SUBMIT_CMD_BUF:
		case MSM_SUBMIT_CMD_IB_TARGET_BUF:
		case MSM_SUBMIT_CMD_CTX_RESTORE_BUF:
			break;
		default:
			SUBMIT_ERROR(submit, "invalid type: %08x\n", submit_cmd.type);
			return -EINVAL;
		}

		if (submit_cmd.size % 4) {
			SUBMIT_ERROR(submit, "non-aligned cmdstream buffer size: %u\n",
				     submit_cmd.size);
			ret = -EINVAL;
			goto out;
		}

		submit->cmd[i].type = submit_cmd.type;
		submit->cmd[i].size = submit_cmd.size / 4;
		submit->cmd[i].offset = submit_cmd.submit_offset / 4;
		submit->cmd[i].idx  = submit_cmd.submit_idx;
		submit->cmd[i].nr_relocs = submit_cmd.nr_relocs;

		userptr = u64_to_user_ptr(submit_cmd.relocs);

		sz = array_size(submit_cmd.nr_relocs,
				sizeof(struct drm_msm_gem_submit_reloc));
		/* check for overflow: */
		if (sz == SIZE_MAX) {
			ret = -ENOMEM;
			goto out;
		}
		submit->cmd[i].relocs = kmalloc(sz, GFP_KERNEL | __GFP_NOWARN);
		if (!submit->cmd[i].relocs) {
			ret = -ENOMEM;
			goto out;
		}
		ret = copy_from_user(submit->cmd[i].relocs, userptr, sz);
		if (ret) {
			ret = -EFAULT;
			goto out;
		}
	}

out:
	return ret;
}

/* This is where we make sure all the bo's are reserved and pin'd: */
static int submit_lock_objects(struct msm_gem_submit *submit)
{
	int ret;

	drm_exec_init(&submit->exec, DRM_EXEC_INTERRUPTIBLE_WAIT, submit->nr_bos);

	drm_exec_until_all_locked (&submit->exec) {
		for (unsigned i = 0; i < submit->nr_bos; i++) {
			struct drm_gem_object *obj = submit->bos[i].obj;
			ret = drm_exec_prepare_obj(&submit->exec, obj, 1);
			drm_exec_retry_on_contention(&submit->exec);
			if (ret)
				goto error;
		}
	}

	return 0;

error:
	return ret;
}

static int submit_fence_sync(struct msm_gem_submit *submit)
{
	int i, ret = 0;

	for (i = 0; i < submit->nr_bos; i++) {
		struct drm_gem_object *obj = submit->bos[i].obj;
		bool write = submit->bos[i].flags & MSM_SUBMIT_BO_WRITE;

		/* Otherwise userspace can ask for implicit sync to be
		 * disabled on specific buffers.  This is useful for internal
		 * usermode driver managed buffers, suballocation, etc.
		 */
		if (submit->bos[i].flags & MSM_SUBMIT_BO_NO_IMPLICIT)
			continue;

		ret = drm_sched_job_add_implicit_dependencies(&submit->base,
							      obj,
							      write);
		if (ret)
			break;
	}

	return ret;
}

static int submit_pin_objects(struct msm_gem_submit *submit)
{
	struct msm_drm_private *priv = submit->dev->dev_private;
	int i, ret = 0;

	for (i = 0; i < submit->nr_bos; i++) {
		struct drm_gem_object *obj = submit->bos[i].obj;
		struct msm_gem_vma *vma;

		/* if locking succeeded, pin bo: */
		vma = msm_gem_get_vma_locked(obj, submit->aspace);
		if (IS_ERR(vma)) {
			ret = PTR_ERR(vma);
			break;
		}

		ret = msm_gem_pin_vma_locked(obj, vma);
		if (ret)
			break;

		submit->bos[i].iova = vma->iova;
	}

	/*
	 * A second loop while holding the LRU lock (a) avoids acquiring/dropping
	 * the LRU lock for each individual bo, while (b) avoiding holding the
	 * LRU lock while calling msm_gem_pin_vma_locked() (which could trigger
	 * get_pages() which could trigger reclaim.. and if we held the LRU lock
	 * could trigger deadlock with the shrinker).
	 */
	mutex_lock(&priv->lru.lock);
	for (i = 0; i < submit->nr_bos; i++) {
		msm_gem_pin_obj_locked(submit->bos[i].obj);
	}
	mutex_unlock(&priv->lru.lock);

	submit->bos_pinned = true;

	return ret;
}

static void submit_unpin_objects(struct msm_gem_submit *submit)
{
	if (!submit->bos_pinned)
		return;

	for (int i = 0; i < submit->nr_bos; i++) {
		struct drm_gem_object *obj = submit->bos[i].obj;

		msm_gem_unpin_locked(obj);
	}

	submit->bos_pinned = false;
}

static void submit_attach_object_fences(struct msm_gem_submit *submit)
{
	int i;

	for (i = 0; i < submit->nr_bos; i++) {
		struct drm_gem_object *obj = submit->bos[i].obj;

		if (submit->bos[i].flags & MSM_SUBMIT_BO_WRITE)
			dma_resv_add_fence(obj->resv, submit->user_fence,
					   DMA_RESV_USAGE_WRITE);
		else if (submit->bos[i].flags & MSM_SUBMIT_BO_READ)
			dma_resv_add_fence(obj->resv, submit->user_fence,
					   DMA_RESV_USAGE_READ);
	}
}

static int submit_bo(struct msm_gem_submit *submit, uint32_t idx,
		struct drm_gem_object **obj, uint64_t *iova)
{
	if (idx >= submit->nr_bos) {
		SUBMIT_ERROR(submit, "invalid buffer index: %u (out of %u)\n",
			     idx, submit->nr_bos);
		return -EINVAL;
	}

	if (obj)
		*obj = submit->bos[idx].obj;
	if (iova)
		*iova = submit->bos[idx].iova;

	return 0;
}

/* process the reloc's and patch up the cmdstream as needed: */
static int submit_reloc(struct msm_gem_submit *submit, struct drm_gem_object *obj,
		uint32_t offset, uint32_t nr_relocs, struct drm_msm_gem_submit_reloc *relocs)
{
	uint32_t i, last_offset = 0;
	uint32_t *ptr;
	int ret = 0;

	if (offset % 4) {
		SUBMIT_ERROR(submit, "non-aligned cmdstream buffer: %u\n", offset);
		return -EINVAL;
	}

	/* For now, just map the entire thing.  Eventually we probably
	 * to do it page-by-page, w/ kmap() if not vmap()d..
	 */
	ptr = msm_gem_get_vaddr_locked(obj);

	if (IS_ERR(ptr)) {
		ret = PTR_ERR(ptr);
		DBG("failed to map: %d", ret);
		return ret;
	}

	for (i = 0; i < nr_relocs; i++) {
		struct drm_msm_gem_submit_reloc submit_reloc = relocs[i];
		uint32_t off;
		uint64_t iova;

		if (submit_reloc.submit_offset % 4) {
			SUBMIT_ERROR(submit, "non-aligned reloc offset: %u\n",
				     submit_reloc.submit_offset);
			ret = -EINVAL;
			goto out;
		}

		/* offset in dwords: */
		off = submit_reloc.submit_offset / 4;

		if ((off >= (obj->size / 4)) ||
				(off < last_offset)) {
			SUBMIT_ERROR(submit, "invalid offset %u at reloc %u\n", off, i);
			ret = -EINVAL;
			goto out;
		}

		ret = submit_bo(submit, submit_reloc.reloc_idx, NULL, &iova);
		if (ret)
			goto out;

		iova += submit_reloc.reloc_offset;

		if (submit_reloc.shift < 0)
			iova >>= -submit_reloc.shift;
		else
			iova <<= submit_reloc.shift;

		ptr[off] = iova | submit_reloc.or;

		last_offset = off;
	}

out:
	msm_gem_put_vaddr_locked(obj);

	return ret;
}

/* Cleanup submit at end of ioctl.  In the error case, this also drops
 * references, unpins, and drops active refcnt.  In the non-error case,
 * this is done when the submit is retired.
 */
static void submit_cleanup(struct msm_gem_submit *submit, bool error)
{
	if (error) {
		submit_unpin_objects(submit);
		/* job wasn't enqueued to scheduler, so early retirement: */
		msm_submit_retire(submit);
	}

	if (submit->exec.objects)
		drm_exec_fini(&submit->exec);
}

void msm_submit_retire(struct msm_gem_submit *submit)
{
	int i;

	for (i = 0; i < submit->nr_bos; i++) {
		struct drm_gem_object *obj = submit->bos[i].obj;

		drm_gem_object_put(obj);
	}
}

struct msm_submit_post_dep {
	struct drm_syncobj *syncobj;
	uint64_t point;
	struct dma_fence_chain *chain;
};

static struct drm_syncobj **msm_parse_deps(struct msm_gem_submit *submit,
                                           struct drm_file *file,
                                           uint64_t in_syncobjs_addr,
                                           uint32_t nr_in_syncobjs,
                                           size_t syncobj_stride)
{
	struct drm_syncobj **syncobjs = NULL;
	struct drm_msm_gem_submit_syncobj syncobj_desc = {0};
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
		    !drm_core_check_feature(submit->dev, DRIVER_SYNCOBJ_TIMELINE)) {
			ret = -EOPNOTSUPP;
			break;
		}

		if (syncobj_desc.flags & ~MSM_SUBMIT_SYNCOBJ_FLAGS) {
			ret = -EINVAL;
			break;
		}

		ret = drm_sched_job_add_syncobj_dependency(&submit->base, file,
							   syncobj_desc.handle, syncobj_desc.point);
		if (ret)
			break;

		if (syncobj_desc.flags & MSM_SUBMIT_SYNCOBJ_RESET) {
			syncobjs[i] =
				drm_syncobj_find(file, syncobj_desc.handle);
			if (!syncobjs[i]) {
				ret = -EINVAL;
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

static void msm_reset_syncobjs(struct drm_syncobj **syncobjs,
                               uint32_t nr_syncobjs)
{
	uint32_t i;

	for (i = 0; syncobjs && i < nr_syncobjs; ++i) {
		if (syncobjs[i])
			drm_syncobj_replace_fence(syncobjs[i], NULL);
	}
}

static struct msm_submit_post_dep *msm_parse_post_deps(struct drm_device *dev,
                                                       struct drm_file *file,
                                                       uint64_t syncobjs_addr,
                                                       uint32_t nr_syncobjs,
                                                       size_t syncobj_stride)
{
	struct msm_submit_post_dep *post_deps;
	struct drm_msm_gem_submit_syncobj syncobj_desc = {0};
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
			ret = -EINVAL;
			break;
		}

		if (syncobj_desc.point) {
			if (!drm_core_check_feature(dev,
			                            DRIVER_SYNCOBJ_TIMELINE)) {
				ret = -EOPNOTSUPP;
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
			ret = -EINVAL;
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

static void msm_process_post_deps(struct msm_submit_post_dep *post_deps,
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

int msm_ioctl_gem_submit(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_msm_gem_submit *args = data;
	struct msm_file_private *ctx = file->driver_priv;
	struct msm_gem_submit *submit = NULL;
	struct msm_gpu *gpu = priv->gpu;
	struct msm_gpu_submitqueue *queue;
	struct msm_ringbuffer *ring;
	struct msm_submit_post_dep *post_deps = NULL;
	struct drm_syncobj **syncobjs_to_reset = NULL;
	int out_fence_fd = -1;
	unsigned i;
	int ret;

	if (!gpu)
		return -ENXIO;

	if (args->pad)
		return -EINVAL;

	if (unlikely(!ctx->aspace) && !capable(CAP_SYS_RAWIO)) {
		DRM_ERROR_RATELIMITED("IOMMU support or CAP_SYS_RAWIO required!\n");
		return -EPERM;
	}

	/* for now, we just have 3d pipe.. eventually this would need to
	 * be more clever to dispatch to appropriate gpu module:
	 */
	if (MSM_PIPE_ID(args->flags) != MSM_PIPE_3D0)
		return -EINVAL;

	if (MSM_PIPE_FLAGS(args->flags) & ~MSM_SUBMIT_FLAGS)
		return -EINVAL;

	if (args->flags & MSM_SUBMIT_SUDO) {
		if (!IS_ENABLED(CONFIG_DRM_MSM_GPU_SUDO) ||
		    !capable(CAP_SYS_RAWIO))
			return -EINVAL;
	}

	queue = msm_submitqueue_get(ctx, args->queueid);
	if (!queue)
		return -ENOENT;

	ring = gpu->rb[queue->ring_nr];

	if (args->flags & MSM_SUBMIT_FENCE_FD_OUT) {
		out_fence_fd = get_unused_fd_flags(O_CLOEXEC);
		if (out_fence_fd < 0) {
			ret = out_fence_fd;
			goto out_post_unlock;
		}
	}

	submit = submit_create(dev, gpu, queue, args->nr_bos, args->nr_cmds);
	if (IS_ERR(submit)) {
		ret = PTR_ERR(submit);
		goto out_post_unlock;
	}

	trace_msm_gpu_submit(pid_nr(submit->pid), ring->id, submit->ident,
		args->nr_bos, args->nr_cmds);

	ret = mutex_lock_interruptible(&queue->lock);
	if (ret)
		goto out_post_unlock;

	if (args->flags & MSM_SUBMIT_SUDO)
		submit->in_rb = true;

	if (args->flags & MSM_SUBMIT_FENCE_FD_IN) {
		struct dma_fence *in_fence;

		in_fence = sync_file_get_fence(args->fence_fd);

		if (!in_fence) {
			ret = -EINVAL;
			goto out_unlock;
		}

		ret = drm_sched_job_add_dependency(&submit->base, in_fence);
		if (ret)
			goto out_unlock;
	}

	if (args->flags & MSM_SUBMIT_SYNCOBJ_IN) {
		syncobjs_to_reset = msm_parse_deps(submit, file,
		                                   args->in_syncobjs,
		                                   args->nr_in_syncobjs,
		                                   args->syncobj_stride);
		if (IS_ERR(syncobjs_to_reset)) {
			ret = PTR_ERR(syncobjs_to_reset);
			goto out_unlock;
		}
	}

	if (args->flags & MSM_SUBMIT_SYNCOBJ_OUT) {
		post_deps = msm_parse_post_deps(dev, file,
		                                args->out_syncobjs,
		                                args->nr_out_syncobjs,
		                                args->syncobj_stride);
		if (IS_ERR(post_deps)) {
			ret = PTR_ERR(post_deps);
			goto out_unlock;
		}
	}

	ret = submit_lookup_objects(submit, args, file);
	if (ret)
		goto out;

	ret = submit_lookup_cmds(submit, args, file);
	if (ret)
		goto out;

	/* copy_*_user while holding a ww ticket upsets lockdep */
	ret = submit_lock_objects(submit);
	if (ret)
		goto out;

	if (!(args->flags & MSM_SUBMIT_NO_IMPLICIT)) {
		ret = submit_fence_sync(submit);
		if (ret)
			goto out;
	}

	ret = submit_pin_objects(submit);
	if (ret)
		goto out;

	for (i = 0; i < args->nr_cmds; i++) {
		struct drm_gem_object *obj;
		uint64_t iova;

		ret = submit_bo(submit, submit->cmd[i].idx, &obj, &iova);
		if (ret)
			goto out;

		if (!submit->cmd[i].size ||
			((submit->cmd[i].size + submit->cmd[i].offset) >
				obj->size / 4)) {
			SUBMIT_ERROR(submit, "invalid cmdstream size: %u\n", submit->cmd[i].size * 4);
			ret = -EINVAL;
			goto out;
		}

		submit->cmd[i].iova = iova + (submit->cmd[i].offset * 4);

		if (likely(!submit->cmd[i].nr_relocs))
			continue;

		if (!gpu->allow_relocs) {
			SUBMIT_ERROR(submit, "relocs not allowed\n");
			ret = -EINVAL;
			goto out;
		}

		ret = submit_reloc(submit, obj, submit->cmd[i].offset * 4,
				submit->cmd[i].nr_relocs, submit->cmd[i].relocs);
		if (ret)
			goto out;
	}

	submit->nr_cmds = i;

	idr_preload(GFP_KERNEL);

	spin_lock(&queue->idr_lock);

	/*
	 * If using userspace provided seqno fence, validate that the id
	 * is available before arming sched job.  Since access to fence_idr
	 * is serialized on the queue lock, the slot should be still avail
	 * after the job is armed
	 */
	if ((args->flags & MSM_SUBMIT_FENCE_SN_IN) &&
			(!args->fence || idr_find(&queue->fence_idr, args->fence))) {
		spin_unlock(&queue->idr_lock);
		idr_preload_end();
		ret = -EINVAL;
		goto out;
	}

	drm_sched_job_arm(&submit->base);

	submit->user_fence = dma_fence_get(&submit->base.s_fence->finished);

	if (args->flags & MSM_SUBMIT_FENCE_SN_IN) {
		/*
		 * Userspace has assigned the seqno fence that it wants
		 * us to use.  It is an error to pick a fence sequence
		 * number that is not available.
		 */
		submit->fence_id = args->fence;
		ret = idr_alloc_u32(&queue->fence_idr, submit->user_fence,
				    &submit->fence_id, submit->fence_id,
				    GFP_NOWAIT);
		/*
		 * We've already validated that the fence_id slot is valid,
		 * so if idr_alloc_u32 failed, it is a kernel bug
		 */
		WARN_ON(ret);
	} else {
		/*
		 * Allocate an id which can be used by WAIT_FENCE ioctl to map
		 * back to the underlying fence.
		 */
		submit->fence_id = idr_alloc_cyclic(&queue->fence_idr,
						    submit->user_fence, 1,
						    INT_MAX, GFP_NOWAIT);
	}

	spin_unlock(&queue->idr_lock);
	idr_preload_end();

	if (submit->fence_id < 0) {
		ret = submit->fence_id;
		submit->fence_id = 0;
	}

	if (ret == 0 && args->flags & MSM_SUBMIT_FENCE_FD_OUT) {
		struct sync_file *sync_file = sync_file_create(submit->user_fence);
		if (!sync_file) {
			ret = -ENOMEM;
		} else {
			fd_install(out_fence_fd, sync_file->file);
			args->fence_fd = out_fence_fd;
		}
	}

	if (ret)
		goto out;

	submit_attach_object_fences(submit);

	/* The scheduler owns a ref now: */
	msm_gem_submit_get(submit);

	msm_rd_dump_submit(priv->rd, submit, NULL);

	drm_sched_entity_push_job(&submit->base);

	args->fence = submit->fence_id;
	queue->last_fence = submit->fence_id;

	msm_reset_syncobjs(syncobjs_to_reset, args->nr_in_syncobjs);
	msm_process_post_deps(post_deps, args->nr_out_syncobjs,
	                      submit->user_fence);


out:
	submit_cleanup(submit, !!ret);
out_unlock:
	mutex_unlock(&queue->lock);
out_post_unlock:
	if (ret && (out_fence_fd >= 0))
		put_unused_fd(out_fence_fd);

	if (!IS_ERR_OR_NULL(submit)) {
		msm_gem_submit_put(submit);
	} else {
		/*
		 * If the submit hasn't yet taken ownership of the queue
		 * then we need to drop the reference ourself:
		 */
		msm_submitqueue_put(queue);
	}
	if (!IS_ERR_OR_NULL(post_deps)) {
		for (i = 0; i < args->nr_out_syncobjs; ++i) {
			kfree(post_deps[i].chain);
			drm_syncobj_put(post_deps[i].syncobj);
		}
		kfree(post_deps);
	}

	if (!IS_ERR_OR_NULL(syncobjs_to_reset)) {
		for (i = 0; i < args->nr_in_syncobjs; ++i) {
			if (syncobjs_to_reset[i])
				drm_syncobj_put(syncobjs_to_reset[i]);
		}
		kfree(syncobjs_to_reset);
	}

	return ret;
}
