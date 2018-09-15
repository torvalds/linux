// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2014-2018 Broadcom */

#include <drm/drmP.h>
#include <drm/drm_syncobj.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/sched/signal.h>

#include "uapi/drm/v3d_drm.h"
#include "v3d_drv.h"
#include "v3d_regs.h"
#include "v3d_trace.h"

static void
v3d_init_core(struct v3d_dev *v3d, int core)
{
	/* Set OVRTMUOUT, which means that the texture sampler uniform
	 * configuration's tmu output type field is used, instead of
	 * using the hardware default behavior based on the texture
	 * type.  If you want the default behavior, you can still put
	 * "2" in the indirect texture state's output_type field.
	 */
	V3D_CORE_WRITE(core, V3D_CTL_MISCCFG, V3D_MISCCFG_OVRTMUOUT);

	/* Whenever we flush the L2T cache, we always want to flush
	 * the whole thing.
	 */
	V3D_CORE_WRITE(core, V3D_CTL_L2TFLSTA, 0);
	V3D_CORE_WRITE(core, V3D_CTL_L2TFLEND, ~0);
}

/* Sets invariant state for the HW. */
static void
v3d_init_hw_state(struct v3d_dev *v3d)
{
	v3d_init_core(v3d, 0);
}

static void
v3d_idle_axi(struct v3d_dev *v3d, int core)
{
	V3D_CORE_WRITE(core, V3D_GMP_CFG, V3D_GMP_CFG_STOP_REQ);

	if (wait_for((V3D_CORE_READ(core, V3D_GMP_STATUS) &
		      (V3D_GMP_STATUS_RD_COUNT_MASK |
		       V3D_GMP_STATUS_WR_COUNT_MASK |
		       V3D_GMP_STATUS_CFG_BUSY)) == 0, 100)) {
		DRM_ERROR("Failed to wait for safe GMP shutdown\n");
	}
}

static void
v3d_idle_gca(struct v3d_dev *v3d)
{
	if (v3d->ver >= 41)
		return;

	V3D_GCA_WRITE(V3D_GCA_SAFE_SHUTDOWN, V3D_GCA_SAFE_SHUTDOWN_EN);

	if (wait_for((V3D_GCA_READ(V3D_GCA_SAFE_SHUTDOWN_ACK) &
		      V3D_GCA_SAFE_SHUTDOWN_ACK_ACKED) ==
		     V3D_GCA_SAFE_SHUTDOWN_ACK_ACKED, 100)) {
		DRM_ERROR("Failed to wait for safe GCA shutdown\n");
	}
}

static void
v3d_reset_v3d(struct v3d_dev *v3d)
{
	int version = V3D_BRIDGE_READ(V3D_TOP_GR_BRIDGE_REVISION);

	if (V3D_GET_FIELD(version, V3D_TOP_GR_BRIDGE_MAJOR) == 2) {
		V3D_BRIDGE_WRITE(V3D_TOP_GR_BRIDGE_SW_INIT_0,
				 V3D_TOP_GR_BRIDGE_SW_INIT_0_V3D_CLK_108_SW_INIT);
		V3D_BRIDGE_WRITE(V3D_TOP_GR_BRIDGE_SW_INIT_0, 0);

		/* GFXH-1383: The SW_INIT may cause a stray write to address 0
		 * of the unit, so reset it to its power-on value here.
		 */
		V3D_WRITE(V3D_HUB_AXICFG, V3D_HUB_AXICFG_MAX_LEN_MASK);
	} else {
		WARN_ON_ONCE(V3D_GET_FIELD(version,
					   V3D_TOP_GR_BRIDGE_MAJOR) != 7);
		V3D_BRIDGE_WRITE(V3D_TOP_GR_BRIDGE_SW_INIT_1,
				 V3D_TOP_GR_BRIDGE_SW_INIT_1_V3D_CLK_108_SW_INIT);
		V3D_BRIDGE_WRITE(V3D_TOP_GR_BRIDGE_SW_INIT_1, 0);
	}

	v3d_init_hw_state(v3d);
}

void
v3d_reset(struct v3d_dev *v3d)
{
	struct drm_device *dev = &v3d->drm;

	DRM_ERROR("Resetting GPU.\n");
	trace_v3d_reset_begin(dev);

	/* XXX: only needed for safe powerdown, not reset. */
	if (false)
		v3d_idle_axi(v3d, 0);

	v3d_idle_gca(v3d);
	v3d_reset_v3d(v3d);

	v3d_mmu_set_page_table(v3d);
	v3d_irq_reset(v3d);

	trace_v3d_reset_end(dev);
}

