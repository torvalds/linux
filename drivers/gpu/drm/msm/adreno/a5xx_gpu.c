/* Copyright (c) 2016-2017 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/qcom_scm.h>
#include <linux/dma-mapping.h>
#include <linux/of_address.h>
#include <linux/soc/qcom/mdt_loader.h>
#include "msm_gem.h"
#include "msm_mmu.h"
#include "a5xx_gpu.h"

extern bool hang_debug;
static void a5xx_dump(struct msm_gpu *gpu);

#define GPU_PAS_ID 13

static int zap_shader_load_mdt(struct device *dev, const char *fwname)
{
	const struct firmware *fw;
	struct device_node *np;
	struct resource r;
	phys_addr_t mem_phys;
	ssize_t mem_size;
	void *mem_region = NULL;
	int ret;

	if (!IS_ENABLED(CONFIG_ARCH_QCOM))
		return -EINVAL;

	np = of_get_child_by_name(dev->of_node, "zap-shader");
	if (!np)
		return -ENODEV;

	np = of_parse_phandle(np, "memory-region", 0);
	if (!np)
		return -EINVAL;

	ret = of_address_to_resource(np, 0, &r);
	if (ret)
		return ret;

	mem_phys = r.start;
	mem_size = resource_size(&r);

	/* Request the MDT file for the firmware */
	ret = request_firmware(&fw, fwname, dev);
	if (ret) {
		DRM_DEV_ERROR(dev, "Unable to load %s\n", fwname);
		return ret;
	}

	/* Figure out how much memory we need */
	mem_size = qcom_mdt_get_size(fw);
	if (mem_size < 0) {
		ret = mem_size;
		goto out;
	}

	/* Allocate memory for the firmware image */
	mem_region = memremap(mem_phys, mem_size,  MEMREMAP_WC);
	if (!mem_region) {
		ret = -ENOMEM;
		goto out;
	}

	/* Load the rest of the MDT */
	ret = qcom_mdt_load(dev, fw, fwname, GPU_PAS_ID, mem_region, mem_phys,
		mem_size);
	if (ret)
		goto out;

	/* Send the image to the secure world */
	ret = qcom_scm_pas_auth_and_reset(GPU_PAS_ID);
	if (ret)
		DRM_DEV_ERROR(dev, "Unable to authorize the image\n");

out:
	if (mem_region)
		memunmap(mem_region);

	release_firmware(fw);

	return ret;
}

static void a5xx_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit,
	struct msm_file_private *ctx)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct msm_drm_private *priv = gpu->dev->dev_private;
	struct msm_ringbuffer *ring = gpu->rb;
	unsigned int i, ibs = 0;

	for (i = 0; i < submit->nr_cmds; i++) {
		switch (submit->cmd[i].type) {
		case MSM_SUBMIT_CMD_IB_TARGET_BUF:
			break;
		case MSM_SUBMIT_CMD_CTX_RESTORE_BUF:
			if (priv->lastctx == ctx)
				break;
		case MSM_SUBMIT_CMD_BUF:
			OUT_PKT7(ring, CP_INDIRECT_BUFFER_PFE, 3);
			OUT_RING(ring, lower_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, upper_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, submit->cmd[i].size);
			ibs++;
			break;
		}
	}

	OUT_PKT4(ring, REG_A5XX_CP_SCRATCH_REG(2), 1);
	OUT_RING(ring, submit->fence->seqno);

	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, CACHE_FLUSH_TS | (1 << 31));
	OUT_RING(ring, lower_32_bits(rbmemptr(adreno_gpu, fence)));
	OUT_RING(ring, upper_32_bits(rbmemptr(adreno_gpu, fence)));
	OUT_RING(ring, submit->fence->seqno);

	gpu->funcs->flush(gpu);
}

