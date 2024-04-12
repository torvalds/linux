// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Authors:
 *    Dave Airlie
 *    Alon Levy
 */

#include <linux/dma-fence-unwrap.h>
#include <linux/file.h>
#include <linux/sync_file.h>
#include <linux/uaccess.h>

#include <drm/drm_file.h>
#include <drm/drm_syncobj.h>
#include <drm/virtgpu_drm.h>

#include "virtgpu_drv.h"

struct virtio_gpu_submit_post_dep {
	struct drm_syncobj *syncobj;
	struct dma_fence_chain *chain;
	u64 point;
};

struct virtio_gpu_submit {
	struct virtio_gpu_submit_post_dep *post_deps;
	unsigned int num_out_syncobjs;

	struct drm_syncobj **in_syncobjs;
	unsigned int num_in_syncobjs;

	struct virtio_gpu_object_array *buflist;
	struct drm_virtgpu_execbuffer *exbuf;
	struct virtio_gpu_fence *out_fence;
	struct virtio_gpu_fpriv *vfpriv;
	struct virtio_gpu_device *vgdev;
	struct sync_file *sync_file;
	struct drm_file *file;
	int out_fence_fd;
	u64 fence_ctx;
	u32 ring_idx;
	void *buf;
};

static int virtio_gpu_do_fence_wait(struct virtio_gpu_submit *submit,
				    struct dma_fence *in_fence)
{
	u32 context = submit->fence_ctx + submit->ring_idx;

	if (dma_fence_match_context(in_fence, context))
		return 0;

	return dma_fence_wait(in_fence, true);
}

static int virtio_gpu_dma_fence_wait(struct virtio_gpu_submit *submit,
				     struct dma_fence *fence)
{
	struct dma_fence_unwrap itr;
	struct dma_fence *f;
	int err;

	dma_fence_unwrap_for_each(f, &itr, fence) {
		err = virtio_gpu_do_fence_wait(submit, f);
		if (err)
			return err;
	}

	return 0;
}

static void virtio_gpu_free_syncobjs(struct drm_syncobj **syncobjs,
				     u32 nr_syncobjs)
{
	u32 i = nr_syncobjs;

	while (i--) {
		if (syncobjs[i])
			drm_syncobj_put(syncobjs[i]);
	}

	kvfree(syncobjs);
}

static int
virtio_gpu_parse_deps(struct virtio_gpu_submit *submit)
{
	struct drm_virtgpu_execbuffer *exbuf = submit->exbuf;
	struct drm_virtgpu_execbuffer_syncobj syncobj_desc;
	size_t syncobj_stride = exbuf->syncobj_stride;
	u32 num_in_syncobjs = exbuf->num_in_syncobjs;
	struct drm_syncobj **syncobjs;
	int ret = 0, i;

	if (!num_in_syncobjs)
		return 0;

	/*
	 * kvmalloc() at first tries to allocate memory using kmalloc() and
	 * falls back to vmalloc() only on failure. It also uses __GFP_NOWARN
	 * internally for allocations larger than a page size, preventing
	 * storm of KMSG warnings.
	 */
	syncobjs = kvcalloc(num_in_syncobjs, sizeof(*syncobjs), GFP_KERNEL);
	if (!syncobjs)
		return -ENOMEM;

	for (i = 0; i < num_in_syncobjs; i++) {
		u64 address = exbuf->in_syncobjs + i * syncobj_stride;
		struct dma_fence *fence;

		memset(&syncobj_desc, 0, sizeof(syncobj_desc));

		if (copy_from_user(&syncobj_desc,
				   u64_to_user_ptr(address),
				   min(syncobj_stride, sizeof(syncobj_desc)))) {
			ret = -EFAULT;
			break;
		}

		if (syncobj_desc.flags & ~VIRTGPU_EXECBUF_SYNCOBJ_FLAGS) {
			ret = -EINVAL;
			break;
		}

		ret = drm_syncobj_find_fence(submit->file, syncobj_desc.handle,
					     syncobj_desc.point, 0, &fence);
		if (ret)
			break;

		ret = virtio_gpu_dma_fence_wait(submit, fence);

		dma_fence_put(fence);
		if (ret)
			break;

		if (syncobj_desc.flags & VIRTGPU_EXECBUF_SYNCOBJ_RESET) {
			syncobjs[i] = drm_syncobj_find(submit->file,
						       syncobj_desc.handle);
			if (!syncobjs[i]) {
				ret = -EINVAL;
				break;
			}
		}
	}

	if (ret) {
		virtio_gpu_free_syncobjs(syncobjs, i);
		return ret;
	}

	submit->num_in_syncobjs = num_in_syncobjs;
	submit->in_syncobjs = syncobjs;

	return ret;
}

