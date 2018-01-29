/*
 * Copyright (C) 2015 Etnaviv Project
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/dma-fence-array.h>
#include <linux/reservation.h>
#include <linux/sync_file.h>
#include "etnaviv_cmdbuf.h"
#include "etnaviv_drv.h"
#include "etnaviv_gpu.h"
#include "etnaviv_gem.h"
#include "etnaviv_perfmon.h"

/*
 * Cmdstream submission:
 */

#define BO_INVALID_FLAGS ~(ETNA_SUBMIT_BO_READ | ETNA_SUBMIT_BO_WRITE)
/* make sure these don't conflict w/ ETNAVIV_SUBMIT_BO_x */
#define BO_LOCKED   0x4000
#define BO_PINNED   0x2000

static struct etnaviv_gem_submit *submit_create(struct drm_device *dev,
		struct etnaviv_gpu *gpu, size_t nr_bos, size_t nr_pmrs)
{
	struct etnaviv_gem_submit *submit;
	size_t sz = size_vstruct(nr_bos, sizeof(submit->bos[0]), sizeof(*submit));

	submit = kzalloc(sz, GFP_KERNEL);
	if (!submit)
		return NULL;

	submit->pmrs = kcalloc(nr_pmrs, sizeof(struct etnaviv_perfmon_request),
			       GFP_KERNEL);
	if (!submit->pmrs) {
		kfree(submit);
		return NULL;
	}
	submit->nr_pmrs = nr_pmrs;

	submit->gpu = gpu;
	kref_init(&submit->refcount);

	return submit;
}

static int submit_lookup_objects(struct etnaviv_gem_submit *submit,
	struct drm_file *file, struct drm_etnaviv_gem_submit_bo *submit_bos,
	unsigned nr_bos)
{
	struct drm_etnaviv_gem_submit_bo *bo;
	unsigned i;
	int ret = 0;

	spin_lock(&file->table_lock);

	for (i = 0, bo = submit_bos; i < nr_bos; i++, bo++) {
		struct drm_gem_object *obj;

		if (bo->flags & BO_INVALID_FLAGS) {
			DRM_ERROR("invalid flags: %x\n", bo->flags);
			ret = -EINVAL;
			goto out_unlock;
		}

		submit->bos[i].flags = bo->flags;

		/* normally use drm_gem_object_lookup(), but for bulk lookup
		 * all under single table_lock just hit object_idr directly:
		 */
		obj = idr_find(&file->object_idr, bo->handle);
		if (!obj) {
			DRM_ERROR("invalid handle %u at index %u\n",
				  bo->handle, i);
			ret = -EINVAL;
			goto out_unlock;
		}

		/*
		 * Take a refcount on the object. The file table lock
		 * prevents the object_idr's refcount on this being dropped.
		 */
		drm_gem_object_get(obj);

		submit->bos[i].obj = to_etnaviv_bo(obj);
	}

out_unlock:
	submit->nr_bos = i;
	spin_unlock(&file->table_lock);

	return ret;
}

static void submit_unlock_object(struct etnaviv_gem_submit *submit, int i)
{
	if (submit->bos[i].flags & BO_LOCKED) {
		struct etnaviv_gem_object *etnaviv_obj = submit->bos[i].obj;

		ww_mutex_unlock(&etnaviv_obj->resv->lock);
		submit->bos[i].flags &= ~BO_LOCKED;
	}
}

static int submit_lock_objects(struct etnaviv_gem_submit *submit,
		struct ww_acquire_ctx *ticket)
{
	int contended, slow_locked = -1, i, ret = 0;

retry:
	for (i = 0; i < submit->nr_bos; i++) {
		struct etnaviv_gem_object *etnaviv_obj = submit->bos[i].obj;

		if (slow_locked == i)
			slow_locked = -1;

		contended = i;

		if (!(submit->bos[i].flags & BO_LOCKED)) {
			ret = ww_mutex_lock_interruptible(&etnaviv_obj->resv->lock,
							  ticket);
			if (ret == -EALREADY)
				DRM_ERROR("BO at index %u already on submit list\n",
					  i);
			if (ret)
				goto fail;
			submit->bos[i].flags |= BO_LOCKED;
		}
	}

	ww_acquire_done(ticket);

	return 0;

fail:
	for (; i >= 0; i--)
		submit_unlock_object(submit, i);

	if (slow_locked > 0)
		submit_unlock_object(submit, slow_locked);

	if (ret == -EDEADLK) {
		struct etnaviv_gem_object *etnaviv_obj;

		etnaviv_obj = submit->bos[contended].obj;

		/* we lost out in a seqno race, lock and retry.. */
		ret = ww_mutex_lock_slow_interruptible(&etnaviv_obj->resv->lock,
						       ticket);
		if (!ret) {
			submit->bos[contended].flags |= BO_LOCKED;
			slow_locked = contended;
			goto retry;
		}
	}

