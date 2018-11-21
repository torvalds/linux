/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
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

#include <linux/pm_opp.h>
#include "adreno_gpu.h"
#include "msm_gem.h"
#include "msm_mmu.h"

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
	case MSM_PARAM_GMEM_BASE:
		*value = 0x100000;
		return 0;
	case MSM_PARAM_CHIP_ID:
		*value = adreno_gpu->rev.patchid |
				(adreno_gpu->rev.minor << 8) |
				(adreno_gpu->rev.major << 16) |
				(adreno_gpu->rev.core << 24);
		return 0;
	case MSM_PARAM_MAX_FREQ:
		*value = adreno_gpu->base.fast_rate;
		return 0;
	case MSM_PARAM_TIMESTAMP:
		if (adreno_gpu->funcs->get_timestamp) {
			int ret;

			pm_runtime_get_sync(&gpu->pdev->dev);
			ret = adreno_gpu->funcs->get_timestamp(gpu, value);
			pm_runtime_put_autosuspend(&gpu->pdev->dev);

			return ret;
		}
		return -EINVAL;
	case MSM_PARAM_NR_RINGS:
		*value = gpu->nr_rings;
		return 0;
	default:
		DBG("%s: invalid param: %u", gpu->name, param);
		return -EINVAL;
	}
}

const struct firmware *
adreno_request_fw(struct adreno_gpu *adreno_gpu, const char *fwname)
{
	struct drm_device *drm = adreno_gpu->base.dev;
	const struct firmware *fw = NULL;
	char newname[strlen("qcom/") + strlen(fwname) + 1];
	int ret;

	sprintf(newname, "qcom/%s", fwname);

	/*
	 * Try first to load from qcom/$fwfile using a direct load (to avoid
	 * a potential timeout waiting for usermode helper)
	 */
	if ((adreno_gpu->fwloc == FW_LOCATION_UNKNOWN) ||
	    (adreno_gpu->fwloc == FW_LOCATION_NEW)) {

		ret = request_firmware_direct(&fw, newname, drm->dev);
		if (!ret) {
			dev_info(drm->dev, "loaded %s from new location\n",
				newname);
			adreno_gpu->fwloc = FW_LOCATION_NEW;
			return fw;
		} else if (adreno_gpu->fwloc != FW_LOCATION_UNKNOWN) {
			dev_err(drm->dev, "failed to load %s: %d\n",
				newname, ret);
			return ERR_PTR(ret);
		}
	}

	/*
	 * Then try the legacy location without qcom/ prefix
	 */
	if ((adreno_gpu->fwloc == FW_LOCATION_UNKNOWN) ||
	    (adreno_gpu->fwloc == FW_LOCATION_LEGACY)) {

		ret = request_firmware_direct(&fw, fwname, drm->dev);
		if (!ret) {
			dev_info(drm->dev, "loaded %s from legacy location\n",
				newname);
			adreno_gpu->fwloc = FW_LOCATION_LEGACY;
			return fw;
		} else if (adreno_gpu->fwloc != FW_LOCATION_UNKNOWN) {
			dev_err(drm->dev, "failed to load %s: %d\n",
				fwname, ret);
			return ERR_PTR(ret);
		}
	}

	/*
	 * Finally fall back to request_firmware() for cases where the
	 * usermode helper is needed (I think mainly android)
	 */
	if ((adreno_gpu->fwloc == FW_LOCATION_UNKNOWN) ||
	    (adreno_gpu->fwloc == FW_LOCATION_HELPER)) {

		ret = request_firmware(&fw, newname, drm->dev);
		if (!ret) {
			dev_info(drm->dev, "loaded %s with helper\n",
				newname);
			adreno_gpu->fwloc = FW_LOCATION_HELPER;
			return fw;
		} else if (adreno_gpu->fwloc != FW_LOCATION_UNKNOWN) {
			dev_err(drm->dev, "failed to load %s: %d\n",
				newname, ret);
			return ERR_PTR(ret);
		}
	}

	dev_err(drm->dev, "failed to load %s\n", fwname);
	return ERR_PTR(-ENOENT);
}

static int adreno_load_fw(struct adreno_gpu *adreno_gpu)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(adreno_gpu->info->fw); i++) {
		const struct firmware *fw;

		if (!adreno_gpu->info->fw[i])
			continue;

		/* Skip if the firmware has already been loaded */
		if (adreno_gpu->fw[i])
			continue;

		fw = adreno_request_fw(adreno_gpu, adreno_gpu->info->fw[i]);
		if (IS_ERR(fw))
			return PTR_ERR(fw);

		adreno_gpu->fw[i] = fw;
	}

	return 0;
}