static const struct {
	u32 offset;
	u32 value;
} a5xx_hwcg[] = {
	{REG_A5XX_RBBM_CLOCK_CNTL_SP0, 0x02222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_SP1, 0x02222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_SP2, 0x02222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_SP3, 0x02222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_SP0, 0x02222220},
	{REG_A5XX_RBBM_CLOCK_CNTL2_SP1, 0x02222220},
	{REG_A5XX_RBBM_CLOCK_CNTL2_SP2, 0x02222220},
	{REG_A5XX_RBBM_CLOCK_CNTL2_SP3, 0x02222220},
	{REG_A5XX_RBBM_CLOCK_HYST_SP0, 0x0000F3CF},
	{REG_A5XX_RBBM_CLOCK_HYST_SP1, 0x0000F3CF},
	{REG_A5XX_RBBM_CLOCK_HYST_SP2, 0x0000F3CF},
	{REG_A5XX_RBBM_CLOCK_HYST_SP3, 0x0000F3CF},
	{REG_A5XX_RBBM_CLOCK_DELAY_SP0, 0x00000080},
	{REG_A5XX_RBBM_CLOCK_DELAY_SP1, 0x00000080},
	{REG_A5XX_RBBM_CLOCK_DELAY_SP2, 0x00000080},
	{REG_A5XX_RBBM_CLOCK_DELAY_SP3, 0x00000080},
	{REG_A5XX_RBBM_CLOCK_CNTL_TP0, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_TP1, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_TP2, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_TP3, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_TP0, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_TP1, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_TP2, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_TP3, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL3_TP0, 0x00002222},
	{REG_A5XX_RBBM_CLOCK_CNTL3_TP1, 0x00002222},
	{REG_A5XX_RBBM_CLOCK_CNTL3_TP2, 0x00002222},
	{REG_A5XX_RBBM_CLOCK_CNTL3_TP3, 0x00002222},
	{REG_A5XX_RBBM_CLOCK_HYST_TP0, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST_TP1, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST_TP2, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST_TP3, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST2_TP0, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST2_TP1, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST2_TP2, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST2_TP3, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST3_TP0, 0x00007777},
	{REG_A5XX_RBBM_CLOCK_HYST3_TP1, 0x00007777},
	{REG_A5XX_RBBM_CLOCK_HYST3_TP2, 0x00007777},
	{REG_A5XX_RBBM_CLOCK_HYST3_TP3, 0x00007777},
	{REG_A5XX_RBBM_CLOCK_DELAY_TP0, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY_TP1, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY_TP2, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY_TP3, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY2_TP0, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY2_TP1, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY2_TP2, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY2_TP3, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY3_TP0, 0x00001111},
	{REG_A5XX_RBBM_CLOCK_DELAY3_TP1, 0x00001111},
	{REG_A5XX_RBBM_CLOCK_DELAY3_TP2, 0x00001111},
	{REG_A5XX_RBBM_CLOCK_DELAY3_TP3, 0x00001111},
	{REG_A5XX_RBBM_CLOCK_CNTL_UCHE, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_UCHE, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL3_UCHE, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL4_UCHE, 0x00222222},
	{REG_A5XX_RBBM_CLOCK_HYST_UCHE, 0x00444444},
	{REG_A5XX_RBBM_CLOCK_DELAY_UCHE, 0x00000002},
	{REG_A5XX_RBBM_CLOCK_CNTL_RB0, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_RB1, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_RB2, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_RB3, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_RB0, 0x00222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_RB1, 0x00222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_RB2, 0x00222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_RB3, 0x00222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_CCU0, 0x00022220},
	{REG_A5XX_RBBM_CLOCK_CNTL_CCU1, 0x00022220},
	{REG_A5XX_RBBM_CLOCK_CNTL_CCU2, 0x00022220},
	{REG_A5XX_RBBM_CLOCK_CNTL_CCU3, 0x00022220},
	{REG_A5XX_RBBM_CLOCK_CNTL_RAC, 0x05522222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_RAC, 0x00505555},
	{REG_A5XX_RBBM_CLOCK_HYST_RB_CCU0, 0x04040404},
	{REG_A5XX_RBBM_CLOCK_HYST_RB_CCU1, 0x04040404},
	{REG_A5XX_RBBM_CLOCK_HYST_RB_CCU2, 0x04040404},
	{REG_A5XX_RBBM_CLOCK_HYST_RB_CCU3, 0x04040404},
	{REG_A5XX_RBBM_CLOCK_HYST_RAC, 0x07444044},
	{REG_A5XX_RBBM_CLOCK_DELAY_RB_CCU_L1_0, 0x00000002},
	{REG_A5XX_RBBM_CLOCK_DELAY_RB_CCU_L1_1, 0x00000002},
	{REG_A5XX_RBBM_CLOCK_DELAY_RB_CCU_L1_2, 0x00000002},
	{REG_A5XX_RBBM_CLOCK_DELAY_RB_CCU_L1_3, 0x00000002},
	{REG_A5XX_RBBM_CLOCK_DELAY_RAC, 0x00010011},
	{REG_A5XX_RBBM_CLOCK_CNTL_TSE_RAS_RBBM, 0x04222222},
	{REG_A5XX_RBBM_CLOCK_MODE_GPC, 0x02222222},
	{REG_A5XX_RBBM_CLOCK_MODE_VFD, 0x00002222},
	{REG_A5XX_RBBM_CLOCK_HYST_TSE_RAS_RBBM, 0x00000000},
	{REG_A5XX_RBBM_CLOCK_HYST_GPC, 0x04104004},
	{REG_A5XX_RBBM_CLOCK_HYST_VFD, 0x00000000},
	{REG_A5XX_RBBM_CLOCK_DELAY_HLSQ, 0x00000000},
	{REG_A5XX_RBBM_CLOCK_DELAY_TSE_RAS_RBBM, 0x00004000},
	{REG_A5XX_RBBM_CLOCK_DELAY_GPC, 0x00000200},
	{REG_A5XX_RBBM_CLOCK_DELAY_VFD, 0x00002222}
};

void a5xx_set_hwcg(struct msm_gpu *gpu, bool state)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(a5xx_hwcg); i++)
		gpu_write(gpu, a5xx_hwcg[i].offset,
			state ? a5xx_hwcg[i].value : 0);

	gpu_write(gpu, REG_A5XX_RBBM_CLOCK_CNTL, state ? 0xAAA8AA00 : 0);
	gpu_write(gpu, REG_A5XX_RBBM_ISDB_CNT, state ? 0x182 : 0x180);
}

static int a5xx_me_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct msm_ringbuffer *ring = gpu->rb;

	OUT_PKT7(ring, CP_ME_INIT, 8);

	OUT_RING(ring, 0x0000002F);

	/* Enable multiple hardware contexts */
	OUT_RING(ring, 0x00000003);

	/* Enable error detection */
	OUT_RING(ring, 0x20000000);

	/* Don't enable header dump */
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);

	/* Specify workarounds for various microcode issues */
	if (adreno_is_a530(adreno_gpu)) {
		/* Workaround for token end syncs
		 * Force a WFI after every direct-render 3D mode draw and every
		 * 2D mode 3 draw
		 */
		OUT_RING(ring, 0x0000000B);
	} else {
		/* No workarounds enabled */
		OUT_RING(ring, 0x00000000);
	}

	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);

	gpu->funcs->flush(gpu);

	return a5xx_idle(gpu) ? 0 : -EINVAL;
}

static struct drm_gem_object *a5xx_ucode_load_bo(struct msm_gpu *gpu,
		const struct firmware *fw, u64 *iova)
{
	struct drm_device *drm = gpu->dev;
	struct drm_gem_object *bo;
	void *ptr;

	bo = msm_gem_new_locked(drm, fw->size - 4, MSM_BO_UNCACHED);
	if (IS_ERR(bo))
		return bo;

	ptr = msm_gem_get_vaddr(bo);
	if (!ptr) {
		drm_gem_object_unreference(bo);
		return ERR_PTR(-ENOMEM);
	}

	if (iova) {
		int ret = msm_gem_get_iova(bo, gpu->aspace, iova);

		if (ret) {
			drm_gem_object_unreference(bo);
			return ERR_PTR(ret);
		}
	}

	memcpy(ptr, &fw->data[4], fw->size - 4);

	msm_gem_put_vaddr(bo);
	return bo;
}

