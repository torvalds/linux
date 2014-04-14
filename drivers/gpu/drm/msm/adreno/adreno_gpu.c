/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
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

#include "adreno_gpu.h"
#include "msm_gem.h"
#include "msm_mmu.h"

struct adreno_info {
	struct adreno_rev rev;
	uint32_t revn;
	const char *name;
	const char *pm4fw, *pfpfw;
	uint32_t gmem;
};

#define ANY_ID 0xff

static const struct adreno_info gpulist[] = {
	{
		.rev   = ADRENO_REV(3, 0, 5, ANY_ID),
		.revn  = 305,
		.name  = "A305",
		.pm4fw = "a300_pm4.fw",
		.pfpfw = "a300_pfp.fw",
		.gmem  = SZ_256K,
	}, {
		.rev   = ADRENO_REV(3, 2, ANY_ID, ANY_ID),
		.revn  = 320,
		.name  = "A320",
		.pm4fw = "a300_pm4.fw",
		.pfpfw = "a300_pfp.fw",
		.gmem  = SZ_512K,
	}, {
		.rev   = ADRENO_REV(3, 3, 0, ANY_ID),
		.revn  = 330,
		.name  = "A330",
		.pm4fw = "a330_pm4.fw",
		.pfpfw = "a330_pfp.fw",
		.gmem  = SZ_1M,
	},
};

MODULE_FIRMWARE("a300_pm4.fw");
MODULE_FIRMWARE("a300_pfp.fw");
MODULE_FIRMWARE("a330_pm4.fw");
MODULE_FIRMWARE("a330_pfp.fw");

#define RB_SIZE    SZ_32K
#define RB_BLKSIZE 16

int adreno_get_param(struct msm_gpu *gpu, uint32_t param, uint64_t *value)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);

	switch (param) {
	case MSM_PARAM_GPU_ID:
		*value = adreno_gpu->info->revn;
		return 0;
	case MSM_PARAM_GMEM_SIZE:
		*value = adreno_gpu->gmem;
		return 0;
	case MSM_PARAM_CHIP_ID:
		*value = adreno_gpu->rev.patchid |
				(adreno_gpu->rev.minor << 8) |
				(adreno_gpu->rev.major << 16) |
				(adreno_gpu->rev.core << 24);
		return 0;
	default:
		DBG("%s: invalid param: %u", gpu->name, param);
		return -EINVAL;
	}
}

#define rbmemptr(adreno_gpu, member)  \
	((adreno_gpu)->memptrs_iova + offsetof(struct adreno_rbmemptrs, member))

int adreno_hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);

	DBG("%s", gpu->name);

	/* Setup REG_CP_RB_CNTL: */
	gpu_write(gpu, REG_AXXX_CP_RB_CNTL,
			/* size is log2(quad-words): */
			AXXX_CP_RB_CNTL_BUFSZ(ilog2(gpu->rb->size / 8)) |
			AXXX_CP_RB_CNTL_BLKSZ(ilog2(RB_BLKSIZE / 8)));

	/* Setup ringbuffer address: */
	gpu_write(gpu, REG_AXXX_CP_RB_BASE, gpu->rb_iova);
	gpu_write(gpu, REG_AXXX_CP_RB_RPTR_ADDR, rbmemptr(adreno_gpu, rptr));

	/* Setup scratch/timestamp: */
	gpu_write(gpu, REG_AXXX_SCRATCH_ADDR, rbmemptr(adreno_gpu, fence));

	gpu_write(gpu, REG_AXXX_SCRATCH_UMSK, 0x1);

	return 0;
}

static uint32_t get_wptr(struct msm_ringbuffer *ring)
{
	return ring->cur - ring->start;
}

uint32_t adreno_last_fence(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	return adreno_gpu->memptrs->fence;
}

void adreno_recover(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct drm_device *dev = gpu->dev;
	int ret;

	gpu->funcs->pm_suspend(gpu);

	/* reset ringbuffer: */
	gpu->rb->cur = gpu->rb->start;

	/* reset completed fence seqno, just discard anything pending: */
	adreno_gpu->memptrs->fence = gpu->submitted_fence;
	adreno_gpu->memptrs->rptr  = 0;
	adreno_gpu->memptrs->wptr  = 0;

	gpu->funcs->pm_resume(gpu);
	ret = gpu->funcs->hw_init(gpu);
	if (ret) {
		dev_err(dev->dev, "gpu hw init failed: %d\n", ret);
		/* hmm, oh well? */
	}
}

