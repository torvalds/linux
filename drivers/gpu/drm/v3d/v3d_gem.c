// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2014-2018 Broadcom */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>

#include <drm/drm_syncobj.h>
#include <uapi/drm/v3d_drm.h>

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
	if (v3d->ver < 40)
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
v3d_reset_by_bridge(struct v3d_dev *v3d)
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
}

static void
v3d_reset_v3d(struct v3d_dev *v3d)
{
	if (v3d->reset)
		reset_control_reset(v3d->reset);
	else
		v3d_reset_by_bridge(v3d);

	v3d_init_hw_state(v3d);
}

void
v3d_reset(struct v3d_dev *v3d)
{
	struct drm_device *dev = &v3d->drm;

	DRM_DEV_ERROR(dev->dev, "Resetting GPU for hang.\n");
	DRM_DEV_ERROR(dev->dev, "V3D_ERR_STAT: 0x%08x\n",
		      V3D_CORE_READ(0, V3D_ERR_STAT));
	trace_v3d_reset_begin(dev);

	/* XXX: only needed for safe powerdown, not reset. */
	if (false)
		v3d_idle_axi(v3d, 0);

	v3d_idle_gca(v3d);
	v3d_reset_v3d(v3d);

	v3d_mmu_set_page_table(v3d);
	v3d_irq_reset(v3d);

	v3d_perfmon_stop(v3d, v3d->active_perfmon, false);

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

/* Invalidates the (read-only) L2C cache.  This was the L2 cache for
 * uniforms and instructions on V3D 3.2.
 */
static void
v3d_invalidate_l2c(struct v3d_dev *v3d, int core)
{
	if (v3d->ver > 32)
		return;

	V3D_CORE_WRITE(core, V3D_CTL_L2CACTL,
		       V3D_L2CACTL_L2CCLR |
		       V3D_L2CACTL_L2CENA);
}

/* Invalidates texture L2 cachelines */
static void
v3d_flush_l2t(struct v3d_dev *v3d, int core)
{
	/* While there is a busy bit (V3D_L2TCACTL_L2TFLS), we don't
	 * need to wait for completion before dispatching the job --
	 * L2T accesses will be stalled until the flush has completed.
	 * However, we do need to make sure we don't try to trigger a
	 * new flush while the L2_CLEAN queue is trying to
	 * synchronously clean after a job.
	 */
	mutex_lock(&v3d->cache_clean_lock);
	V3D_CORE_WRITE(core, V3D_CTL_L2TCACTL,
		       V3D_L2TCACTL_L2TFLS |
		       V3D_SET_FIELD(V3D_L2TCACTL_FLM_FLUSH, V3D_L2TCACTL_FLM));
	mutex_unlock(&v3d->cache_clean_lock);
}

/* Cleans texture L1 and L2 cachelines (writing back dirty data).
 *
 * For cleaning, which happens from the CACHE_CLEAN queue after CSD has
 * executed, we need to make sure that the clean is done before
 * signaling job completion.  So, we synchronously wait before
 * returning, and we make sure that L2 invalidates don't happen in the
 * meantime to confuse our are-we-done checks.
 */
void
v3d_clean_caches(struct v3d_dev *v3d)
{
	struct drm_device *dev = &v3d->drm;
	int core = 0;

	trace_v3d_cache_clean_begin(dev);

	V3D_CORE_WRITE(core, V3D_CTL_L2TCACTL, V3D_L2TCACTL_TMUWCF);
	if (wait_for(!(V3D_CORE_READ(core, V3D_CTL_L2TCACTL) &
		       V3D_L2TCACTL_TMUWCF), 100)) {
		DRM_ERROR("Timeout waiting for TMU write combiner flush\n");
	}

	mutex_lock(&v3d->cache_clean_lock);
	V3D_CORE_WRITE(core, V3D_CTL_L2TCACTL,
		       V3D_L2TCACTL_L2TFLS |
		       V3D_SET_FIELD(V3D_L2TCACTL_FLM_CLEAN, V3D_L2TCACTL_FLM));

	if (wait_for(!(V3D_CORE_READ(core, V3D_CTL_L2TCACTL) &
		       V3D_L2TCACTL_L2TFLS), 100)) {
		DRM_ERROR("Timeout waiting for L2T clean\n");
	}

	mutex_unlock(&v3d->cache_clean_lock);

	trace_v3d_cache_clean_end(dev);
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

void
v3d_invalidate_caches(struct v3d_dev *v3d)
{
	/* Invalidate the caches from the outside in.  That way if
	 * another CL's concurrent use of nearby memory were to pull
	 * an invalidated cacheline back in, we wouldn't leave stale
	 * data in the inner cache.
	 */
	v3d_flush_l3(v3d);
	v3d_invalidate_l2c(v3d, 0);
	v3d_flush_l2t(v3d, 0);
	v3d_invalidate_slices(v3d, 0);
}

/* Takes the reservation lock on all the BOs being referenced, so that
 * at queue submit time we can update the reservations.
 *
 * We don't lock the RCL the tile alloc/state BOs, or overflow memory
 * (all of which are on exec->unref_list).  They're entirely private
 * to v3d, so we don't attach dma-buf fences to them.
 */
static int
v3d_lock_bo_reservations(struct v3d_job *job,
			 struct ww_acquire_ctx *acquire_ctx)
{
	int i, ret;

	ret = drm_gem_lock_reservations(job->bo, job->bo_count, acquire_ctx);
	if (ret)
		return ret;

	for (i = 0; i < job->bo_count; i++) {
		ret = drm_gem_fence_array_add_implicit(&job->deps,
						       job->bo[i], true);
		if (ret) {
			drm_gem_unlock_reservations(job->bo, job->bo_count,
						    acquire_ctx);
			return ret;
		}
	}

	return 0;
}

/**
 * v3d_lookup_bos() - Sets up job->bo[] with the GEM objects
 * referenced by the job.
 * @dev: DRM device
 * @file_priv: DRM file for this fd
 * @job: V3D job being set up
 * @bo_handles: GEM handles
 * @bo_count: Number of GEM handles passed in
 *
 * The command validator needs to reference BOs by their index within
 * the submitted job's BO list.  This does the validation of the job's
 * BO list and reference counting for the lifetime of the job.
 *
 * Note that this function doesn't need to unreference the BOs on
 * failure, because that will happen at v3d_exec_cleanup() time.
 */
static int
v3d_lookup_bos(struct drm_device *dev,
	       struct drm_file *file_priv,
	       struct v3d_job *job,
	       u64 bo_handles,
	       u32 bo_count)
{
	u32 *handles;
	int ret = 0;
	int i;

	job->bo_count = bo_count;

	if (!job->bo_count) {
		/* See comment on bo_index for why we have to check
		 * this.
		 */
		DRM_DEBUG("Rendering requires BOs\n");
		return -EINVAL;
	}

	job->bo = kvmalloc_array(job->bo_count,
				 sizeof(struct drm_gem_cma_object *),
				 GFP_KERNEL | __GFP_ZERO);
	if (!job->bo) {
		DRM_DEBUG("Failed to allocate validated BO pointers\n");
		return -ENOMEM;
	}

	handles = kvmalloc_array(job->bo_count, sizeof(u32), GFP_KERNEL);
	if (!handles) {
		ret = -ENOMEM;
		DRM_DEBUG("Failed to allocate incoming GEM handles\n");
		goto fail;
	}

	if (copy_from_user(handles,
			   (void __user *)(uintptr_t)bo_handles,
			   job->bo_count * sizeof(u32))) {
		ret = -EFAULT;
		DRM_DEBUG("Failed to copy in GEM handles\n");
		goto fail;
	}

	spin_lock(&file_priv->table_lock);
	for (i = 0; i < job->bo_count; i++) {
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
		job->bo[i] = bo;
	}
	spin_unlock(&file_priv->table_lock);

fail:
	kvfree(handles);
	return ret;
}

static void
v3d_job_free(struct kref *ref)
{
	struct v3d_job *job = container_of(ref, struct v3d_job, refcount);
	unsigned long index;
	struct dma_fence *fence;
	int i;

	for (i = 0; i < job->bo_count; i++) {
		if (job->bo[i])
			drm_gem_object_put(job->bo[i]);
	}
	kvfree(job->bo);

	xa_for_each(&job->deps, index, fence) {
		dma_fence_put(fence);
	}
	xa_destroy(&job->deps);

	dma_fence_put(job->irq_fence);
	dma_fence_put(job->done_fence);

	pm_runtime_mark_last_busy(job->v3d->drm.dev);
	pm_runtime_put_autosuspend(job->v3d->drm.dev);

	if (job->perfmon)
		v3d_perfmon_put(job->perfmon);

	kfree(job);
}

static void
v3d_render_job_free(struct kref *ref)
{
	struct v3d_render_job *job = container_of(ref, struct v3d_render_job,
						  base.refcount);
	struct v3d_bo *bo, *save;

	list_for_each_entry_safe(bo, save, &job->unref_list, unref_head) {
		drm_gem_object_put(&bo->base.base);
	}

	v3d_job_free(ref);
}

void v3d_job_put(struct v3d_job *job)
{
	kref_put(&job->refcount, job->free);
}

int
v3d_wait_bo_ioctl(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	int ret;
	struct drm_v3d_wait_bo *args = data;
	ktime_t start = ktime_get();
	u64 delta_ns;
	unsigned long timeout_jiffies =
		nsecs_to_jiffies_timeout(args->timeout_ns);

	if (args->pad != 0)
		return -EINVAL;

	ret = drm_gem_dma_resv_wait(file_priv, args->handle,
					      true, timeout_jiffies);

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

	return ret;
}

static int
v3d_job_init(struct v3d_dev *v3d, struct drm_file *file_priv,
	     struct v3d_job *job, void (*free)(struct kref *ref),
	     u32 in_sync)
{
	struct dma_fence *in_fence = NULL;
	int ret;

	job->v3d = v3d;
	job->free = free;

	ret = pm_runtime_get_sync(v3d->drm.dev);
	if (ret < 0)
		return ret;

	xa_init_flags(&job->deps, XA_FLAGS_ALLOC);

	ret = drm_syncobj_find_fence(file_priv, in_sync, 0, 0, &in_fence);
	if (ret == -EINVAL)
		goto fail;

	ret = drm_gem_fence_array_add(&job->deps, in_fence);
	if (ret)
		goto fail;

	kref_init(&job->refcount);

	return 0;
fail:
	xa_destroy(&job->deps);
	pm_runtime_put_autosuspend(v3d->drm.dev);
	return ret;
}

static int
v3d_push_job(struct v3d_file_priv *v3d_priv,
	     struct v3d_job *job, enum v3d_queue queue)
{
	int ret;

	ret = drm_sched_job_init(&job->base, &v3d_priv->sched_entity[queue],
				 v3d_priv);
	if (ret)
		return ret;

	job->done_fence = dma_fence_get(&job->base.s_fence->finished);

	/* put by scheduler job completion */
	kref_get(&job->refcount);

	drm_sched_entity_push_job(&job->base, &v3d_priv->sched_entity[queue]);

	return 0;
}

static void
v3d_attach_fences_and_unlock_reservation(struct drm_file *file_priv,
					 struct v3d_job *job,
					 struct ww_acquire_ctx *acquire_ctx,
					 u32 out_sync,
					 struct dma_fence *done_fence)
{
	struct drm_syncobj *sync_out;
	int i;

	for (i = 0; i < job->bo_count; i++) {
		/* XXX: Use shared fences for read-only objects. */
		dma_resv_add_excl_fence(job->bo[i]->resv,
						  job->done_fence);
	}

	drm_gem_unlock_reservations(job->bo, job->bo_count, acquire_ctx);

	/* Update the return sync object for the job */
	sync_out = drm_syncobj_find(file_priv, out_sync);
	if (sync_out) {
		drm_syncobj_replace_fence(sync_out, done_fence);
		drm_syncobj_put(sync_out);
	}
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
	struct v3d_bin_job *bin = NULL;
	struct v3d_render_job *render;
	struct v3d_job *clean_job = NULL;
	struct v3d_job *last_job;
	struct ww_acquire_ctx acquire_ctx;
	int ret = 0;

	trace_v3d_submit_cl_ioctl(&v3d->drm, args->rcl_start, args->rcl_end);

	if (args->pad != 0)
		return -EINVAL;

	if (args->flags != 0 &&
	    args->flags != DRM_V3D_SUBMIT_CL_FLUSH_CACHE) {
		DRM_INFO("invalid flags: %d\n", args->flags);
		return -EINVAL;
	}

	render = kcalloc(1, sizeof(*render), GFP_KERNEL);
	if (!render)
		return -ENOMEM;

	render->start = args->rcl_start;
	render->end = args->rcl_end;
	INIT_LIST_HEAD(&render->unref_list);

	ret = v3d_job_init(v3d, file_priv, &render->base,
			   v3d_render_job_free, args->in_sync_rcl);
	if (ret) {
		kfree(render);
		return ret;
	}

	if (args->bcl_start != args->bcl_end) {
		bin = kcalloc(1, sizeof(*bin), GFP_KERNEL);
		if (!bin) {
			v3d_job_put(&render->base);
			return -ENOMEM;
		}

		ret = v3d_job_init(v3d, file_priv, &bin->base,
				   v3d_job_free, args->in_sync_bcl);
		if (ret) {
			v3d_job_put(&render->base);
			kfree(bin);
			return ret;
		}

		bin->start = args->bcl_start;
		bin->end = args->bcl_end;
		bin->qma = args->qma;
		bin->qms = args->qms;
		bin->qts = args->qts;
		bin->render = render;
	}

	if (args->flags & DRM_V3D_SUBMIT_CL_FLUSH_CACHE) {
		clean_job = kcalloc(1, sizeof(*clean_job), GFP_KERNEL);
		if (!clean_job) {
			ret = -ENOMEM;
			goto fail;
		}

		ret = v3d_job_init(v3d, file_priv, clean_job, v3d_job_free, 0);
		if (ret) {
			kfree(clean_job);
			clean_job = NULL;
			goto fail;
		}

		last_job = clean_job;
	} else {
		last_job = &render->base;
	}

	ret = v3d_lookup_bos(dev, file_priv, last_job,
			     args->bo_handles, args->bo_handle_count);
	if (ret)
		goto fail;

	ret = v3d_lock_bo_reservations(last_job, &acquire_ctx);
	if (ret)
		goto fail;

	if (args->perfmon_id) {
		render->base.perfmon = v3d_perfmon_find(v3d_priv,
							args->perfmon_id);

		if (!render->base.perfmon) {
			ret = -ENOENT;
			goto fail_perfmon;
		}
	}

	mutex_lock(&v3d->sched_lock);
	if (bin) {
		bin->base.perfmon = render->base.perfmon;
		v3d_perfmon_get(bin->base.perfmon);
		ret = v3d_push_job(v3d_priv, &bin->base, V3D_BIN);
		if (ret)
			goto fail_unreserve;

		ret = drm_gem_fence_array_add(&render->base.deps,
					      dma_fence_get(bin->base.done_fence));
		if (ret)
			goto fail_unreserve;
	}

	ret = v3d_push_job(v3d_priv, &render->base, V3D_RENDER);
	if (ret)
		goto fail_unreserve;

	if (clean_job) {
		struct dma_fence *render_fence =
			dma_fence_get(render->base.done_fence);
		ret = drm_gem_fence_array_add(&clean_job->deps, render_fence);
		if (ret)
			goto fail_unreserve;
		clean_job->perfmon = render->base.perfmon;
		v3d_perfmon_get(clean_job->perfmon);
		ret = v3d_push_job(v3d_priv, clean_job, V3D_CACHE_CLEAN);
		if (ret)
			goto fail_unreserve;
	}

	mutex_unlock(&v3d->sched_lock);

	v3d_attach_fences_and_unlock_reservation(file_priv,
						 last_job,
						 &acquire_ctx,
						 args->out_sync,
						 last_job->done_fence);

	if (bin)
		v3d_job_put(&bin->base);
	v3d_job_put(&render->base);
	if (clean_job)
		v3d_job_put(clean_job);

	return 0;

fail_unreserve:
	mutex_unlock(&v3d->sched_lock);
fail_perfmon:
	drm_gem_unlock_reservations(last_job->bo,
				    last_job->bo_count, &acquire_ctx);
fail:
	if (bin)
		v3d_job_put(&bin->base);
	v3d_job_put(&render->base);
	if (clean_job)
		v3d_job_put(clean_job);

	return ret;
}

/**
 * v3d_submit_tfu_ioctl() - Submits a TFU (texture formatting) job to the V3D.
 * @dev: DRM device
 * @data: ioctl argument
 * @file_priv: DRM file for this fd
 *
 * Userspace provides the register setup for the TFU, which we don't
 * need to validate since the TFU is behind the MMU.
 */
int
v3d_submit_tfu_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct v3d_dev *v3d = to_v3d_dev(dev);
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct drm_v3d_submit_tfu *args = data;
	struct v3d_tfu_job *job;
	struct ww_acquire_ctx acquire_ctx;
	int ret = 0;

	trace_v3d_submit_tfu_ioctl(&v3d->drm, args->iia);

	job = kcalloc(1, sizeof(*job), GFP_KERNEL);
	if (!job)
		return -ENOMEM;

	ret = v3d_job_init(v3d, file_priv, &job->base,
			   v3d_job_free, args->in_sync);
	if (ret) {
		kfree(job);
		return ret;
	}

	job->base.bo = kcalloc(ARRAY_SIZE(args->bo_handles),
			       sizeof(*job->base.bo), GFP_KERNEL);
	if (!job->base.bo) {
		v3d_job_put(&job->base);
		return -ENOMEM;
	}

	job->args = *args;

	spin_lock(&file_priv->table_lock);
	for (job->base.bo_count = 0;
	     job->base.bo_count < ARRAY_SIZE(args->bo_handles);
	     job->base.bo_count++) {
		struct drm_gem_object *bo;

		if (!args->bo_handles[job->base.bo_count])
			break;

		bo = idr_find(&file_priv->object_idr,
			      args->bo_handles[job->base.bo_count]);
		if (!bo) {
			DRM_DEBUG("Failed to look up GEM BO %d: %d\n",
				  job->base.bo_count,
				  args->bo_handles[job->base.bo_count]);
			ret = -ENOENT;
			spin_unlock(&file_priv->table_lock);
			goto fail;
		}
		drm_gem_object_get(bo);
		job->base.bo[job->base.bo_count] = bo;
	}
	spin_unlock(&file_priv->table_lock);

	ret = v3d_lock_bo_reservations(&job->base, &acquire_ctx);
	if (ret)
		goto fail;

	mutex_lock(&v3d->sched_lock);
	ret = v3d_push_job(v3d_priv, &job->base, V3D_TFU);
	if (ret)
		goto fail_unreserve;
	mutex_unlock(&v3d->sched_lock);

	v3d_attach_fences_and_unlock_reservation(file_priv,
						 &job->base, &acquire_ctx,
						 args->out_sync,
						 job->base.done_fence);

	v3d_job_put(&job->base);

	return 0;

fail_unreserve:
	mutex_unlock(&v3d->sched_lock);
	drm_gem_unlock_reservations(job->base.bo, job->base.bo_count,
				    &acquire_ctx);
fail:
	v3d_job_put(&job->base);

	return ret;
}

/**
 * v3d_submit_csd_ioctl() - Submits a CSD (texture formatting) job to the V3D.
 * @dev: DRM device
 * @data: ioctl argument
 * @file_priv: DRM file for this fd
 *
 * Userspace provides the register setup for the CSD, which we don't
 * need to validate since the CSD is behind the MMU.
 */
int
v3d_submit_csd_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct v3d_dev *v3d = to_v3d_dev(dev);
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct drm_v3d_submit_csd *args = data;
	struct v3d_csd_job *job;
	struct v3d_job *clean_job;
	struct ww_acquire_ctx acquire_ctx;
	int ret;

	trace_v3d_submit_csd_ioctl(&v3d->drm, args->cfg[5], args->cfg[6]);

	if (!v3d_has_csd(v3d)) {
		DRM_DEBUG("Attempting CSD submit on non-CSD hardware\n");
		return -EINVAL;
	}

	job = kcalloc(1, sizeof(*job), GFP_KERNEL);
	if (!job)
		return -ENOMEM;

	ret = v3d_job_init(v3d, file_priv, &job->base,
			   v3d_job_free, args->in_sync);
	if (ret) {
		kfree(job);
		return ret;
	}

	clean_job = kcalloc(1, sizeof(*clean_job), GFP_KERNEL);
	if (!clean_job) {
		v3d_job_put(&job->base);
		kfree(job);
		return -ENOMEM;
	}

	ret = v3d_job_init(v3d, file_priv, clean_job, v3d_job_free, 0);
	if (ret) {
		v3d_job_put(&job->base);
		kfree(clean_job);
		return ret;
	}

	job->args = *args;

	ret = v3d_lookup_bos(dev, file_priv, clean_job,
			     args->bo_handles, args->bo_handle_count);
	if (ret)
		goto fail;

	ret = v3d_lock_bo_reservations(clean_job, &acquire_ctx);
	if (ret)
		goto fail;

	if (args->perfmon_id) {
		job->base.perfmon = v3d_perfmon_find(v3d_priv,
						     args->perfmon_id);
		if (!job->base.perfmon) {
			ret = -ENOENT;
			goto fail_perfmon;
		}
	}

	mutex_lock(&v3d->sched_lock);
	ret = v3d_push_job(v3d_priv, &job->base, V3D_CSD);
	if (ret)
		goto fail_unreserve;

	ret = drm_gem_fence_array_add(&clean_job->deps,
				      dma_fence_get(job->base.done_fence));
	if (ret)
		goto fail_unreserve;

	ret = v3d_push_job(v3d_priv, clean_job, V3D_CACHE_CLEAN);
	if (ret)
		goto fail_unreserve;
	mutex_unlock(&v3d->sched_lock);

	v3d_attach_fences_and_unlock_reservation(file_priv,
						 clean_job,
						 &acquire_ctx,
						 args->out_sync,
						 clean_job->done_fence);

	v3d_job_put(&job->base);
	v3d_job_put(clean_job);

	return 0;

fail_unreserve:
	mutex_unlock(&v3d->sched_lock);
fail_perfmon:
	drm_gem_unlock_reservations(clean_job->bo, clean_job->bo_count,
				    &acquire_ctx);
fail:
	v3d_job_put(&job->base);
	v3d_job_put(clean_job);

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
	mutex_init(&v3d->cache_clean_lock);

	/* Note: We don't allocate address 0.  Various bits of HW
	 * treat 0 as special, such as the occlusion query counters
	 * where 0 means "disabled".
	 */
	drm_mm_init(&v3d->mm, 1, pt_size / sizeof(u32) - 1);

	v3d->pt = dma_alloc_wc(v3d->drm.dev, pt_size,
			       &v3d->pt_paddr,
			       GFP_KERNEL | __GFP_NOWARN | __GFP_ZERO);
	if (!v3d->pt) {
		drm_mm_takedown(&v3d->mm);
		dev_err(v3d->drm.dev,
			"Failed to allocate page tables. "
			"Please ensure you have CMA enabled.\n");
		return -ENOMEM;
	}

	v3d_init_hw_state(v3d);
	v3d_mmu_set_page_table(v3d);

	ret = v3d_sched_init(v3d);
	if (ret) {
		drm_mm_takedown(&v3d->mm);
		dma_free_coherent(v3d->drm.dev, 4096 * 1024, (void *)v3d->pt,
				  v3d->pt_paddr);
	}

	return 0;
}

void
v3d_gem_destroy(struct drm_device *dev)
{
	struct v3d_dev *v3d = to_v3d_dev(dev);

	v3d_sched_fini(v3d);

	/* Waiting for jobs to finish would need to be done before
	 * unregistering V3D.
	 */
	WARN_ON(v3d->bin_job);
	WARN_ON(v3d->render_job);

	drm_mm_takedown(&v3d->mm);

	dma_free_coherent(v3d->drm.dev, 4096 * 1024, (void *)v3d->pt,
			  v3d->pt_paddr);
}
