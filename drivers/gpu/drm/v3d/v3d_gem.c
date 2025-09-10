// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2014-2018 Broadcom */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>

#include <drm/drm_managed.h>

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
	if (v3d->ver < V3D_GEN_41)
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
	V3D_CORE_WRITE(core, V3D_GMP_CFG(v3d->ver), V3D_GMP_CFG_STOP_REQ);

	if (wait_for((V3D_CORE_READ(core, V3D_GMP_STATUS(v3d->ver)) &
		      (V3D_GMP_STATUS_RD_COUNT_MASK |
		       V3D_GMP_STATUS_WR_COUNT_MASK |
		       V3D_GMP_STATUS_CFG_BUSY)) == 0, 100)) {
		DRM_ERROR("Failed to wait for safe GMP shutdown\n");
	}
}

static void
v3d_idle_gca(struct v3d_dev *v3d)
{
	if (v3d->ver >= V3D_GEN_41)
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
v3d_reset_sms(struct v3d_dev *v3d)
{
	if (v3d->ver < V3D_GEN_71)
		return;

	V3D_SMS_WRITE(V3D_SMS_REE_CS, V3D_SET_FIELD(0x4, V3D_SMS_STATE));

	if (wait_for(!(V3D_GET_FIELD(V3D_SMS_READ(V3D_SMS_REE_CS),
				     V3D_SMS_STATE) == V3D_SMS_ISOLATING_FOR_RESET) &&
		     !(V3D_GET_FIELD(V3D_SMS_READ(V3D_SMS_REE_CS),
				     V3D_SMS_STATE) == V3D_SMS_RESETTING), 100)) {
		DRM_ERROR("Failed to wait for SMS reset\n");
	}
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

	v3d_irq_disable(v3d);

	v3d_idle_gca(v3d);
	v3d_reset_sms(v3d);
	v3d_reset_v3d(v3d);

	v3d_mmu_set_page_table(v3d);
	v3d_irq_reset(v3d);

	v3d_perfmon_stop(v3d, v3d->active_perfmon, false);

	trace_v3d_reset_end(dev);
}

static void
v3d_flush_l3(struct v3d_dev *v3d)
{
	if (v3d->ver < V3D_GEN_41) {
		u32 gca_ctrl = V3D_GCA_READ(V3D_GCA_CACHE_CTRL);

		V3D_GCA_WRITE(V3D_GCA_CACHE_CTRL,
			      gca_ctrl | V3D_GCA_CACHE_CTRL_FLUSH);

		if (v3d->ver < V3D_GEN_33) {
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
	if (v3d->ver >= V3D_GEN_33)
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

int
v3d_gem_init(struct drm_device *dev)
{
	struct v3d_dev *v3d = to_v3d_dev(dev);
	u32 pt_size = 4096 * 1024;
	int ret, i;

	for (i = 0; i < V3D_MAX_QUEUES; i++) {
		struct v3d_queue_state *queue = &v3d->queue[i];

		queue->fence_context = dma_fence_context_alloc(1);
		memset(&queue->stats, 0, sizeof(queue->stats));
		seqcount_init(&queue->stats.lock);
	}

	spin_lock_init(&v3d->mm_lock);
	spin_lock_init(&v3d->job_lock);
	ret = drmm_mutex_init(dev, &v3d->bo_lock);
	if (ret)
		return ret;
	ret = drmm_mutex_init(dev, &v3d->reset_lock);
	if (ret)
		return ret;
	ret = drmm_mutex_init(dev, &v3d->sched_lock);
	if (ret)
		return ret;
	ret = drmm_mutex_init(dev, &v3d->cache_clean_lock);
	if (ret)
		return ret;

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
			"Failed to allocate page tables. Please ensure you have DMA enabled.\n");
		return -ENOMEM;
	}

	v3d_init_hw_state(v3d);
	v3d_mmu_set_page_table(v3d);

	v3d_gemfs_init(v3d);

	ret = v3d_sched_init(v3d);
	if (ret) {
		drm_mm_takedown(&v3d->mm);
		dma_free_coherent(v3d->drm.dev, pt_size, (void *)v3d->pt,
				  v3d->pt_paddr);
		return ret;
	}

	return 0;
}

void
v3d_gem_destroy(struct drm_device *dev)
{
	struct v3d_dev *v3d = to_v3d_dev(dev);

	v3d_sched_fini(v3d);
	v3d_gemfs_fini(v3d);

	/* Waiting for jobs to finish would need to be done before
	 * unregistering V3D.
	 */
	WARN_ON(v3d->bin_job);
	WARN_ON(v3d->render_job);
	WARN_ON(v3d->tfu_job);
	WARN_ON(v3d->csd_job);

	drm_mm_takedown(&v3d->mm);

	dma_free_coherent(v3d->drm.dev, 4096 * 1024, (void *)v3d->pt,
			  v3d->pt_paddr);
}