static void
v3d_flush_l3(struct v3d_dev *v3d)
{
	if (v3d->ver < 41) {
		u32 gca_ctrl = V3D_GCA_READ(V3D_GCA_CACHE_CTRL);

		V3D_GCA_WRITE(V3D_GCA_CACHE_CTRL,
			      gca_ctrl | V3D_GCA_CACHE_CTRL_FLUSH);

		if (v3d->ver < 33) {
			V3D_GCA_WRITE(V3D_GCA_CACHE_CTRL,
				      gca_ctrl & ~V3D_GCA_CACHE_CTRL_FLUSH);
		}
	}
}

/* Invalidates the (read-only) L2 cache. */
static void
v3d_invalidate_l2(struct v3d_dev *v3d, int core)
{
	V3D_CORE_WRITE(core, V3D_CTL_L2CACTL,
		       V3D_L2CACTL_L2CCLR |
		       V3D_L2CACTL_L2CENA);
}

static void
v3d_invalidate_l1td(struct v3d_dev *v3d, int core)
{
	V3D_CORE_WRITE(core, V3D_CTL_L2TCACTL, V3D_L2TCACTL_TMUWCF);
	if (wait_for(!(V3D_CORE_READ(core, V3D_CTL_L2TCACTL) &
		       V3D_L2TCACTL_L2TFLS), 100)) {
		DRM_ERROR("Timeout waiting for L1T write combiner flush\n");
	}
}

/* Invalidates texture L2 cachelines */
static void
v3d_flush_l2t(struct v3d_dev *v3d, int core)
{
	v3d_invalidate_l1td(v3d, core);

	V3D_CORE_WRITE(core, V3D_CTL_L2TCACTL,
		       V3D_L2TCACTL_L2TFLS |
		       V3D_SET_FIELD(V3D_L2TCACTL_FLM_FLUSH, V3D_L2TCACTL_FLM));
	if (wait_for(!(V3D_CORE_READ(core, V3D_CTL_L2TCACTL) &
		       V3D_L2TCACTL_L2TFLS), 100)) {
		DRM_ERROR("Timeout waiting for L2T flush\n");
	}
}

/* Invalidates the slice caches.  These are read-only caches. */
static void
v3d_invalidate_slices(struct v3d_dev *v3d, int core)
{
	V3D_CORE_WRITE(core, V3D_CTL_SLCACTL,
		       V3D_SET_FIELD(0xf, V3D_SLCACTL_TVCCS) |
		       V3D_SET_FIELD(0xf, V3D_SLCACTL_TDCCS) |
		       V3D_SET_FIELD(0xf, V3D_SLCACTL_UCC) |
		       V3D_SET_FIELD(0xf, V3D_SLCACTL_ICC));
}

/* Invalidates texture L2 cachelines */
static void
v3d_invalidate_l2t(struct v3d_dev *v3d, int core)
{
	V3D_CORE_WRITE(core,
		       V3D_CTL_L2TCACTL,
		       V3D_L2TCACTL_L2TFLS |
		       V3D_SET_FIELD(V3D_L2TCACTL_FLM_CLEAR, V3D_L2TCACTL_FLM));
	if (wait_for(!(V3D_CORE_READ(core, V3D_CTL_L2TCACTL) &
		       V3D_L2TCACTL_L2TFLS), 100)) {
		DRM_ERROR("Timeout waiting for L2T invalidate\n");
	}
}

void
v3d_invalidate_caches(struct v3d_dev *v3d)
{
	v3d_flush_l3(v3d);

	v3d_invalidate_l2(v3d, 0);
	v3d_invalidate_slices(v3d, 0);
	v3d_flush_l2t(v3d, 0);
}

void
v3d_flush_caches(struct v3d_dev *v3d)
{
	v3d_invalidate_l1td(v3d, 0);
	v3d_invalidate_l2t(v3d, 0);
}

static void
v3d_attach_object_fences(struct v3d_exec_info *exec)
{
	struct dma_fence *out_fence = &exec->render.base.s_fence->finished;
	struct v3d_bo *bo;
	int i;

	for (i = 0; i < exec->bo_count; i++) {
		bo = to_v3d_bo(&exec->bo[i]->base);

		/* XXX: Use shared fences for read-only objects. */
		reservation_object_add_excl_fence(bo->resv, out_fence);
	}
}

static void
v3d_unlock_bo_reservations(struct drm_device *dev,
			   struct v3d_exec_info *exec,
			   struct ww_acquire_ctx *acquire_ctx)
{
	int i;