	return ret;
}

static int submit_fence_sync(const struct etnaviv_gem_submit *submit)
{
	unsigned int context = submit->gpu->fence_context;
	int i, ret = 0;

	for (i = 0; i < submit->nr_bos; i++) {
		struct etnaviv_gem_object *etnaviv_obj = submit->bos[i].obj;
		bool write = submit->bos[i].flags & ETNA_SUBMIT_BO_WRITE;
		bool explicit = !!(submit->flags & ETNA_SUBMIT_NO_IMPLICIT);

		ret = etnaviv_gpu_fence_sync_obj(etnaviv_obj, context, write,
						 explicit);
		if (ret)
			break;
	}

	if (submit->flags & ETNA_SUBMIT_FENCE_FD_IN) {
		/*
		 * Wait if the fence is from a foreign context, or if the fence
		 * array contains any fence from a foreign context.
		 */
		if (!dma_fence_match_context(submit->in_fence, context))
			ret = dma_fence_wait(submit->in_fence, true);
	}

	return ret;
}

static void submit_attach_object_fences(struct etnaviv_gem_submit *submit)
{
	int i;

	for (i = 0; i < submit->nr_bos; i++) {
		struct etnaviv_gem_object *etnaviv_obj = submit->bos[i].obj;

		if (submit->bos[i].flags & ETNA_SUBMIT_BO_WRITE)
			reservation_object_add_excl_fence(etnaviv_obj->resv,
							  submit->out_fence);
		else
			reservation_object_add_shared_fence(etnaviv_obj->resv,
							    submit->out_fence);

		submit_unlock_object(submit, i);
	}
}

static int submit_pin_objects(struct etnaviv_gem_submit *submit)
{
	int i, ret = 0;

	for (i = 0; i < submit->nr_bos; i++) {
		struct etnaviv_gem_object *etnaviv_obj = submit->bos[i].obj;
		struct etnaviv_vram_mapping *mapping;

		mapping = etnaviv_gem_mapping_get(&etnaviv_obj->base,
						  submit->gpu);
		if (IS_ERR(mapping)) {
			ret = PTR_ERR(mapping);
			break;
		}
		atomic_inc(&etnaviv_obj->gpu_active);

		submit->bos[i].flags |= BO_PINNED;
		submit->bos[i].mapping = mapping;
	}

	return ret;
}

static int submit_bo(struct etnaviv_gem_submit *submit, u32 idx,
	struct etnaviv_gem_submit_bo **bo)
{
	if (idx >= submit->nr_bos) {
		DRM_ERROR("invalid buffer index: %u (out of %u)\n",
				idx, submit->nr_bos);
		return -EINVAL;
	}

	*bo = &submit->bos[idx];

	return 0;
}

/* process the reloc's and patch up the cmdstream as needed: */
static int submit_reloc(struct etnaviv_gem_submit *submit, void *stream,
		u32 size, const struct drm_etnaviv_gem_submit_reloc *relocs,
		u32 nr_relocs)
{
	u32 i, last_offset = 0;
	u32 *ptr = stream;
	int ret;

	for (i = 0; i < nr_relocs; i++) {
		const struct drm_etnaviv_gem_submit_reloc *r = relocs + i;
		struct etnaviv_gem_submit_bo *bo;
		u32 off;

		if (unlikely(r->flags)) {
			DRM_ERROR("invalid reloc flags\n");
			return -EINVAL;
		}

		if (r->submit_offset % 4) {
			DRM_ERROR("non-aligned reloc offset: %u\n",
				  r->submit_offset);
			return -EINVAL;
		}

		/* offset in dwords: */
		off = r->submit_offset / 4;

		if ((off >= size ) ||
				(off < last_offset)) {
			DRM_ERROR("invalid offset %u at reloc %u\n", off, i);
			return -EINVAL;
		}

		ret = submit_bo(submit, r->reloc_idx, &bo);
		if (ret)
			return ret;

		if (r->reloc_offset > bo->obj->base.size - sizeof(*ptr)) {
			DRM_ERROR("relocation %u outside object\n", i);
			return -EINVAL;
		}

		ptr[off] = bo->mapping->iova + r->reloc_offset;

		last_offset = off;
	}

	return 0;
}