static int a5xx_ucode_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);
	int ret;

	if (!a5xx_gpu->pm4_bo) {
		a5xx_gpu->pm4_bo = a5xx_ucode_load_bo(gpu, adreno_gpu->pm4,
			&a5xx_gpu->pm4_iova);

		if (IS_ERR(a5xx_gpu->pm4_bo)) {
			ret = PTR_ERR(a5xx_gpu->pm4_bo);
			a5xx_gpu->pm4_bo = NULL;
			dev_err(gpu->dev->dev, "could not allocate PM4: %d\n",
				ret);
			return ret;
		}
	}

	if (!a5xx_gpu->pfp_bo) {
		a5xx_gpu->pfp_bo = a5xx_ucode_load_bo(gpu, adreno_gpu->pfp,
			&a5xx_gpu->pfp_iova);

		if (IS_ERR(a5xx_gpu->pfp_bo)) {
			ret = PTR_ERR(a5xx_gpu->pfp_bo);
			a5xx_gpu->pfp_bo = NULL;
			dev_err(gpu->dev->dev, "could not allocate PFP: %d\n",
				ret);
			return ret;
		}
	}

	gpu_write64(gpu, REG_A5XX_CP_ME_INSTR_BASE_LO,
		REG_A5XX_CP_ME_INSTR_BASE_HI, a5xx_gpu->pm4_iova);

	gpu_write64(gpu, REG_A5XX_CP_PFP_INSTR_BASE_LO,
		REG_A5XX_CP_PFP_INSTR_BASE_HI, a5xx_gpu->pfp_iova);

	return 0;
}

#define SCM_GPU_ZAP_SHADER_RESUME 0

static int a5xx_zap_shader_resume(struct msm_gpu *gpu)
{
	int ret;

	ret = qcom_scm_set_remote_state(SCM_GPU_ZAP_SHADER_RESUME, GPU_PAS_ID);
	if (ret)
		DRM_ERROR("%s: zap-shader resume failed: %d\n",
			gpu->name, ret);

	return ret;
}

static int a5xx_zap_shader_init(struct msm_gpu *gpu)
{
	static bool loaded;
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);
	struct platform_device *pdev = a5xx_gpu->pdev;
	int ret;

	/*
	 * If the zap shader is already loaded into memory we just need to kick
	 * the remote processor to reinitialize it
	 */
	if (loaded)
		return a5xx_zap_shader_resume(gpu);

	/* We need SCM to be able to load the firmware */
	if (!qcom_scm_is_available()) {
		DRM_DEV_ERROR(&pdev->dev, "SCM is not available\n");
		return -EPROBE_DEFER;
	}

	/* Each GPU has a target specific zap shader firmware name to use */
	if (!adreno_gpu->info->zapfw) {
		DRM_DEV_ERROR(&pdev->dev,
			"Zap shader firmware file not specified for this target\n");
		return -ENODEV;
	}

	ret = zap_shader_load_mdt(&pdev->dev, adreno_gpu->info->zapfw);

	loaded = !ret;

	return ret;
}

#define A5XX_INT_MASK (A5XX_RBBM_INT_0_MASK_RBBM_AHB_ERROR | \
	  A5XX_RBBM_INT_0_MASK_RBBM_TRANSFER_TIMEOUT | \
	  A5XX_RBBM_INT_0_MASK_RBBM_ME_MS_TIMEOUT | \
	  A5XX_RBBM_INT_0_MASK_RBBM_PFP_MS_TIMEOUT | \
	  A5XX_RBBM_INT_0_MASK_RBBM_ETS_MS_TIMEOUT | \
	  A5XX_RBBM_INT_0_MASK_RBBM_ATB_ASYNC_OVERFLOW | \
	  A5XX_RBBM_INT_0_MASK_CP_HW_ERROR | \
	  A5XX_RBBM_INT_0_MASK_CP_CACHE_FLUSH_TS | \
	  A5XX_RBBM_INT_0_MASK_UCHE_OOB_ACCESS | \
	  A5XX_RBBM_INT_0_MASK_GPMU_VOLTAGE_DROOP)