	for (i = 0; i < exec->bo_count; i++) {
		struct v3d_bo *bo = to_v3d_bo(&exec->bo[i]->base);

		ww_mutex_unlock(&bo->resv->lock);
	}

	ww_acquire_fini(acquire_ctx);
}

/* Takes the reservation lock on all the BOs being referenced, so that
 * at queue submit time we can update the reservations.
 *
 * We don't lock the RCL the tile alloc/state BOs, or overflow memory
 * (all of which are on exec->unref_list).  They're entirely private
 * to v3d, so we don't attach dma-buf fences to them.
 */
static int
v3d_lock_bo_reservations(struct drm_device *dev,
			 struct v3d_exec_info *exec,
			 struct ww_acquire_ctx *acquire_ctx)
{
	int contended_lock = -1;
	int i, ret;
	struct v3d_bo *bo;

	ww_acquire_init(acquire_ctx, &reservation_ww_class);

retry:
	if (contended_lock != -1) {
		bo = to_v3d_bo(&exec->bo[contended_lock]->base);
		ret = ww_mutex_lock_slow_interruptible(&bo->resv->lock,
						       acquire_ctx);
		if (ret) {
			ww_acquire_done(acquire_ctx);
			return ret;
		}
	}

	for (i = 0; i < exec->bo_count; i++) {
		if (i == contended_lock)
			continue;

		bo = to_v3d_bo(&exec->bo[i]->base);

		ret = ww_mutex_lock_interruptible(&bo->resv->lock, acquire_ctx);
		if (ret) {
			int j;

			for (j = 0; j < i; j++) {
				bo = to_v3d_bo(&exec->bo[j]->base);
				ww_mutex_unlock(&bo->resv->lock);
			}

			if (contended_lock != -1 && contended_lock >= i) {
				bo = to_v3d_bo(&exec->bo[contended_lock]->base);

				ww_mutex_unlock(&bo->resv->lock);
			}

			if (ret == -EDEADLK) {
				contended_lock = i;
				goto retry;
			}

			ww_acquire_done(acquire_ctx);
			return ret;
		}
	}

	ww_acquire_done(acquire_ctx);

	/* Reserve space for our shared (read-only) fence references,
	 * before we commit the CL to the hardware.
	 */
	for (i = 0; i < exec->bo_count; i++) {
		bo = to_v3d_bo(&exec->bo[i]->base);

		ret = reservation_object_reserve_shared(bo->resv);
		if (ret) {
			v3d_unlock_bo_reservations(dev, exec, acquire_ctx);
			return ret;
		}
	}

	return 0;
}

/**
 * v3d_cl_lookup_bos() - Sets up exec->bo[] with the GEM objects
 * referenced by the job.
 * @dev: DRM device
 * @file_priv: DRM file for this fd
 * @exec: V3D job being set up
 *
 * The command validator needs to reference BOs by their index within
 * the submitted job's BO list.  This does the validation of the job's
 * BO list and reference counting for the lifetime of the job.
 *
 * Note that this function doesn't need to unreference the BOs on
 * failure, because that will happen at v3d_exec_cleanup() time.
 */
static int
v3d_cl_lookup_bos(struct drm_device *dev,
		  struct drm_file *file_priv,
		  struct drm_v3d_submit_cl *args,
		  struct v3d_exec_info *exec)
{
	u32 *handles;
	int ret = 0;
	int i;

	exec->bo_count = args->bo_handle_count;

	if (!exec->bo_count) {
		/* See comment on bo_index for why we have to check
		 * this.
		 */
		DRM_DEBUG("Rendering requires BOs\n");
		return -EINVAL;
	}

	exec->bo = kvmalloc_array(exec->bo_count,
				  sizeof(struct drm_gem_cma_object *),
				  GFP_KERNEL | __GFP_ZERO);
	if (!exec->bo) {
		DRM_DEBUG("Failed to allocate validated BO pointers\n");
		return -ENOMEM;
	}

	handles = kvmalloc_array(exec->bo_count, sizeof(u32), GFP_KERNEL);
	if (!handles) {
		ret = -ENOMEM;
		DRM_DEBUG("Failed to allocate incoming GEM handles\n");
		goto fail;
	}

	if (copy_from_user(handles,
			   (void __user *)(uintptr_t)args->bo_handles,
			   exec->bo_count * sizeof(u32))) {
		ret = -EFAULT;
		DRM_DEBUG("Failed to copy in GEM handles\n");
		goto fail;
	}