int adreno_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit,
		struct msm_file_private *ctx)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct msm_drm_private *priv = gpu->dev->dev_private;
	struct msm_ringbuffer *ring = gpu->rb;
	unsigned i, ibs = 0;

	for (i = 0; i < submit->nr_cmds; i++) {
		switch (submit->cmd[i].type) {
		case MSM_SUBMIT_CMD_IB_TARGET_BUF:
			/* ignore IB-targets */
			break;
		case MSM_SUBMIT_CMD_CTX_RESTORE_BUF:
			/* ignore if there has not been a ctx switch: */
			if (priv->lastctx == ctx)
				break;
		case MSM_SUBMIT_CMD_BUF:
			OUT_PKT3(ring, CP_INDIRECT_BUFFER_PFD, 2);
			OUT_RING(ring, submit->cmd[i].iova);
			OUT_RING(ring, submit->cmd[i].size);
			ibs++;
			break;
		}
	}

	/* on a320, at least, we seem to need to pad things out to an
	 * even number of qwords to avoid issue w/ CP hanging on wrap-
	 * around:
	 */
	if (ibs % 2)
		OUT_PKT2(ring);

	OUT_PKT0(ring, REG_AXXX_CP_SCRATCH_REG2, 1);
	OUT_RING(ring, submit->fence);

	if (adreno_is_a3xx(adreno_gpu)) {
		/* Flush HLSQ lazy updates to make sure there is nothing
		 * pending for indirect loads after the timestamp has
		 * passed:
		 */
		OUT_PKT3(ring, CP_EVENT_WRITE, 1);
		OUT_RING(ring, HLSQ_FLUSH);

		OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
		OUT_RING(ring, 0x00000000);
	}

	OUT_PKT3(ring, CP_EVENT_WRITE, 3);
	OUT_RING(ring, CACHE_FLUSH_TS);
	OUT_RING(ring, rbmemptr(adreno_gpu, fence));
	OUT_RING(ring, submit->fence);

	/* we could maybe be clever and only CP_COND_EXEC the interrupt: */
	OUT_PKT3(ring, CP_INTERRUPT, 1);
	OUT_RING(ring, 0x80000000);

#if 0
	if (adreno_is_a3xx(adreno_gpu)) {
		/* Dummy set-constant to trigger context rollover */
		OUT_PKT3(ring, CP_SET_CONSTANT, 2);
		OUT_RING(ring, CP_REG(REG_A3XX_HLSQ_CL_KERNEL_GROUP_X_REG));
		OUT_RING(ring, 0x00000000);
	}
#endif

	gpu->funcs->flush(gpu);

	return 0;
}

void adreno_flush(struct msm_gpu *gpu)
{
	uint32_t wptr = get_wptr(gpu->rb);

	/* ensure writes to ringbuffer have hit system memory: */
	mb();

	gpu_write(gpu, REG_AXXX_CP_RB_WPTR, wptr);
}

void adreno_idle(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	uint32_t wptr = get_wptr(gpu->rb);

	/* wait for CP to drain ringbuffer: */
	if (spin_until(adreno_gpu->memptrs->rptr == wptr))
		DRM_ERROR("%s: timeout waiting to drain ringbuffer!\n", gpu->name);

	/* TODO maybe we need to reset GPU here to recover from hang? */
}

#ifdef CONFIG_DEBUG_FS
void adreno_show(struct msm_gpu *gpu, struct seq_file *m)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);

	seq_printf(m, "revision: %d (%d.%d.%d.%d)\n",
			adreno_gpu->info->revn, adreno_gpu->rev.core,
			adreno_gpu->rev.major, adreno_gpu->rev.minor,
			adreno_gpu->rev.patchid);

	seq_printf(m, "fence:    %d/%d\n", adreno_gpu->memptrs->fence,
			gpu->submitted_fence);
	seq_printf(m, "rptr:     %d\n", adreno_gpu->memptrs->rptr);
	seq_printf(m, "wptr:     %d\n", adreno_gpu->memptrs->wptr);
	seq_printf(m, "rb wptr:  %d\n", get_wptr(gpu->rb));
}
#endif

/* would be nice to not have to duplicate the _show() stuff with printk(): */
void adreno_dump(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);

	printk("revision: %d (%d.%d.%d.%d)\n",
			adreno_gpu->info->revn, adreno_gpu->rev.core,
			adreno_gpu->rev.major, adreno_gpu->rev.minor,
			adreno_gpu->rev.patchid);

	printk("fence:    %d/%d\n", adreno_gpu->memptrs->fence,
			gpu->submitted_fence);
	printk("rptr:     %d\n", adreno_gpu->memptrs->rptr);
	printk("wptr:     %d\n", adreno_gpu->memptrs->wptr);
	printk("rb wptr:  %d\n", get_wptr(gpu->rb));

}