struct drm_gem_object *adreno_fw_create_bo(struct msm_gpu *gpu,
		const struct firmware *fw, u64 *iova)
{
	struct drm_gem_object *bo;
	void *ptr;

	ptr = msm_gem_kernel_new_locked(gpu->dev, fw->size - 4,
		MSM_BO_UNCACHED | MSM_BO_GPU_READONLY, gpu->aspace, &bo, iova);

	if (IS_ERR(ptr))
		return ERR_CAST(ptr);

	memcpy(ptr, &fw->data[4], fw->size - 4);

	msm_gem_put_vaddr(bo);

	return bo;
}

int adreno_hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	int ret, i;

	DBG("%s", gpu->name);

	ret = adreno_load_fw(adreno_gpu);
	if (ret)
		return ret;

	for (i = 0; i < gpu->nr_rings; i++) {
		struct msm_ringbuffer *ring = gpu->rb[i];

		if (!ring)
			continue;

		ret = msm_gem_get_iova(ring->bo, gpu->aspace, &ring->iova);
		if (ret) {
			ring->iova = 0;
			dev_err(gpu->dev->dev,
				"could not map ringbuffer %d: %d\n", i, ret);
			return ret;
		}

		ring->cur = ring->start;
		ring->next = ring->start;

		/* reset completed fence seqno: */
		ring->memptrs->fence = ring->seqno;
		ring->memptrs->rptr = 0;
	}

	/*
	 * Setup REG_CP_RB_CNTL.  The same value is used across targets (with
	 * the excpetion of A430 that disables the RPTR shadow) - the cacluation
	 * for the ringbuffer size and block size is moved to msm_gpu.h for the
	 * pre-processor to deal with and the A430 variant is ORed in here
	 */
	adreno_gpu_write(adreno_gpu, REG_ADRENO_CP_RB_CNTL,
		MSM_GPU_RB_CNTL_DEFAULT |
		(adreno_is_a430(adreno_gpu) ? AXXX_CP_RB_CNTL_NO_UPDATE : 0));

	/* Setup ringbuffer address - use ringbuffer[0] for GPU init */
	adreno_gpu_write64(adreno_gpu, REG_ADRENO_CP_RB_BASE,
		REG_ADRENO_CP_RB_BASE_HI, gpu->rb[0]->iova);

	if (!adreno_is_a430(adreno_gpu)) {
		adreno_gpu_write64(adreno_gpu, REG_ADRENO_CP_RB_RPTR_ADDR,
			REG_ADRENO_CP_RB_RPTR_ADDR_HI,
			rbmemptr(gpu->rb[0], rptr));
	}

	return 0;
}

/* Use this helper to read rptr, since a430 doesn't update rptr in memory */
static uint32_t get_rptr(struct adreno_gpu *adreno_gpu,
		struct msm_ringbuffer *ring)
{
	if (adreno_is_a430(adreno_gpu))
		return ring->memptrs->rptr = adreno_gpu_read(
			adreno_gpu, REG_ADRENO_CP_RB_RPTR);
	else
		return ring->memptrs->rptr;
}

struct msm_ringbuffer *adreno_active_ring(struct msm_gpu *gpu)
{
	return gpu->rb[0];
}

void adreno_recover(struct msm_gpu *gpu)
{
	struct drm_device *dev = gpu->dev;
	int ret;

	// XXX pm-runtime??  we *need* the device to be off after this
	// so maybe continuing to call ->pm_suspend/resume() is better?

	gpu->funcs->pm_suspend(gpu);
	gpu->funcs->pm_resume(gpu);

	ret = msm_gpu_hw_init(gpu);
	if (ret) {
		dev_err(dev->dev, "gpu hw init failed: %d\n", ret);
		/* hmm, oh well? */
	}
}

void adreno_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit,
		struct msm_file_private *ctx)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct msm_drm_private *priv = gpu->dev->dev_private;
	struct msm_ringbuffer *ring = submit->ring;
	unsigned i;

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
			OUT_PKT3(ring, adreno_is_a430(adreno_gpu) ?
				CP_INDIRECT_BUFFER_PFE : CP_INDIRECT_BUFFER_PFD, 2);
			OUT_RING(ring, lower_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, submit->cmd[i].size);
			OUT_PKT2(ring);
			break;
		}
	}

	OUT_PKT0(ring, REG_AXXX_CP_SCRATCH_REG2, 1);
	OUT_RING(ring, submit->seqno);

	if (adreno_is_a3xx(adreno_gpu) || adreno_is_a4xx(adreno_gpu)) {
		/* Flush HLSQ lazy updates to make sure there is nothing
		 * pending for indirect loads after the timestamp has
		 * passed:
		 */
		OUT_PKT3(ring, CP_EVENT_WRITE, 1);
		OUT_RING(ring, HLSQ_FLUSH);

		OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
		OUT_RING(ring, 0x00000000);
	}

	/* BIT(31) of CACHE_FLUSH_TS triggers CACHE_FLUSH_TS IRQ from GPU */
	OUT_PKT3(ring, CP_EVENT_WRITE, 3);
	OUT_RING(ring, CACHE_FLUSH_TS | BIT(31));
	OUT_RING(ring, rbmemptr(ring, fence));
	OUT_RING(ring, submit->seqno);