	spin_lock(&file_priv->table_lock);
	for (i = 0; i < exec->bo_count; i++) {
		struct drm_gem_object *bo = idr_find(&file_priv->object_idr,
						     handles[i]);
		if (!bo) {
			DRM_DEBUG("Failed to look up GEM BO %d: %d\n",
				  i, handles[i]);
			ret = -ENOENT;
			spin_unlock(&file_priv->table_lock);
			goto fail;
		}
		drm_gem_object_get(bo);
		exec->bo[i] = to_v3d_bo(bo);
	}
	spin_unlock(&file_priv->table_lock);

fail:
	kvfree(handles);
	return ret;
}

static void
v3d_exec_cleanup(struct kref *ref)
{
	struct v3d_exec_info *exec = container_of(ref, struct v3d_exec_info,
						  refcount);
	struct v3d_dev *v3d = exec->v3d;
	unsigned int i;
	struct v3d_bo *bo, *save;

	dma_fence_put(exec->bin.in_fence);
	dma_fence_put(exec->render.in_fence);

	dma_fence_put(exec->bin.done_fence);
	dma_fence_put(exec->render.done_fence);

	dma_fence_put(exec->bin_done_fence);

	for (i = 0; i < exec->bo_count; i++)
		drm_gem_object_put_unlocked(&exec->bo[i]->base);
	kvfree(exec->bo);

	list_for_each_entry_safe(bo, save, &exec->unref_list, unref_head) {
		drm_gem_object_put_unlocked(&bo->base);
	}

	pm_runtime_mark_last_busy(v3d->dev);
	pm_runtime_put_autosuspend(v3d->dev);

	kfree(exec);
}

void v3d_exec_put(struct v3d_exec_info *exec)
{
	kref_put(&exec->refcount, v3d_exec_cleanup);
}

int
v3d_wait_bo_ioctl(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	int ret;
	struct drm_v3d_wait_bo *args = data;
	struct drm_gem_object *gem_obj;
	struct v3d_bo *bo;
	ktime_t start = ktime_get();
	u64 delta_ns;
	unsigned long timeout_jiffies =
		nsecs_to_jiffies_timeout(args->timeout_ns);

	if (args->pad != 0)
		return -EINVAL;

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -EINVAL;
	}
	bo = to_v3d_bo(gem_obj);

	ret = reservation_object_wait_timeout_rcu(bo->resv,
						  true, true,
						  timeout_jiffies);

	if (ret == 0)
		ret = -ETIME;
	else if (ret > 0)
		ret = 0;

	/* Decrement the user's timeout, in case we got interrupted
	 * such that the ioctl will be restarted.
	 */
	delta_ns = ktime_to_ns(ktime_sub(ktime_get(), start));
	if (delta_ns < args->timeout_ns)
		args->timeout_ns -= delta_ns;
	else
		args->timeout_ns = 0;

	/* Asked to wait beyond the jiffie/scheduler precision? */
	if (ret == -ETIME && args->timeout_ns)
		ret = -EAGAIN;

	drm_gem_object_put_unlocked(gem_obj);

	return ret;
}

/**
 * v3d_submit_cl_ioctl() - Submits a job (frame) to the V3D.
 * @dev: DRM device
 * @data: ioctl argument
 * @file_priv: DRM file for this fd
 *
 * This is the main entrypoint for userspace to submit a 3D frame to
 * the GPU.  Userspace provides the binner command list (if
 * applicable), and the kernel sets up the render command list to draw
 * to the framebuffer described in the ioctl, using the command lists
 * that the 3D engine's binner will produce.
 */