static uint32_t ring_freewords(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	uint32_t size = gpu->rb->size / 4;
	uint32_t wptr = get_wptr(gpu->rb);
	uint32_t rptr = adreno_gpu->memptrs->rptr;
	return (rptr + (size - 1) - wptr) % size;
}

void adreno_wait_ring(struct msm_gpu *gpu, uint32_t ndwords)
{
	if (spin_until(ring_freewords(gpu) >= ndwords))
		DRM_ERROR("%s: timeout waiting for ringbuffer space\n", gpu->name);
}

static const char *iommu_ports[] = {
		"gfx3d_user", "gfx3d_priv",
		"gfx3d1_user", "gfx3d1_priv",
};

static inline bool _rev_match(uint8_t entry, uint8_t id)
{
	return (entry == ANY_ID) || (entry == id);
}

int adreno_gpu_init(struct drm_device *drm, struct platform_device *pdev,
		struct adreno_gpu *gpu, const struct adreno_gpu_funcs *funcs,
		struct adreno_rev rev)
{
	struct msm_mmu *mmu;
	int i, ret;

	/* identify gpu: */
	for (i = 0; i < ARRAY_SIZE(gpulist); i++) {
		const struct adreno_info *info = &gpulist[i];
		if (_rev_match(info->rev.core, rev.core) &&
				_rev_match(info->rev.major, rev.major) &&
				_rev_match(info->rev.minor, rev.minor) &&
				_rev_match(info->rev.patchid, rev.patchid)) {
			gpu->info = info;
			gpu->revn = info->revn;
			break;
		}
	}

	if (i == ARRAY_SIZE(gpulist)) {
		dev_err(drm->dev, "Unknown GPU revision: %u.%u.%u.%u\n",
				rev.core, rev.major, rev.minor, rev.patchid);
		return -ENXIO;
	}

	DBG("Found GPU: %s (%u.%u.%u.%u)", gpu->info->name,
			rev.core, rev.major, rev.minor, rev.patchid);

	gpu->funcs = funcs;
	gpu->gmem = gpu->info->gmem;
	gpu->rev = rev;

	ret = request_firmware(&gpu->pm4, gpu->info->pm4fw, drm->dev);
	if (ret) {
		dev_err(drm->dev, "failed to load %s PM4 firmware: %d\n",
				gpu->info->pm4fw, ret);
		return ret;
	}

	ret = request_firmware(&gpu->pfp, gpu->info->pfpfw, drm->dev);
	if (ret) {
		dev_err(drm->dev, "failed to load %s PFP firmware: %d\n",
				gpu->info->pfpfw, ret);
		return ret;
	}

	ret = msm_gpu_init(drm, pdev, &gpu->base, &funcs->base,
			gpu->info->name, "kgsl_3d0_reg_memory", "kgsl_3d0_irq",
			RB_SIZE);
	if (ret)
		return ret;

	mmu = gpu->base.mmu;
	if (mmu) {
		ret = mmu->funcs->attach(mmu, iommu_ports,
				ARRAY_SIZE(iommu_ports));
		if (ret)
			return ret;
	}

	gpu->memptrs_bo = msm_gem_new(drm, sizeof(*gpu->memptrs),
			MSM_BO_UNCACHED);
	if (IS_ERR(gpu->memptrs_bo)) {
		ret = PTR_ERR(gpu->memptrs_bo);
		gpu->memptrs_bo = NULL;
		dev_err(drm->dev, "could not allocate memptrs: %d\n", ret);
		return ret;
	}

	gpu->memptrs = msm_gem_vaddr_locked(gpu->memptrs_bo);
	if (!gpu->memptrs) {
		dev_err(drm->dev, "could not vmap memptrs\n");
		return -ENOMEM;
	}

	ret = msm_gem_get_iova_locked(gpu->memptrs_bo, gpu->base.id,
			&gpu->memptrs_iova);
	if (ret) {
		dev_err(drm->dev, "could not map memptrs: %d\n", ret);
		return ret;
	}

	return 0;
}

void adreno_gpu_cleanup(struct adreno_gpu *gpu)
{
	if (gpu->memptrs_bo) {
		if (gpu->memptrs_iova)
			msm_gem_put_iova(gpu->memptrs_bo, gpu->base.id);
		drm_gem_object_unreference(gpu->memptrs_bo);
	}
	if (gpu->pm4)
		release_firmware(gpu->pm4);
	if (gpu->pfp)
		release_firmware(gpu->pfp);
	msm_gpu_cleanup(&gpu->base);
}