static int submit_perfmon_validate(struct etnaviv_gem_submit *submit,
		u32 exec_state, const struct drm_etnaviv_gem_submit_pmr *pmrs)
{
	u32 i;

	for (i = 0; i < submit->nr_pmrs; i++) {
		const struct drm_etnaviv_gem_submit_pmr *r = pmrs + i;
		struct etnaviv_gem_submit_bo *bo;
		int ret;

		ret = submit_bo(submit, r->read_idx, &bo);
		if (ret)
			return ret;

		/* at offset 0 a sequence number gets stored used for userspace sync */
		if (r->read_offset == 0) {
			DRM_ERROR("perfmon request: offset is 0");
			return -EINVAL;
		}

		if (r->read_offset >= bo->obj->base.size - sizeof(u32)) {
			DRM_ERROR("perfmon request: offset %u outside object", i);
			return -EINVAL;
		}

		if (r->flags & ~(ETNA_PM_PROCESS_PRE | ETNA_PM_PROCESS_POST)) {
			DRM_ERROR("perfmon request: flags are not valid");
			return -EINVAL;
		}

		if (etnaviv_pm_req_validate(r, exec_state)) {
			DRM_ERROR("perfmon request: domain or signal not valid");
			return -EINVAL;
		}

		submit->pmrs[i].flags = r->flags;
		submit->pmrs[i].domain = r->domain;
		submit->pmrs[i].signal = r->signal;
		submit->pmrs[i].sequence = r->sequence;
		submit->pmrs[i].offset = r->read_offset;
		submit->pmrs[i].bo_vma = etnaviv_gem_vmap(&bo->obj->base);
	}

	return 0;
}

static void submit_cleanup(struct kref *kref)
{
	struct etnaviv_gem_submit *submit =
			container_of(kref, struct etnaviv_gem_submit, refcount);
	unsigned i;

	if (submit->runtime_resumed)
		pm_runtime_put_autosuspend(submit->gpu->dev);

	if (submit->cmdbuf.suballoc)
		etnaviv_cmdbuf_free(&submit->cmdbuf);

	for (i = 0; i < submit->nr_bos; i++) {
		struct etnaviv_gem_object *etnaviv_obj = submit->bos[i].obj;

		/* unpin all objects */
		if (submit->bos[i].flags & BO_PINNED) {
			etnaviv_gem_mapping_unreference(submit->bos[i].mapping);
			atomic_dec(&etnaviv_obj->gpu_active);
			submit->bos[i].mapping = NULL;
			submit->bos[i].flags &= ~BO_PINNED;
		}

		/* if the GPU submit failed, objects might still be locked */
		submit_unlock_object(submit, i);
		drm_gem_object_put_unlocked(&etnaviv_obj->base);
	}

	wake_up_all(&submit->gpu->fence_event);

	if (submit->in_fence)
		dma_fence_put(submit->in_fence);
	if (submit->out_fence)
		dma_fence_put(submit->out_fence);
	kfree(submit->pmrs);
	kfree(submit);
}

void etnaviv_submit_put(struct etnaviv_gem_submit *submit)
{
	kref_put(&submit->refcount, submit_cleanup);
}