int
v3d_submit_cl_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct v3d_dev *v3d = to_v3d_dev(dev);
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct drm_v3d_submit_cl *args = data;
	struct v3d_exec_info *exec;
	struct ww_acquire_ctx acquire_ctx;
	struct drm_syncobj *sync_out;
	int ret = 0;

	if (args->pad != 0) {
		DRM_INFO("pad must be zero: %d\n", args->pad);
		return -EINVAL;
	}

	exec = kcalloc(1, sizeof(*exec), GFP_KERNEL);
	if (!exec)
		return -ENOMEM;

	ret = pm_runtime_get_sync(v3d->dev);
	if (ret < 0) {
		kfree(exec);
		return ret;
	}

	kref_init(&exec->refcount);

	ret = drm_syncobj_find_fence(file_priv, args->in_sync_bcl,
				     &exec->bin.in_fence);
	if (ret == -EINVAL)
		goto fail;

	ret = drm_syncobj_find_fence(file_priv, args->in_sync_rcl,
				     &exec->render.in_fence);
	if (ret == -EINVAL)
		goto fail;

	exec->qma = args->qma;
	exec->qms = args->qms;
	exec->qts = args->qts;
	exec->bin.exec = exec;
	exec->bin.start = args->bcl_start;
	exec->bin.end = args->bcl_end;
	exec->render.exec = exec;
	exec->render.start = args->rcl_start;
	exec->render.end = args->rcl_end;
	exec->v3d = v3d;
	INIT_LIST_HEAD(&exec->unref_list);

	ret = v3d_cl_lookup_bos(dev, file_priv, args, exec);
	if (ret)
		goto fail;

	ret = v3d_lock_bo_reservations(dev, exec, &acquire_ctx);
	if (ret)
		goto fail;

	mutex_lock(&v3d->sched_lock);
	if (exec->bin.start != exec->bin.end) {
		ret = drm_sched_job_init(&exec->bin.base,
					 &v3d_priv->sched_entity[V3D_BIN],
					 v3d_priv);
		if (ret)
			goto fail_unreserve;

		exec->bin_done_fence =
			dma_fence_get(&exec->bin.base.s_fence->finished);

		kref_get(&exec->refcount); /* put by scheduler job completion */
		drm_sched_entity_push_job(&exec->bin.base,
					  &v3d_priv->sched_entity[V3D_BIN]);
	}

	ret = drm_sched_job_init(&exec->render.base,
				 &v3d_priv->sched_entity[V3D_RENDER],
				 v3d_priv);
	if (ret)
		goto fail_unreserve;

	kref_get(&exec->refcount); /* put by scheduler job completion */
	drm_sched_entity_push_job(&exec->render.base,
				  &v3d_priv->sched_entity[V3D_RENDER]);
	mutex_unlock(&v3d->sched_lock);

	v3d_attach_object_fences(exec);

	v3d_unlock_bo_reservations(dev, exec, &acquire_ctx);

	/* Update the return sync object for the */
	sync_out = drm_syncobj_find(file_priv, args->out_sync);
	if (sync_out) {
		drm_syncobj_replace_fence(sync_out,
					  &exec->render.base.s_fence->finished);
		drm_syncobj_put(sync_out);
	}

	v3d_exec_put(exec);

	return 0;

fail_unreserve:
	mutex_unlock(&v3d->sched_lock);
	v3d_unlock_bo_reservations(dev, exec, &acquire_ctx);
fail:
	v3d_exec_put(exec);

	return ret;
}

int
v3d_gem_init(struct drm_device *dev)
{
	struct v3d_dev *v3d = to_v3d_dev(dev);
	u32 pt_size = 4096 * 1024;
	int ret, i;

	for (i = 0; i < V3D_MAX_QUEUES; i++)
		v3d->queue[i].fence_context = dma_fence_context_alloc(1);

	spin_lock_init(&v3d->mm_lock);
	spin_lock_init(&v3d->job_lock);
	mutex_init(&v3d->bo_lock);
	mutex_init(&v3d->reset_lock);
	mutex_init(&v3d->sched_lock);

	/* Note: We don't allocate address 0.  Various bits of HW
	 * treat 0 as special, such as the occlusion query counters
	 * where 0 means "disabled".
	 */
	drm_mm_init(&v3d->mm, 1, pt_size / sizeof(u32) - 1);

	v3d->pt = dma_alloc_wc(v3d->dev, pt_size,
			       &v3d->pt_paddr,
			       GFP_KERNEL | __GFP_NOWARN | __GFP_ZERO);
	if (!v3d->pt) {
		drm_mm_takedown(&v3d->mm);
		dev_err(v3d->dev,
			"Failed to allocate page tables. "
			"Please ensure you have CMA enabled.\n");
		return -ENOMEM;
	}

	v3d_init_hw_state(v3d);
	v3d_mmu_set_page_table(v3d);

	ret = v3d_sched_init(v3d);
	if (ret) {
		drm_mm_takedown(&v3d->mm);
		dma_free_coherent(v3d->dev, 4096 * 1024, (void *)v3d->pt,
				  v3d->pt_paddr);
	}

	return 0;
}

void
v3d_gem_destroy(struct drm_device *dev)
{
	struct v3d_dev *v3d = to_v3d_dev(dev);

	v3d_sched_fini(v3d);

	/* Waiting for exec to finish would need to be done before
	 * unregistering V3D.
	 */
	WARN_ON(v3d->bin_job);
	WARN_ON(v3d->render_job);

	drm_mm_takedown(&v3d->mm);

	dma_free_coherent(v3d->dev, 4096 * 1024, (void *)v3d->pt, v3d->pt_paddr);
}