static int a5xx_hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	int ret;

	gpu_write(gpu, REG_A5XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x00000003);

	/* Make all blocks contribute to the GPU BUSY perf counter */
	gpu_write(gpu, REG_A5XX_RBBM_PERFCTR_GPU_BUSY_MASKED, 0xFFFFFFFF);

	/* Enable RBBM error reporting bits */
	gpu_write(gpu, REG_A5XX_RBBM_AHB_CNTL0, 0x00000001);

	if (adreno_gpu->info->quirks & ADRENO_QUIRK_FAULT_DETECT_MASK) {
		/*
		 * Mask out the activity signals from RB1-3 to avoid false
		 * positives
		 */

		gpu_write(gpu, REG_A5XX_RBBM_INTERFACE_HANG_MASK_CNTL11,
			0xF0000000);
		gpu_write(gpu, REG_A5XX_RBBM_INTERFACE_HANG_MASK_CNTL12,
			0xFFFFFFFF);
		gpu_write(gpu, REG_A5XX_RBBM_INTERFACE_HANG_MASK_CNTL13,
			0xFFFFFFFF);
		gpu_write(gpu, REG_A5XX_RBBM_INTERFACE_HANG_MASK_CNTL14,
			0xFFFFFFFF);
		gpu_write(gpu, REG_A5XX_RBBM_INTERFACE_HANG_MASK_CNTL15,
			0xFFFFFFFF);
		gpu_write(gpu, REG_A5XX_RBBM_INTERFACE_HANG_MASK_CNTL16,
			0xFFFFFFFF);
		gpu_write(gpu, REG_A5XX_RBBM_INTERFACE_HANG_MASK_CNTL17,
			0xFFFFFFFF);
		gpu_write(gpu, REG_A5XX_RBBM_INTERFACE_HANG_MASK_CNTL18,
			0xFFFFFFFF);
	}

	/* Enable fault detection */
	gpu_write(gpu, REG_A5XX_RBBM_INTERFACE_HANG_INT_CNTL,
		(1 << 30) | 0xFFFF);

	/* Turn on performance counters */
	gpu_write(gpu, REG_A5XX_RBBM_PERFCTR_CNTL, 0x01);

	/* Increase VFD cache access so LRZ and other data gets evicted less */
	gpu_write(gpu, REG_A5XX_UCHE_CACHE_WAYS, 0x02);

	/* Disable L2 bypass in the UCHE */
	gpu_write(gpu, REG_A5XX_UCHE_TRAP_BASE_LO, 0xFFFF0000);
	gpu_write(gpu, REG_A5XX_UCHE_TRAP_BASE_HI, 0x0001FFFF);
	gpu_write(gpu, REG_A5XX_UCHE_WRITE_THRU_BASE_LO, 0xFFFF0000);
	gpu_write(gpu, REG_A5XX_UCHE_WRITE_THRU_BASE_HI, 0x0001FFFF);

	/* Set the GMEM VA range (0 to gpu->gmem) */
	gpu_write(gpu, REG_A5XX_UCHE_GMEM_RANGE_MIN_LO, 0x00100000);
	gpu_write(gpu, REG_A5XX_UCHE_GMEM_RANGE_MIN_HI, 0x00000000);
	gpu_write(gpu, REG_A5XX_UCHE_GMEM_RANGE_MAX_LO,
		0x00100000 + adreno_gpu->gmem - 1);
	gpu_write(gpu, REG_A5XX_UCHE_GMEM_RANGE_MAX_HI, 0x00000000);

	gpu_write(gpu, REG_A5XX_CP_MEQ_THRESHOLDS, 0x40);
	gpu_write(gpu, REG_A5XX_CP_MERCIU_SIZE, 0x40);
	gpu_write(gpu, REG_A5XX_CP_ROQ_THRESHOLDS_2, 0x80000060);
	gpu_write(gpu, REG_A5XX_CP_ROQ_THRESHOLDS_1, 0x40201B16);

	gpu_write(gpu, REG_A5XX_PC_DBG_ECO_CNTL, (0x400 << 11 | 0x300 << 22));

	if (adreno_gpu->info->quirks & ADRENO_QUIRK_TWO_PASS_USE_WFI)
		gpu_rmw(gpu, REG_A5XX_PC_DBG_ECO_CNTL, 0, (1 << 8));

	gpu_write(gpu, REG_A5XX_PC_DBG_ECO_CNTL, 0xc0200100);

	/* Enable USE_RETENTION_FLOPS */
	gpu_write(gpu, REG_A5XX_CP_CHICKEN_DBG, 0x02000000);

	/* Enable ME/PFP split notification */
	gpu_write(gpu, REG_A5XX_RBBM_AHB_CNTL1, 0xA6FFFFFF);

	/* Enable HWCG */
	a5xx_set_hwcg(gpu, true);

	gpu_write(gpu, REG_A5XX_RBBM_AHB_CNTL2, 0x0000003F);

	/* Set the highest bank bit */
	gpu_write(gpu, REG_A5XX_TPL1_MODE_CNTL, 2 << 7);
	gpu_write(gpu, REG_A5XX_RB_MODE_CNTL, 2 << 1);

	/* Protect registers from the CP */
	gpu_write(gpu, REG_A5XX_CP_PROTECT_CNTL, 0x00000007);

	/* RBBM */
	gpu_write(gpu, REG_A5XX_CP_PROTECT(0), ADRENO_PROTECT_RW(0x04, 4));
	gpu_write(gpu, REG_A5XX_CP_PROTECT(1), ADRENO_PROTECT_RW(0x08, 8));
	gpu_write(gpu, REG_A5XX_CP_PROTECT(2), ADRENO_PROTECT_RW(0x10, 16));
	gpu_write(gpu, REG_A5XX_CP_PROTECT(3), ADRENO_PROTECT_RW(0x20, 32));
	gpu_write(gpu, REG_A5XX_CP_PROTECT(4), ADRENO_PROTECT_RW(0x40, 64));
	gpu_write(gpu, REG_A5XX_CP_PROTECT(5), ADRENO_PROTECT_RW(0x80, 64));

	/* Content protect */
	gpu_write(gpu, REG_A5XX_CP_PROTECT(6),
		ADRENO_PROTECT_RW(REG_A5XX_RBBM_SECVID_TSB_TRUSTED_BASE_LO,
			16));
	gpu_write(gpu, REG_A5XX_CP_PROTECT(7),
		ADRENO_PROTECT_RW(REG_A5XX_RBBM_SECVID_TRUST_CNTL, 2));

	/* CP */
	gpu_write(gpu, REG_A5XX_CP_PROTECT(8), ADRENO_PROTECT_RW(0x800, 64));
	gpu_write(gpu, REG_A5XX_CP_PROTECT(9), ADRENO_PROTECT_RW(0x840, 8));
	gpu_write(gpu, REG_A5XX_CP_PROTECT(10), ADRENO_PROTECT_RW(0x880, 32));
	gpu_write(gpu, REG_A5XX_CP_PROTECT(11), ADRENO_PROTECT_RW(0xAA0, 1));

	/* RB */
	gpu_write(gpu, REG_A5XX_CP_PROTECT(12), ADRENO_PROTECT_RW(0xCC0, 1));
	gpu_write(gpu, REG_A5XX_CP_PROTECT(13), ADRENO_PROTECT_RW(0xCF0, 2));

	/* VPC */
	gpu_write(gpu, REG_A5XX_CP_PROTECT(14), ADRENO_PROTECT_RW(0xE68, 8));
	gpu_write(gpu, REG_A5XX_CP_PROTECT(15), ADRENO_PROTECT_RW(0xE70, 4));

	/* UCHE */
	gpu_write(gpu, REG_A5XX_CP_PROTECT(16), ADRENO_PROTECT_RW(0xE80, 16));

	if (adreno_is_a530(adreno_gpu))
		gpu_write(gpu, REG_A5XX_CP_PROTECT(17),
			ADRENO_PROTECT_RW(0x10000, 0x8000));

	gpu_write(gpu, REG_A5XX_RBBM_SECVID_TSB_CNTL, 0);
	/*
	 * Disable the trusted memory range - we don't actually supported secure
	 * memory rendering at this point in time and we don't want to block off
	 * part of the virtual memory space.
	 */
	gpu_write64(gpu, REG_A5XX_RBBM_SECVID_TSB_TRUSTED_BASE_LO,
		REG_A5XX_RBBM_SECVID_TSB_TRUSTED_BASE_HI, 0x00000000);
	gpu_write(gpu, REG_A5XX_RBBM_SECVID_TSB_TRUSTED_SIZE, 0x00000000);

	/* Load the GPMU firmware before starting the HW init */
	a5xx_gpmu_ucode_init(gpu);

	ret = adreno_hw_init(gpu);
	if (ret)
		return ret;

	ret = a5xx_ucode_init(gpu);
	if (ret)
		return ret;

	/* Disable the interrupts through the initial bringup stage */
	gpu_write(gpu, REG_A5XX_RBBM_INT_0_MASK, A5XX_INT_MASK);

	/* Clear ME_HALT to start the micro engine */
	gpu_write(gpu, REG_A5XX_CP_PFP_ME_CNTL, 0);
	ret = a5xx_me_init(gpu);
	if (ret)
		return ret;

	ret = a5xx_power_init(gpu);
	if (ret)
		return ret;

	/*
	 * Send a pipeline event stat to get misbehaving counters to start
	 * ticking correctly
	 */
	if (adreno_is_a530(adreno_gpu)) {
		OUT_PKT7(gpu->rb, CP_EVENT_WRITE, 1);
		OUT_RING(gpu->rb, 0x0F);

		gpu->funcs->flush(gpu);
		if (!a5xx_idle(gpu))
			return -EINVAL;
	}

	/*
	 * Try to load a zap shader into the secure world. If successful
	 * we can use the CP to switch out of secure mode. If not then we
	 * have no resource but to try to switch ourselves out manually. If we
	 * guessed wrong then access to the RBBM_SECVID_TRUST_CNTL register will
	 * be blocked and a permissions violation will soon follow.
	 */
	ret = a5xx_zap_shader_init(gpu);
	if (!ret) {
		OUT_PKT7(gpu->rb, CP_SET_SECURE_MODE, 1);
		OUT_RING(gpu->rb, 0x00000000);

		gpu->funcs->flush(gpu);
		if (!a5xx_idle(gpu))
			return -EINVAL;
	} else {
		/* Print a warning so if we die, we know why */
		dev_warn_once(gpu->dev->dev,
			"Zap shader not enabled - using SECVID_TRUST_CNTL instead\n");
		gpu_write(gpu, REG_A5XX_RBBM_SECVID_TRUST_CNTL, 0x0);
	}

	return 0;
}