static void virtio_gpu_reset_syncobjs(struct drm_syncobj **syncobjs,
				      u32 nr_syncobjs)
{
	u32 i;

	for (i = 0; i < nr_syncobjs; i++) {
		if (syncobjs[i])
			drm_syncobj_replace_fence(syncobjs[i], NULL);
	}
}

static void
virtio_gpu_free_post_deps(struct virtio_gpu_submit_post_dep *post_deps,
			  u32 nr_syncobjs)
{
	u32 i = nr_syncobjs;

	while (i--) {
		kfree(post_deps[i].chain);
		drm_syncobj_put(post_deps[i].syncobj);
	}

	kvfree(post_deps);
}

static int virtio_gpu_parse_post_deps(struct virtio_gpu_submit *submit)
{
	struct drm_virtgpu_execbuffer *exbuf = submit->exbuf;
	struct drm_virtgpu_execbuffer_syncobj syncobj_desc;
	struct virtio_gpu_submit_post_dep *post_deps;
	u32 num_out_syncobjs = exbuf->num_out_syncobjs;
	size_t syncobj_stride = exbuf->syncobj_stride;
	int ret = 0, i;

	if (!num_out_syncobjs)
		return 0;

	post_deps = kvcalloc(num_out_syncobjs, sizeof(*post_deps), GFP_KERNEL);
	if (!post_deps)
		return -ENOMEM;

	for (i = 0; i < num_out_syncobjs; i++) {
		u64 address = exbuf->out_syncobjs + i * syncobj_stride;

		memset(&syncobj_desc, 0, sizeof(syncobj_desc));

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
			post_deps[i].chain = dma_fence_chain_alloc();
			if (!post_deps[i].chain) {
				ret = -ENOMEM;
				break;
			}
		}

		post_deps[i].syncobj = drm_syncobj_find(submit->file,
							syncobj_desc.handle);
		if (!post_deps[i].syncobj) {
			kfree(post_deps[i].chain);
			ret = -EINVAL;
			break;
		}
	}

	if (ret) {
		virtio_gpu_free_post_deps(post_deps, i);
		return ret;
	}

	submit->num_out_syncobjs = num_out_syncobjs;
	submit->post_deps = post_deps;

	return 0;
}