#if 0
	if (adreno_is_a3xx(adreno_gpu)) {
		/* Dummy set-constant to trigger context rollover */
		OUT_PKT3(ring, CP_SET_CONSTANT, 2);
		OUT_RING(ring, CP_REG(REG_A3XX_HLSQ_CL_KERNEL_GROUP_X_REG));
		OUT_RING(ring, 0x00000000);
	}
#endif

	gpu->funcs->flush(gpu, ring);
}

void adreno_flush(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	uint32_t wptr;

	/* Copy the shadow to the actual register */
	ring->cur = ring->next;

	/*
	 * Mask wptr value that we calculate to fit in the HW range. This is
	 * to account for the possibility that the last command fit exactly into
	 * the ringbuffer and rb->next hasn't wrapped to zero yet
	 */
	wptr = get_wptr(ring);

	/* ensure writes to ringbuffer have hit system memory: */
	mb();

	adreno_gpu_write(adreno_gpu, REG_ADRENO_CP_RB_WPTR, wptr);
}

bool adreno_idle(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	uint32_t wptr = get_wptr(ring);

	/* wait for CP to drain ringbuffer: */
	if (!spin_until(get_rptr(adreno_gpu, ring) == wptr))
		return true;

	/* TODO maybe we need to reset GPU here to recover from hang? */
	DRM_ERROR("%s: timeout waiting to drain ringbuffer %d rptr/wptr = %X/%X\n",
		gpu->name, ring->id, get_rptr(adreno_gpu, ring), wptr);

	return false;
}

#ifdef CONFIG_DEBUG_FS
void adreno_show(struct msm_gpu *gpu, struct seq_file *m)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	int i;

	seq_printf(m, "revision: %d (%d.%d.%d.%d)\n",
			adreno_gpu->info->revn, adreno_gpu->rev.core,
			adreno_gpu->rev.major, adreno_gpu->rev.minor,
			adreno_gpu->rev.patchid);

	for (i = 0; i < gpu->nr_rings; i++) {
		struct msm_ringbuffer *ring = gpu->rb[i];

		seq_printf(m, "rb %d: fence:    %d/%d\n", i,
			ring->memptrs->fence, ring->seqno);

		seq_printf(m, "      rptr:     %d\n",
			get_rptr(adreno_gpu, ring));
		seq_printf(m, "rb wptr:  %d\n", get_wptr(ring));
	}

	/* dump these out in a form that can be parsed by demsm: */
	seq_printf(m, "IO:region %s 00000000 00020000\n", gpu->name);
	for (i = 0; adreno_gpu->registers[i] != ~0; i += 2) {
		uint32_t start = adreno_gpu->registers[i];
		uint32_t end   = adreno_gpu->registers[i+1];
		uint32_t addr;

		for (addr = start; addr <= end; addr++) {
			uint32_t val = gpu_read(gpu, addr);
			seq_printf(m, "IO:R %08x %08x\n", addr<<2, val);
		}
	}
}
#endif

/* Dump common gpu status and scratch registers on any hang, to make
 * the hangcheck logs more useful.  The scratch registers seem always
 * safe to read when GPU has hung (unlike some other regs, depending
 * on how the GPU hung), and they are useful to match up to cmdstream
 * dumps when debugging hangs:
 */
void adreno_dump_info(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	int i;

	printk("revision: %d (%d.%d.%d.%d)\n",
			adreno_gpu->info->revn, adreno_gpu->rev.core,
			adreno_gpu->rev.major, adreno_gpu->rev.minor,
			adreno_gpu->rev.patchid);

	for (i = 0; i < gpu->nr_rings; i++) {
		struct msm_ringbuffer *ring = gpu->rb[i];

		printk("rb %d: fence:    %d/%d\n", i,
			ring->memptrs->fence,
			ring->seqno);

		printk("rptr:     %d\n", get_rptr(adreno_gpu, ring));
		printk("rb wptr:  %d\n", get_wptr(ring));
	}
}