static void a5xx_recover(struct msm_gpu *gpu)
{
	int i;

	adreno_dump_info(gpu);

	for (i = 0; i < 8; i++) {
		printk("CP_SCRATCH_REG%d: %u\n", i,
			gpu_read(gpu, REG_A5XX_CP_SCRATCH_REG(i)));
	}

	if (hang_debug)
		a5xx_dump(gpu);

	gpu_write(gpu, REG_A5XX_RBBM_SW_RESET_CMD, 1);
	gpu_read(gpu, REG_A5XX_RBBM_SW_RESET_CMD);
	gpu_write(gpu, REG_A5XX_RBBM_SW_RESET_CMD, 0);
	adreno_recover(gpu);
}

static void a5xx_destroy(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);

	DBG("%s", gpu->name);

	if (a5xx_gpu->pm4_bo) {
		if (a5xx_gpu->pm4_iova)
			msm_gem_put_iova(a5xx_gpu->pm4_bo, gpu->aspace);
		drm_gem_object_unreference_unlocked(a5xx_gpu->pm4_bo);
	}

	if (a5xx_gpu->pfp_bo) {
		if (a5xx_gpu->pfp_iova)
			msm_gem_put_iova(a5xx_gpu->pfp_bo, gpu->aspace);
		drm_gem_object_unreference_unlocked(a5xx_gpu->pfp_bo);
	}

	if (a5xx_gpu->gpmu_bo) {
		if (a5xx_gpu->gpmu_iova)
			msm_gem_put_iova(a5xx_gpu->gpmu_bo, gpu->aspace);
		drm_gem_object_unreference_unlocked(a5xx_gpu->gpmu_bo);
	}

	adreno_gpu_cleanup(adreno_gpu);
	kfree(a5xx_gpu);
}

static inline bool _a5xx_check_idle(struct msm_gpu *gpu)
{
	if (gpu_read(gpu, REG_A5XX_RBBM_STATUS) & ~A5XX_RBBM_STATUS_HI_BUSY)
		return false;

	/*
	 * Nearly every abnormality ends up pausing the GPU and triggering a
	 * fault so we can safely just watch for this one interrupt to fire
	 */
	return !(gpu_read(gpu, REG_A5XX_RBBM_INT_0_STATUS) &
		A5XX_RBBM_INT_0_MASK_MISC_HANG_DETECT);
}

bool a5xx_idle(struct msm_gpu *gpu)
{
	/* wait for CP to drain ringbuffer: */
	if (!adreno_idle(gpu))
		return false;

	if (spin_until(_a5xx_check_idle(gpu))) {
		DRM_ERROR("%s: %ps: timeout waiting for GPU to idle: status %8.8X irq %8.8X\n",
			gpu->name, __builtin_return_address(0),
			gpu_read(gpu, REG_A5XX_RBBM_STATUS),
			gpu_read(gpu, REG_A5XX_RBBM_INT_0_STATUS));

		return false;
	}

	return true;
}