int etnaviv_ioctl_gem_submit(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct etnaviv_drm_private *priv = dev->dev_private;
	struct drm_etnaviv_gem_submit *args = data;
	struct drm_etnaviv_gem_submit_reloc *relocs;
	struct drm_etnaviv_gem_submit_pmr *pmrs;
	struct drm_etnaviv_gem_submit_bo *bos;
	struct etnaviv_gem_submit *submit;
	struct etnaviv_gpu *gpu;
	struct sync_file *sync_file = NULL;
	struct ww_acquire_ctx ticket;
	int out_fence_fd = -1;
	void *stream;
	int ret;

	if (args->pipe >= ETNA_MAX_PIPES)
		return -EINVAL;

	gpu = priv->gpu[args->pipe];
	if (!gpu)
		return -ENXIO;

	if (args->stream_size % 4) {
		DRM_ERROR("non-aligned cmdstream buffer size: %u\n",
			  args->stream_size);
		return -EINVAL;
	}

	if (args->exec_state != ETNA_PIPE_3D &&
	    args->exec_state != ETNA_PIPE_2D &&
	    args->exec_state != ETNA_PIPE_VG) {
		DRM_ERROR("invalid exec_state: 0x%x\n", args->exec_state);
		return -EINVAL;
	}

	if (args->flags & ~ETNA_SUBMIT_FLAGS) {
		DRM_ERROR("invalid flags: 0x%x\n", args->flags);
		return -EINVAL;
	}

	/*
	 * Copy the command submission and bo array to kernel space in
	 * one go, and do this outside of any locks.
	 */
	bos = kvmalloc_array(args->nr_bos, sizeof(*bos), GFP_KERNEL);
	relocs = kvmalloc_array(args->nr_relocs, sizeof(*relocs), GFP_KERNEL);
	pmrs = kvmalloc_array(args->nr_pmrs, sizeof(*pmrs), GFP_KERNEL);
	stream = kvmalloc_array(1, args->stream_size, GFP_KERNEL);
	if (!bos || !relocs || !pmrs || !stream) {
		ret = -ENOMEM;
		goto err_submit_cmds;
	}

	ret = copy_from_user(bos, u64_to_user_ptr(args->bos),
			     args->nr_bos * sizeof(*bos));
	if (ret) {
		ret = -EFAULT;
		goto err_submit_cmds;
	}

	ret = copy_from_user(relocs, u64_to_user_ptr(args->relocs),
			     args->nr_relocs * sizeof(*relocs));
	if (ret) {
		ret = -EFAULT;
		goto err_submit_cmds;
	}

	ret = copy_from_user(pmrs, u64_to_user_ptr(args->pmrs),
			     args->nr_pmrs * sizeof(*pmrs));
	if (ret) {
		ret = -EFAULT;
		goto err_submit_cmds;
	}

	ret = copy_from_user(stream, u64_to_user_ptr(args->stream),
			     args->stream_size);
	if (ret) {
		ret = -EFAULT;
		goto err_submit_cmds;
	}

	if (args->flags & ETNA_SUBMIT_FENCE_FD_OUT) {
		out_fence_fd = get_unused_fd_flags(O_CLOEXEC);
		if (out_fence_fd < 0) {
			ret = out_fence_fd;
			goto err_submit_cmds;
		}
	}

	ww_acquire_init(&ticket, &reservation_ww_class);

	submit = submit_create(dev, gpu, args->nr_bos, args->nr_pmrs);
	if (!submit) {
		ret = -ENOMEM;
		goto err_submit_ww_acquire;
	}

	ret = etnaviv_cmdbuf_init(gpu->cmdbuf_suballoc, &submit->cmdbuf,
				  ALIGN(args->stream_size, 8) + 8);
	if (ret)
		goto err_submit_objects;

	submit->cmdbuf.ctx = file->driver_priv;
	submit->exec_state = args->exec_state;
	submit->flags = args->flags;

	ret = submit_lookup_objects(submit, file, bos, args->nr_bos);
	if (ret)
		goto err_submit_objects;

	ret = submit_lock_objects(submit, &ticket);
	if (ret)
		goto err_submit_objects;

	if (!etnaviv_cmd_validate_one(gpu, stream, args->stream_size / 4,
				      relocs, args->nr_relocs)) {
		ret = -EINVAL;
		goto err_submit_objects;
	}

	if (args->flags & ETNA_SUBMIT_FENCE_FD_IN) {
		submit->in_fence = sync_file_get_fence(args->fence_fd);
		if (!submit->in_fence) {
			ret = -EINVAL;
			goto err_submit_objects;
		}
	}

	ret = submit_fence_sync(submit);
	if (ret)
		goto err_submit_objects;

	ret = submit_pin_objects(submit);
	if (ret)
		goto err_submit_objects;

	ret = submit_reloc(submit, stream, args->stream_size / 4,
			   relocs, args->nr_relocs);
	if (ret)
		goto err_submit_objects;

	ret = submit_perfmon_validate(submit, args->exec_state, pmrs);
	if (ret)
		goto err_submit_objects;

	memcpy(submit->cmdbuf.vaddr, stream, args->stream_size);
	submit->cmdbuf.user_size = ALIGN(args->stream_size, 8);

	ret = etnaviv_gpu_submit(gpu, submit);
	if (ret)
		goto err_submit_objects;

	submit_attach_object_fences(submit);

	if (args->flags & ETNA_SUBMIT_FENCE_FD_OUT) {
		/*
		 * This can be improved: ideally we want to allocate the sync
		 * file before kicking off the GPU job and just attach the
		 * fence to the sync file here, eliminating the ENOMEM
		 * possibility at this stage.
		 */
		sync_file = sync_file_create(submit->out_fence);
		if (!sync_file) {
			ret = -ENOMEM;
			goto err_submit_objects;
		}
		fd_install(out_fence_fd, sync_file->file);
	}

	args->fence_fd = out_fence_fd;
	args->fence = submit->out_fence->seqno;

err_submit_objects:
	etnaviv_submit_put(submit);

err_submit_ww_acquire:
	ww_acquire_fini(&ticket);

err_submit_cmds:
	if (ret && (out_fence_fd >= 0))
		put_unused_fd(out_fence_fd);
	if (stream)
		kvfree(stream);
	if (bos)
		kvfree(bos);
	if (relocs)
		kvfree(relocs);
	if (pmrs)
		kvfree(pmrs);

	return ret;
}