static void
virtio_gpu_process_post_deps(struct virtio_gpu_submit *submit)
{
	struct virtio_gpu_submit_post_dep *post_deps = submit->post_deps;

	if (post_deps) {
		struct dma_fence *fence = &submit->out_fence->f;
		u32 i;

		for (i = 0; i < submit->num_out_syncobjs; i++) {
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
}

static int virtio_gpu_fence_event_create(struct drm_device *dev,
					 struct drm_file *file,
					 struct virtio_gpu_fence *fence,
					 u32 ring_idx)
{
	struct virtio_gpu_fence_event *e = NULL;
	int ret;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	e->event.type = VIRTGPU_EVENT_FENCE_SIGNALED;
	e->event.length = sizeof(e->event);

	ret = drm_event_reserve_init(dev, file, &e->base, &e->event);
	if (ret) {
		kfree(e);
		return ret;
	}

	fence->e = e;

	return 0;
}

static int virtio_gpu_init_submit_buflist(struct virtio_gpu_submit *submit)
{
	struct drm_virtgpu_execbuffer *exbuf = submit->exbuf;
	u32 *bo_handles;

	if (!exbuf->num_bo_handles)
		return 0;

	bo_handles = kvmalloc_array(exbuf->num_bo_handles, sizeof(*bo_handles),
				    GFP_KERNEL);
	if (!bo_handles)
		return -ENOMEM;

	if (copy_from_user(bo_handles, u64_to_user_ptr(exbuf->bo_handles),
			   exbuf->num_bo_handles * sizeof(*bo_handles))) {
		kvfree(bo_handles);
		return -EFAULT;
	}

	submit->buflist = virtio_gpu_array_from_handles(submit->file, bo_handles,
							exbuf->num_bo_handles);
	if (!submit->buflist) {
		kvfree(bo_handles);
		return -ENOENT;
	}

	kvfree(bo_handles);

	return 0;
}

static void virtio_gpu_cleanup_submit(struct virtio_gpu_submit *submit)
{
	virtio_gpu_reset_syncobjs(submit->in_syncobjs, submit->num_in_syncobjs);
	virtio_gpu_free_syncobjs(submit->in_syncobjs, submit->num_in_syncobjs);
	virtio_gpu_free_post_deps(submit->post_deps, submit->num_out_syncobjs);

	if (!IS_ERR(submit->buf))
		kvfree(submit->buf);

	if (submit->buflist)
		virtio_gpu_array_put_free(submit->buflist);

	if (submit->out_fence_fd >= 0)
		put_unused_fd(submit->out_fence_fd);

	if (submit->out_fence)
		dma_fence_put(&submit->out_fence->f);

	if (submit->sync_file)
		fput(submit->sync_file->file);
}

static void virtio_gpu_submit(struct virtio_gpu_submit *submit)
{
	virtio_gpu_cmd_submit(submit->vgdev, submit->buf, submit->exbuf->size,
			      submit->vfpriv->ctx_id, submit->buflist,
			      submit->out_fence);
	virtio_gpu_notify(submit->vgdev);
}

static void virtio_gpu_complete_submit(struct virtio_gpu_submit *submit)
{
	submit->buf = NULL;
	submit->buflist = NULL;
	submit->sync_file = NULL;
	submit->out_fence_fd = -1;
}

static int virtio_gpu_init_submit(struct virtio_gpu_submit *submit,
				  struct drm_virtgpu_execbuffer *exbuf,
				  struct drm_device *dev,
				  struct drm_file *file,
				  u64 fence_ctx, u32 ring_idx)
{
	struct virtio_gpu_fpriv *vfpriv = file->driver_priv;
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_fence *out_fence;
	bool drm_fence_event;
	int err;

	memset(submit, 0, sizeof(*submit));

	if ((exbuf->flags & VIRTGPU_EXECBUF_RING_IDX) &&
	    (vfpriv->ring_idx_mask & BIT_ULL(ring_idx)))
		drm_fence_event = true;
	else
		drm_fence_event = false;

	if ((exbuf->flags & VIRTGPU_EXECBUF_FENCE_FD_OUT) ||
	    exbuf->num_out_syncobjs ||
	    exbuf->num_bo_handles ||
	    drm_fence_event)
		out_fence = virtio_gpu_fence_alloc(vgdev, fence_ctx, ring_idx);
	else
		out_fence = NULL;

	if (drm_fence_event) {
		err = virtio_gpu_fence_event_create(dev, file, out_fence, ring_idx);
		if (err) {
			dma_fence_put(&out_fence->f);
			return err;
		}
	}

	submit->out_fence = out_fence;
	submit->fence_ctx = fence_ctx;
	submit->ring_idx = ring_idx;
	submit->out_fence_fd = -1;
	submit->vfpriv = vfpriv;
	submit->vgdev = vgdev;
	submit->exbuf = exbuf;
	submit->file = file;

	err = virtio_gpu_init_submit_buflist(submit);
	if (err)
		return err;

	submit->buf = vmemdup_user(u64_to_user_ptr(exbuf->command), exbuf->size);
	if (IS_ERR(submit->buf))
		return PTR_ERR(submit->buf);

	if (exbuf->flags & VIRTGPU_EXECBUF_FENCE_FD_OUT) {
		err = get_unused_fd_flags(O_CLOEXEC);
		if (err < 0)
			return err;

		submit->out_fence_fd = err;

		submit->sync_file = sync_file_create(&out_fence->f);
		if (!submit->sync_file)
			return -ENOMEM;
	}

	return 0;
}

static int virtio_gpu_wait_in_fence(struct virtio_gpu_submit *submit)
{
	int ret = 0;

	if (submit->exbuf->flags & VIRTGPU_EXECBUF_FENCE_FD_IN) {
		struct dma_fence *in_fence =
				sync_file_get_fence(submit->exbuf->fence_fd);
		if (!in_fence)
			return -EINVAL;

		/*
		 * Wait if the fence is from a foreign context, or if the
		 * fence array contains any fence from a foreign context.
		 */
		ret = virtio_gpu_dma_fence_wait(submit, in_fence);

		dma_fence_put(in_fence);
	}

	return ret;
}

static void virtio_gpu_install_out_fence_fd(struct virtio_gpu_submit *submit)
{
	if (submit->sync_file) {
		submit->exbuf->fence_fd = submit->out_fence_fd;
		fd_install(submit->out_fence_fd, submit->sync_file->file);
	}
}

static int virtio_gpu_lock_buflist(struct virtio_gpu_submit *submit)
{
	if (submit->buflist)
		return virtio_gpu_array_lock_resv(submit->buflist);

	return 0;
}

int virtio_gpu_execbuffer_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_fpriv *vfpriv = file->driver_priv;
	u64 fence_ctx = vgdev->fence_drv.context;
	struct drm_virtgpu_execbuffer *exbuf = data;
	struct virtio_gpu_submit submit;
	u32 ring_idx = 0;
	int ret = -EINVAL;

	if (!vgdev->has_virgl_3d)
		return -ENOSYS;

	if (exbuf->flags & ~VIRTGPU_EXECBUF_FLAGS)
		return ret;

	if (exbuf->flags & VIRTGPU_EXECBUF_RING_IDX) {
		if (exbuf->ring_idx >= vfpriv->num_rings)
			return ret;

		if (!vfpriv->base_fence_ctx)
			return ret;

		fence_ctx = vfpriv->base_fence_ctx;
		ring_idx = exbuf->ring_idx;
	}

	virtio_gpu_create_context(dev, file);

	ret = virtio_gpu_init_submit(&submit, exbuf, dev, file,
				     fence_ctx, ring_idx);
	if (ret)
		goto cleanup;

	ret = virtio_gpu_parse_post_deps(&submit);
	if (ret)
		goto cleanup;

	ret = virtio_gpu_parse_deps(&submit);
	if (ret)
		goto cleanup;

	/*
	 * Await in-fences in the end of the job submission path to
	 * optimize the path by proceeding directly to the submission
	 * to virtio after the waits.
	 */
	ret = virtio_gpu_wait_in_fence(&submit);
	if (ret)
		goto cleanup;

	ret = virtio_gpu_lock_buflist(&submit);
	if (ret)
		goto cleanup;

	virtio_gpu_submit(&submit);

	/*
	 * Set up user-out data after submitting the job to optimize
	 * the job submission path.
	 */
	virtio_gpu_install_out_fence_fd(&submit);
	virtio_gpu_process_post_deps(&submit);
	virtio_gpu_complete_submit(&submit);
cleanup:
	virtio_gpu_cleanup_submit(&submit);

	return ret;
}