static int a5xx_fault_handler(void *arg, unsigned long iova, int flags)
{
	struct msm_gpu *gpu = arg;
	pr_warn_ratelimited("*** gpu fault: iova=%08lx, flags=%d (%u,%u,%u,%u)\n",
			iova, flags,
			gpu_read(gpu, REG_A5XX_CP_SCRATCH_REG(4)),
			gpu_read(gpu, REG_A5XX_CP_SCRATCH_REG(5)),
			gpu_read(gpu, REG_A5XX_CP_SCRATCH_REG(6)),
			gpu_read(gpu, REG_A5XX_CP_SCRATCH_REG(7)));

	return -EFAULT;
}

static void a5xx_cp_err_irq(struct msm_gpu *gpu)
{
	u32 status = gpu_read(gpu, REG_A5XX_CP_INTERRUPT_STATUS);

	if (status & A5XX_CP_INT_CP_OPCODE_ERROR) {
		u32 val;

		gpu_write(gpu, REG_A5XX_CP_PFP_STAT_ADDR, 0);

		/*
		 * REG_A5XX_CP_PFP_STAT_DATA is indexed, and we want index 1 so
		 * read it twice
		 */

		gpu_read(gpu, REG_A5XX_CP_PFP_STAT_DATA);
		val = gpu_read(gpu, REG_A5XX_CP_PFP_STAT_DATA);

		dev_err_ratelimited(gpu->dev->dev, "CP | opcode error | possible opcode=0x%8.8X\n",
			val);
	}

	if (status & A5XX_CP_INT_CP_HW_FAULT_ERROR)
		dev_err_ratelimited(gpu->dev->dev, "CP | HW fault | status=0x%8.8X\n",
			gpu_read(gpu, REG_A5XX_CP_HW_FAULT));

	if (status & A5XX_CP_INT_CP_DMA_ERROR)
		dev_err_ratelimited(gpu->dev->dev, "CP | DMA error\n");

	if (status & A5XX_CP_INT_CP_REGISTER_PROTECTION_ERROR) {
		u32 val = gpu_read(gpu, REG_A5XX_CP_PROTECT_STATUS);

		dev_err_ratelimited(gpu->dev->dev,
			"CP | protected mode error | %s | addr=0x%8.8X | status=0x%8.8X\n",
			val & (1 << 24) ? "WRITE" : "READ",
			(val & 0xFFFFF) >> 2, val);
	}

	if (status & A5XX_CP_INT_CP_AHB_ERROR) {
		u32 status = gpu_read(gpu, REG_A5XX_CP_AHB_FAULT);
		const char *access[16] = { "reserved", "reserved",
			"timestamp lo", "timestamp hi", "pfp read", "pfp write",
			"", "", "me read", "me write", "", "", "crashdump read",
			"crashdump write" };

		dev_err_ratelimited(gpu->dev->dev,
			"CP | AHB error | addr=%X access=%s error=%d | status=0x%8.8X\n",
			status & 0xFFFFF, access[(status >> 24) & 0xF],
			(status & (1 << 31)), status);
	}
}

static void a5xx_rbbm_err_irq(struct msm_gpu *gpu, u32 status)
{
	if (status & A5XX_RBBM_INT_0_MASK_RBBM_AHB_ERROR) {
		u32 val = gpu_read(gpu, REG_A5XX_RBBM_AHB_ERROR_STATUS);

		dev_err_ratelimited(gpu->dev->dev,
			"RBBM | AHB bus error | %s | addr=0x%X | ports=0x%X:0x%X\n",
			val & (1 << 28) ? "WRITE" : "READ",
			(val & 0xFFFFF) >> 2, (val >> 20) & 0x3,
			(val >> 24) & 0xF);

		/* Clear the error */
		gpu_write(gpu, REG_A5XX_RBBM_AHB_CMD, (1 << 4));

		/* Clear the interrupt */
		gpu_write(gpu, REG_A5XX_RBBM_INT_CLEAR_CMD,
			A5XX_RBBM_INT_0_MASK_RBBM_AHB_ERROR);
	}

	if (status & A5XX_RBBM_INT_0_MASK_RBBM_TRANSFER_TIMEOUT)
		dev_err_ratelimited(gpu->dev->dev, "RBBM | AHB transfer timeout\n");

	if (status & A5XX_RBBM_INT_0_MASK_RBBM_ME_MS_TIMEOUT)
		dev_err_ratelimited(gpu->dev->dev, "RBBM | ME master split | status=0x%X\n",
			gpu_read(gpu, REG_A5XX_RBBM_AHB_ME_SPLIT_STATUS));

	if (status & A5XX_RBBM_INT_0_MASK_RBBM_PFP_MS_TIMEOUT)
		dev_err_ratelimited(gpu->dev->dev, "RBBM | PFP master split | status=0x%X\n",
			gpu_read(gpu, REG_A5XX_RBBM_AHB_PFP_SPLIT_STATUS));

	if (status & A5XX_RBBM_INT_0_MASK_RBBM_ETS_MS_TIMEOUT)
		dev_err_ratelimited(gpu->dev->dev, "RBBM | ETS master split | status=0x%X\n",
			gpu_read(gpu, REG_A5XX_RBBM_AHB_ETS_SPLIT_STATUS));

	if (status & A5XX_RBBM_INT_0_MASK_RBBM_ATB_ASYNC_OVERFLOW)
		dev_err_ratelimited(gpu->dev->dev, "RBBM | ATB ASYNC overflow\n");

	if (status & A5XX_RBBM_INT_0_MASK_RBBM_ATB_BUS_OVERFLOW)
		dev_err_ratelimited(gpu->dev->dev, "RBBM | ATB bus overflow\n");
}

static void a5xx_uche_err_irq(struct msm_gpu *gpu)
{
	uint64_t addr = (uint64_t) gpu_read(gpu, REG_A5XX_UCHE_TRAP_LOG_HI);

	addr |= gpu_read(gpu, REG_A5XX_UCHE_TRAP_LOG_LO);

	dev_err_ratelimited(gpu->dev->dev, "UCHE | Out of bounds access | addr=0x%llX\n",
		addr);
}