/* would be nice to not have to duplicate the _show() stuff with printk(): */
void adreno_dump(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	int i;

	/* dump these out in a form that can be parsed by demsm: */
	printk("IO:region %s 00000000 00020000\n", gpu->name);
	for (i = 0; adreno_gpu->registers[i] != ~0; i += 2) {
		uint32_t start = adreno_gpu->registers[i];
		uint32_t end   = adreno_gpu->registers[i+1];
		uint32_t addr;

		for (addr = start; addr <= end; addr++) {
			uint32_t val = gpu_read(gpu, addr);
			printk("IO:R %08x %08x\n", addr<<2, val);
		}
	}
}

static uint32_t ring_freewords(struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(ring->gpu);
	uint32_t size = MSM_GPU_RINGBUFFER_SZ >> 2;
	/* Use ring->next to calculate free size */
	uint32_t wptr = ring->next - ring->start;
	uint32_t rptr = get_rptr(adreno_gpu, ring);
	return (rptr + (size - 1) - wptr) % size;
}

void adreno_wait_ring(struct msm_ringbuffer *ring, uint32_t ndwords)
{
	if (spin_until(ring_freewords(ring) >= ndwords))
		DRM_DEV_ERROR(ring->gpu->dev->dev,
			"timeout waiting for space in ringbuffer %d\n",
			ring->id);
}

/* Get legacy powerlevels from qcom,gpu-pwrlevels and populate the opp table */
static int adreno_get_legacy_pwrlevels(struct device *dev)
{
	struct device_node *child, *node;
	int ret;

	node = of_find_compatible_node(dev->of_node, NULL,
		"qcom,gpu-pwrlevels");
	if (!node) {
		dev_err(dev, "Could not find the GPU powerlevels\n");
		return -ENXIO;
	}

	for_each_child_of_node(node, child) {
		unsigned int val;

		ret = of_property_read_u32(child, "qcom,gpu-freq", &val);
		if (ret)
			continue;

		/*
		 * Skip the intentionally bogus clock value found at the bottom
		 * of most legacy frequency tables
		 */
		if (val != 27000000)
			dev_pm_opp_add(dev, val, 0);
	}

	return 0;
}

static int adreno_get_pwrlevels(struct device *dev,
		struct msm_gpu *gpu)
{
	unsigned long freq = ULONG_MAX;
	struct dev_pm_opp *opp;
	int ret;

	gpu->fast_rate = 0;

	/* You down with OPP? */
	if (!of_find_property(dev->of_node, "operating-points-v2", NULL))
		ret = adreno_get_legacy_pwrlevels(dev);
	else {
		ret = dev_pm_opp_of_add_table(dev);
		if (ret)
			dev_err(dev, "Unable to set the OPP table\n");
	}

	if (!ret) {
		/* Find the fastest defined rate */
		opp = dev_pm_opp_find_freq_floor(dev, &freq);
		if (!IS_ERR(opp)) {
			gpu->fast_rate = freq;
			dev_pm_opp_put(opp);
		}
	}

	if (!gpu->fast_rate) {
		dev_warn(dev,
			"Could not find a clock rate. Using a reasonable default\n");
		/* Pick a suitably safe clock speed for any target */
		gpu->fast_rate = 200000000;
	}

	DBG("fast_rate=%u, slow_rate=27000000", gpu->fast_rate);

	return 0;
}

int adreno_gpu_init(struct drm_device *drm, struct platform_device *pdev,
		struct adreno_gpu *adreno_gpu,
		const struct adreno_gpu_funcs *funcs, int nr_rings)
{
	struct adreno_platform_config *config = pdev->dev.platform_data;
	struct msm_gpu_config adreno_gpu_config  = { 0 };
	struct msm_gpu *gpu = &adreno_gpu->base;

	adreno_gpu->funcs = funcs;
	adreno_gpu->info = adreno_info(config->rev);
	adreno_gpu->gmem = adreno_gpu->info->gmem;
	adreno_gpu->revn = adreno_gpu->info->revn;
	adreno_gpu->rev = config->rev;

	adreno_gpu_config.ioname = "kgsl_3d0_reg_memory";
	adreno_gpu_config.irqname = "kgsl_3d0_irq";

	adreno_gpu_config.va_start = SZ_16M;
	adreno_gpu_config.va_end = 0xffffffff;

	adreno_gpu_config.nr_rings = nr_rings;

	adreno_get_pwrlevels(&pdev->dev, gpu);

	pm_runtime_set_autosuspend_delay(&pdev->dev, DRM_MSM_INACTIVE_PERIOD);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return msm_gpu_init(drm, pdev, &adreno_gpu->base, &funcs->base,
			adreno_gpu->info->name, &adreno_gpu_config);
}

void adreno_gpu_cleanup(struct adreno_gpu *adreno_gpu)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(adreno_gpu->info->fw); i++)
		release_firmware(adreno_gpu->fw[i]);

	msm_gpu_cleanup(&adreno_gpu->base);
}