static void a5xx_gpmu_err_irq(struct msm_gpu *gpu)
{
	dev_err_ratelimited(gpu->dev->dev, "GPMU | voltage droop\n");
}

#define RBBM_ERROR_MASK \
	(A5XX_RBBM_INT_0_MASK_RBBM_AHB_ERROR | \
	A5XX_RBBM_INT_0_MASK_RBBM_TRANSFER_TIMEOUT | \
	A5XX_RBBM_INT_0_MASK_RBBM_ME_MS_TIMEOUT | \
	A5XX_RBBM_INT_0_MASK_RBBM_PFP_MS_TIMEOUT | \
	A5XX_RBBM_INT_0_MASK_RBBM_ETS_MS_TIMEOUT | \
	A5XX_RBBM_INT_0_MASK_RBBM_ATB_ASYNC_OVERFLOW)

static irqreturn_t a5xx_irq(struct msm_gpu *gpu)
{
	u32 status = gpu_read(gpu, REG_A5XX_RBBM_INT_0_STATUS);

	/*
	 * Clear all the interrupts except RBBM_AHB_ERROR - if we clear it
	 * before the source is cleared the interrupt will storm.
	 */
	gpu_write(gpu, REG_A5XX_RBBM_INT_CLEAR_CMD,
		status & ~A5XX_RBBM_INT_0_MASK_RBBM_AHB_ERROR);

	/* Pass status to a5xx_rbbm_err_irq because we've already cleared it */
	if (status & RBBM_ERROR_MASK)
		a5xx_rbbm_err_irq(gpu, status);

	if (status & A5XX_RBBM_INT_0_MASK_CP_HW_ERROR)
		a5xx_cp_err_irq(gpu);

	if (status & A5XX_RBBM_INT_0_MASK_UCHE_OOB_ACCESS)
		a5xx_uche_err_irq(gpu);

	if (status & A5XX_RBBM_INT_0_MASK_GPMU_VOLTAGE_DROOP)
		a5xx_gpmu_err_irq(gpu);

	if (status & A5XX_RBBM_INT_0_MASK_CP_CACHE_FLUSH_TS)
		msm_gpu_retire(gpu);

	return IRQ_HANDLED;
}

static const u32 a5xx_register_offsets[REG_ADRENO_REGISTER_MAX] = {
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_BASE, REG_A5XX_CP_RB_BASE),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_BASE_HI, REG_A5XX_CP_RB_BASE_HI),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_RPTR_ADDR, REG_A5XX_CP_RB_RPTR_ADDR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_RPTR_ADDR_HI,
		REG_A5XX_CP_RB_RPTR_ADDR_HI),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_RPTR, REG_A5XX_CP_RB_RPTR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_WPTR, REG_A5XX_CP_RB_WPTR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_CNTL, REG_A5XX_CP_RB_CNTL),
};

static const u32 a5xx_registers[] = {
	0x0000, 0x0002, 0x0004, 0x0020, 0x0022, 0x0026, 0x0029, 0x002B,
	0x002E, 0x0035, 0x0038, 0x0042, 0x0044, 0x0044, 0x0047, 0x0095,
	0x0097, 0x00BB, 0x03A0, 0x0464, 0x0469, 0x046F, 0x04D2, 0x04D3,
	0x04E0, 0x0533, 0x0540, 0x0555, 0x0800, 0x081A, 0x081F, 0x0841,
	0x0860, 0x0860, 0x0880, 0x08A0, 0x0B00, 0x0B12, 0x0B15, 0x0B28,
	0x0B78, 0x0B7F, 0x0BB0, 0x0BBD, 0x0BC0, 0x0BC6, 0x0BD0, 0x0C53,
	0x0C60, 0x0C61, 0x0C80, 0x0C82, 0x0C84, 0x0C85, 0x0C90, 0x0C98,
	0x0CA0, 0x0CA0, 0x0CB0, 0x0CB2, 0x2180, 0x2185, 0x2580, 0x2585,
	0x0CC1, 0x0CC1, 0x0CC4, 0x0CC7, 0x0CCC, 0x0CCC, 0x0CD0, 0x0CD8,
	0x0CE0, 0x0CE5, 0x0CE8, 0x0CE8, 0x0CEC, 0x0CF1, 0x0CFB, 0x0D0E,
	0x2100, 0x211E, 0x2140, 0x2145, 0x2500, 0x251E, 0x2540, 0x2545,
	0x0D10, 0x0D17, 0x0D20, 0x0D23, 0x0D30, 0x0D30, 0x20C0, 0x20C0,
	0x24C0, 0x24C0, 0x0E40, 0x0E43, 0x0E4A, 0x0E4A, 0x0E50, 0x0E57,
	0x0E60, 0x0E7C, 0x0E80, 0x0E8E, 0x0E90, 0x0E96, 0x0EA0, 0x0EA8,
	0x0EB0, 0x0EB2, 0xE140, 0xE147, 0xE150, 0xE187, 0xE1A0, 0xE1A9,
	0xE1B0, 0xE1B6, 0xE1C0, 0xE1C7, 0xE1D0, 0xE1D1, 0xE200, 0xE201,
	0xE210, 0xE21C, 0xE240, 0xE268, 0xE000, 0xE006, 0xE010, 0xE09A,
	0xE0A0, 0xE0A4, 0xE0AA, 0xE0EB, 0xE100, 0xE105, 0xE380, 0xE38F,
	0xE3B0, 0xE3B0, 0xE400, 0xE405, 0xE408, 0xE4E9, 0xE4F0, 0xE4F0,
	0xE280, 0xE280, 0xE282, 0xE2A3, 0xE2A5, 0xE2C2, 0xE940, 0xE947,
	0xE950, 0xE987, 0xE9A0, 0xE9A9, 0xE9B0, 0xE9B6, 0xE9C0, 0xE9C7,
	0xE9D0, 0xE9D1, 0xEA00, 0xEA01, 0xEA10, 0xEA1C, 0xEA40, 0xEA68,
	0xE800, 0xE806, 0xE810, 0xE89A, 0xE8A0, 0xE8A4, 0xE8AA, 0xE8EB,
	0xE900, 0xE905, 0xEB80, 0xEB8F, 0xEBB0, 0xEBB0, 0xEC00, 0xEC05,
	0xEC08, 0xECE9, 0xECF0, 0xECF0, 0xEA80, 0xEA80, 0xEA82, 0xEAA3,
	0xEAA5, 0xEAC2, 0xA800, 0xA8FF, 0xAC60, 0xAC60, 0xB000, 0xB97F,
	0xB9A0, 0xB9BF, ~0
};

static void a5xx_dump(struct msm_gpu *gpu)
{
	dev_info(gpu->dev->dev, "status:   %08x\n",
		gpu_read(gpu, REG_A5XX_RBBM_STATUS));
	adreno_dump(gpu);
}

static int a5xx_pm_resume(struct msm_gpu *gpu)
{
	int ret;

	/* Turn on the core power */
	ret = msm_gpu_pm_resume(gpu);
	if (ret)
		return ret;

	/* Turn the RBCCU domain first to limit the chances of voltage droop */
	gpu_write(gpu, REG_A5XX_GPMU_RBCCU_POWER_CNTL, 0x778000);

	/* Wait 3 usecs before polling */
	udelay(3);

	ret = spin_usecs(gpu, 20, REG_A5XX_GPMU_RBCCU_PWR_CLK_STATUS,
		(1 << 20), (1 << 20));
	if (ret) {
		DRM_ERROR("%s: timeout waiting for RBCCU GDSC enable: %X\n",
			gpu->name,
			gpu_read(gpu, REG_A5XX_GPMU_RBCCU_PWR_CLK_STATUS));
		return ret;
	}

	/* Turn on the SP domain */
	gpu_write(gpu, REG_A5XX_GPMU_SP_POWER_CNTL, 0x778000);
	ret = spin_usecs(gpu, 20, REG_A5XX_GPMU_SP_PWR_CLK_STATUS,
		(1 << 20), (1 << 20));
	if (ret)
		DRM_ERROR("%s: timeout waiting for SP GDSC enable\n",
			gpu->name);

	return ret;
}

static int a5xx_pm_suspend(struct msm_gpu *gpu)
{
	/* Clear the VBIF pipe before shutting down */
	gpu_write(gpu, REG_A5XX_VBIF_XIN_HALT_CTRL0, 0xF);
	spin_until((gpu_read(gpu, REG_A5XX_VBIF_XIN_HALT_CTRL1) & 0xF) == 0xF);

	gpu_write(gpu, REG_A5XX_VBIF_XIN_HALT_CTRL0, 0);

	/*
	 * Reset the VBIF before power collapse to avoid issue with FIFO
	 * entries
	 */
	gpu_write(gpu, REG_A5XX_RBBM_BLOCK_SW_RESET_CMD, 0x003C0000);
	gpu_write(gpu, REG_A5XX_RBBM_BLOCK_SW_RESET_CMD, 0x00000000);

	return msm_gpu_pm_suspend(gpu);
}

static int a5xx_get_timestamp(struct msm_gpu *gpu, uint64_t *value)
{
	*value = gpu_read64(gpu, REG_A5XX_RBBM_PERFCTR_CP_0_LO,
		REG_A5XX_RBBM_PERFCTR_CP_0_HI);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void a5xx_show(struct msm_gpu *gpu, struct seq_file *m)
{
	seq_printf(m, "status:   %08x\n",
			gpu_read(gpu, REG_A5XX_RBBM_STATUS));

	/*
	 * Temporarily disable hardware clock gating before going into
	 * adreno_show to avoid issues while reading the registers
	 */
	a5xx_set_hwcg(gpu, false);
	adreno_show(gpu, m);
	a5xx_set_hwcg(gpu, true);
}
#endif

static const struct adreno_gpu_funcs funcs = {
	.base = {
		.get_param = adreno_get_param,
		.hw_init = a5xx_hw_init,
		.pm_suspend = a5xx_pm_suspend,
		.pm_resume = a5xx_pm_resume,
		.recover = a5xx_recover,
		.last_fence = adreno_last_fence,
		.submit = a5xx_submit,
		.flush = adreno_flush,
		.irq = a5xx_irq,
		.destroy = a5xx_destroy,
#ifdef CONFIG_DEBUG_FS
		.show = a5xx_show,
#endif
	},
	.get_timestamp = a5xx_get_timestamp,
};

struct msm_gpu *a5xx_gpu_init(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = priv->gpu_pdev;
	struct a5xx_gpu *a5xx_gpu = NULL;
	struct adreno_gpu *adreno_gpu;
	struct msm_gpu *gpu;
	int ret;

	if (!pdev) {
		dev_err(dev->dev, "No A5XX device is defined\n");
		return ERR_PTR(-ENXIO);
	}

	a5xx_gpu = kzalloc(sizeof(*a5xx_gpu), GFP_KERNEL);
	if (!a5xx_gpu)
		return ERR_PTR(-ENOMEM);

	adreno_gpu = &a5xx_gpu->base;
	gpu = &adreno_gpu->base;

	a5xx_gpu->pdev = pdev;
	adreno_gpu->registers = a5xx_registers;
	adreno_gpu->reg_offsets = a5xx_register_offsets;

	a5xx_gpu->lm_leakage = 0x4E001A;

	ret = adreno_gpu_init(dev, pdev, adreno_gpu, &funcs);
	if (ret) {
		a5xx_destroy(&(a5xx_gpu->base.base));
		return ERR_PTR(ret);
	}

	if (gpu->aspace)
		msm_mmu_set_fault_handler(gpu->aspace->mmu, gpu, a5xx_fault_handler);

	return gpu;
}
