// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017-2019 The Linux Foundation. All rights reserved. */


#include "msm_gem.h"
#include "msm_mmu.h"
#include "msm_gpu_trace.h"
#include "a6xx_gpu.h"
#include "a6xx_gmu.xml.h"

#include <linux/devfreq.h>

#define GPU_PAS_ID 13

static inline bool _a6xx_check_idle(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	/* Check that the GMU is idle */
	if (!a6xx_gmu_isidle(&a6xx_gpu->gmu))
		return false;

	/* Check tha the CX master is idle */
	if (gpu_read(gpu, REG_A6XX_RBBM_STATUS) &
			~A6XX_RBBM_STATUS_CP_AHB_BUSY_CX_MASTER)
		return false;

	return !(gpu_read(gpu, REG_A6XX_RBBM_INT_0_STATUS) &
		A6XX_RBBM_INT_0_MASK_RBBM_HANG_DETECT);
}

bool a6xx_idle(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	/* wait for CP to drain ringbuffer: */
	if (!adreno_idle(gpu, ring))
		return false;

	if (spin_until(_a6xx_check_idle(gpu))) {
		DRM_ERROR("%s: %ps: timeout waiting for GPU to idle: status %8.8X irq %8.8X rptr/wptr %d/%d\n",
			gpu->name, __builtin_return_address(0),
			gpu_read(gpu, REG_A6XX_RBBM_STATUS),
			gpu_read(gpu, REG_A6XX_RBBM_INT_0_STATUS),
			gpu_read(gpu, REG_A6XX_CP_RB_RPTR),
			gpu_read(gpu, REG_A6XX_CP_RB_WPTR));
		return false;
	}

	return true;
}

static void a6xx_flush(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	uint32_t wptr;
	unsigned long flags;

	spin_lock_irqsave(&ring->lock, flags);

	/* Copy the shadow to the actual register */
	ring->cur = ring->next;

	/* Make sure to wrap wptr if we need to */
	wptr = get_wptr(ring);

	spin_unlock_irqrestore(&ring->lock, flags);

	/* Make sure everything is posted before making a decision */
	mb();

	gpu_write(gpu, REG_A6XX_CP_RB_WPTR, wptr);
}

static void get_stats_counter(struct msm_ringbuffer *ring, u32 counter,
		u64 iova)
{
	OUT_PKT7(ring, CP_REG_TO_MEM, 3);
	OUT_RING(ring, counter | (1 << 30) | (2 << 18));
	OUT_RING(ring, lower_32_bits(iova));
	OUT_RING(ring, upper_32_bits(iova));
}

static void a6xx_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit,
	struct msm_file_private *ctx)
{
	unsigned int index = submit->seqno % MSM_GPU_SUBMIT_STATS_COUNT;
	struct msm_drm_private *priv = gpu->dev->dev_private;
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = submit->ring;
	unsigned int i;

	get_stats_counter(ring, REG_A6XX_RBBM_PERFCTR_CP_0_LO,
		rbmemptr_stats(ring, index, cpcycles_start));

	/*
	 * For PM4 the GMU register offsets are calculated from the base of the
	 * GPU registers so we need to add 0x1a800 to the register value on A630
	 * to get the right value from PM4.
	 */
	get_stats_counter(ring, REG_A6XX_GMU_ALWAYS_ON_COUNTER_L + 0x1a800,
		rbmemptr_stats(ring, index, alwayson_start));

	/* Invalidate CCU depth and color */
	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, PC_CCU_INVALIDATE_DEPTH);

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, PC_CCU_INVALIDATE_COLOR);

	/* Submit the commands */
	for (i = 0; i < submit->nr_cmds; i++) {
		switch (submit->cmd[i].type) {
		case MSM_SUBMIT_CMD_IB_TARGET_BUF:
			break;
		case MSM_SUBMIT_CMD_CTX_RESTORE_BUF:
			if (priv->lastctx == ctx)
				break;
			/* fall-thru */
		case MSM_SUBMIT_CMD_BUF:
			OUT_PKT7(ring, CP_INDIRECT_BUFFER_PFE, 3);
			OUT_RING(ring, lower_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, upper_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, submit->cmd[i].size);
			break;
		}
	}

	get_stats_counter(ring, REG_A6XX_RBBM_PERFCTR_CP_0_LO,
		rbmemptr_stats(ring, index, cpcycles_end));
	get_stats_counter(ring, REG_A6XX_GMU_ALWAYS_ON_COUNTER_L + 0x1a800,
		rbmemptr_stats(ring, index, alwayson_end));

	/* Write the fence to the scratch register */
	OUT_PKT4(ring, REG_A6XX_CP_SCRATCH_REG(2), 1);
	OUT_RING(ring, submit->seqno);

	/*
	 * Execute a CACHE_FLUSH_TS event. This will ensure that the
	 * timestamp is written to the memory and then triggers the interrupt
	 */
	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, CACHE_FLUSH_TS | (1 << 31));
	OUT_RING(ring, lower_32_bits(rbmemptr(ring, fence)));
	OUT_RING(ring, upper_32_bits(rbmemptr(ring, fence)));
	OUT_RING(ring, submit->seqno);

	trace_msm_gpu_submit_flush(submit,
		gmu_read64(&a6xx_gpu->gmu, REG_A6XX_GMU_ALWAYS_ON_COUNTER_L,
			REG_A6XX_GMU_ALWAYS_ON_COUNTER_H));

	a6xx_flush(gpu, ring);
}

static const struct {
	u32 offset;
	u32 value;
} a6xx_hwcg[] = {
	{REG_A6XX_RBBM_CLOCK_CNTL_SP0, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL_SP1, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL_SP2, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL_SP3, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_SP0, 0x02022220},
	{REG_A6XX_RBBM_CLOCK_CNTL2_SP1, 0x02022220},
	{REG_A6XX_RBBM_CLOCK_CNTL2_SP2, 0x02022220},
	{REG_A6XX_RBBM_CLOCK_CNTL2_SP3, 0x02022220},
	{REG_A6XX_RBBM_CLOCK_DELAY_SP0, 0x00000080},
	{REG_A6XX_RBBM_CLOCK_DELAY_SP1, 0x00000080},
	{REG_A6XX_RBBM_CLOCK_DELAY_SP2, 0x00000080},
	{REG_A6XX_RBBM_CLOCK_DELAY_SP3, 0x00000080},
	{REG_A6XX_RBBM_CLOCK_HYST_SP0, 0x0000f3cf},
	{REG_A6XX_RBBM_CLOCK_HYST_SP1, 0x0000f3cf},
	{REG_A6XX_RBBM_CLOCK_HYST_SP2, 0x0000f3cf},
	{REG_A6XX_RBBM_CLOCK_HYST_SP3, 0x0000f3cf},
	{REG_A6XX_RBBM_CLOCK_CNTL_TP0, 0x02222222},
	{REG_A6XX_RBBM_CLOCK_CNTL_TP1, 0x02222222},
	{REG_A6XX_RBBM_CLOCK_CNTL_TP2, 0x02222222},
	{REG_A6XX_RBBM_CLOCK_CNTL_TP3, 0x02222222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_TP0, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_TP1, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_TP2, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_TP3, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL3_TP0, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL3_TP1, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL3_TP2, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL3_TP3, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL4_TP0, 0x00022222},
	{REG_A6XX_RBBM_CLOCK_CNTL4_TP1, 0x00022222},
	{REG_A6XX_RBBM_CLOCK_CNTL4_TP2, 0x00022222},
	{REG_A6XX_RBBM_CLOCK_CNTL4_TP3, 0x00022222},
	{REG_A6XX_RBBM_CLOCK_HYST_TP0, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST_TP1, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST_TP2, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST_TP3, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST2_TP0, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST2_TP1, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST2_TP2, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST2_TP3, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST3_TP0, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST3_TP1, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST3_TP2, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST3_TP3, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST4_TP0, 0x00077777},
	{REG_A6XX_RBBM_CLOCK_HYST4_TP1, 0x00077777},
	{REG_A6XX_RBBM_CLOCK_HYST4_TP2, 0x00077777},
	{REG_A6XX_RBBM_CLOCK_HYST4_TP3, 0x00077777},
	{REG_A6XX_RBBM_CLOCK_DELAY_TP0, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY_TP1, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY_TP2, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY_TP3, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY2_TP0, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY2_TP1, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY2_TP2, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY2_TP3, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY3_TP0, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY3_TP1, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY3_TP2, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY3_TP3, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY4_TP0, 0x00011111},
	{REG_A6XX_RBBM_CLOCK_DELAY4_TP1, 0x00011111},
	{REG_A6XX_RBBM_CLOCK_DELAY4_TP2, 0x00011111},
	{REG_A6XX_RBBM_CLOCK_DELAY4_TP3, 0x00011111},
	{REG_A6XX_RBBM_CLOCK_CNTL_UCHE, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_UCHE, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL3_UCHE, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL4_UCHE, 0x00222222},
	{REG_A6XX_RBBM_CLOCK_HYST_UCHE, 0x00000004},
	{REG_A6XX_RBBM_CLOCK_DELAY_UCHE, 0x00000002},
	{REG_A6XX_RBBM_CLOCK_CNTL_RB0, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL_RB1, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL_RB2, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL_RB3, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_RB0, 0x00002222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_RB1, 0x00002222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_RB2, 0x00002222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_RB3, 0x00002222},
	{REG_A6XX_RBBM_CLOCK_CNTL_CCU0, 0x00002220},
	{REG_A6XX_RBBM_CLOCK_CNTL_CCU1, 0x00002220},
	{REG_A6XX_RBBM_CLOCK_CNTL_CCU2, 0x00002220},
	{REG_A6XX_RBBM_CLOCK_CNTL_CCU3, 0x00002220},
	{REG_A6XX_RBBM_CLOCK_HYST_RB_CCU0, 0x00040f00},
	{REG_A6XX_RBBM_CLOCK_HYST_RB_CCU1, 0x00040f00},
	{REG_A6XX_RBBM_CLOCK_HYST_RB_CCU2, 0x00040f00},
	{REG_A6XX_RBBM_CLOCK_HYST_RB_CCU3, 0x00040f00},
	{REG_A6XX_RBBM_CLOCK_CNTL_RAC, 0x05022022},
	{REG_A6XX_RBBM_CLOCK_CNTL2_RAC, 0x00005555},
	{REG_A6XX_RBBM_CLOCK_DELAY_RAC, 0x00000011},
	{REG_A6XX_RBBM_CLOCK_HYST_RAC, 0x00445044},
	{REG_A6XX_RBBM_CLOCK_CNTL_TSE_RAS_RBBM, 0x04222222},
	{REG_A6XX_RBBM_CLOCK_MODE_GPC, 0x00222222},
	{REG_A6XX_RBBM_CLOCK_MODE_VFD, 0x00002222},
	{REG_A6XX_RBBM_CLOCK_HYST_TSE_RAS_RBBM, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_HYST_GPC, 0x04104004},
	{REG_A6XX_RBBM_CLOCK_HYST_VFD, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_DELAY_HLSQ, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_DELAY_TSE_RAS_RBBM, 0x00004000},
	{REG_A6XX_RBBM_CLOCK_DELAY_GPC, 0x00000200},
	{REG_A6XX_RBBM_CLOCK_DELAY_VFD, 0x00002222},
	{REG_A6XX_RBBM_CLOCK_DELAY_HLSQ_2, 0x00000002},
	{REG_A6XX_RBBM_CLOCK_MODE_HLSQ, 0x00002222},
	{REG_A6XX_RBBM_CLOCK_CNTL_GMU_GX, 0x00000222},
	{REG_A6XX_RBBM_CLOCK_DELAY_GMU_GX, 0x00000111},
	{REG_A6XX_RBBM_CLOCK_HYST_GMU_GX, 0x00000555}
};

static void a6xx_set_hwcg(struct msm_gpu *gpu, bool state)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	unsigned int i;
	u32 val;

	val = gpu_read(gpu, REG_A6XX_RBBM_CLOCK_CNTL);

	/* Don't re-program the registers if they are already correct */
	if ((!state && !val) || (state && (val == 0x8aa8aa02)))
		return;

	/* Disable SP clock before programming HWCG registers */
	gmu_rmw(gmu, REG_A6XX_GPU_GMU_GX_SPTPRAC_CLOCK_CONTROL, 1, 0);

	for (i = 0; i < ARRAY_SIZE(a6xx_hwcg); i++)
		gpu_write(gpu, a6xx_hwcg[i].offset,
			state ? a6xx_hwcg[i].value : 0);

	/* Enable SP clock */
	gmu_rmw(gmu, REG_A6XX_GPU_GMU_GX_SPTPRAC_CLOCK_CONTROL, 0, 1);

	gpu_write(gpu, REG_A6XX_RBBM_CLOCK_CNTL, state ? 0x8aa8aa02 : 0);
}

static int a6xx_cp_init(struct msm_gpu *gpu)
{
	struct msm_ringbuffer *ring = gpu->rb[0];

	OUT_PKT7(ring, CP_ME_INIT, 8);

	OUT_RING(ring, 0x0000002f);

	/* Enable multiple hardware contexts */
	OUT_RING(ring, 0x00000003);

	/* Enable error detection */
	OUT_RING(ring, 0x20000000);

	/* Don't enable header dump */
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);

	/* No workarounds enabled */
	OUT_RING(ring, 0x00000000);

	/* Pad rest of the cmds with 0's */
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);

	a6xx_flush(gpu, ring);
	return a6xx_idle(gpu, ring) ? 0 : -EINVAL;
}

static int a6xx_ucode_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	if (!a6xx_gpu->sqe_bo) {
		a6xx_gpu->sqe_bo = adreno_fw_create_bo(gpu,
			adreno_gpu->fw[ADRENO_FW_SQE], &a6xx_gpu->sqe_iova);

		if (IS_ERR(a6xx_gpu->sqe_bo)) {
			int ret = PTR_ERR(a6xx_gpu->sqe_bo);

			a6xx_gpu->sqe_bo = NULL;
			DRM_DEV_ERROR(&gpu->pdev->dev,
				"Could not allocate SQE ucode: %d\n", ret);

			return ret;
		}

		msm_gem_object_set_name(a6xx_gpu->sqe_bo, "sqefw");
	}

	gpu_write64(gpu, REG_A6XX_CP_SQE_INSTR_BASE_LO,
		REG_A6XX_CP_SQE_INSTR_BASE_HI, a6xx_gpu->sqe_iova);

	return 0;
}

static int a6xx_zap_shader_init(struct msm_gpu *gpu)
{
	static bool loaded;
	int ret;

	if (loaded)
		return 0;

	ret = adreno_zap_shader_load(gpu, GPU_PAS_ID);

	loaded = !ret;
	return ret;
}

#define A6XX_INT_MASK (A6XX_RBBM_INT_0_MASK_CP_AHB_ERROR | \
	  A6XX_RBBM_INT_0_MASK_RBBM_ATB_ASYNCFIFO_OVERFLOW | \
	  A6XX_RBBM_INT_0_MASK_CP_HW_ERROR | \
	  A6XX_RBBM_INT_0_MASK_CP_IB2 | \
	  A6XX_RBBM_INT_0_MASK_CP_IB1 | \
	  A6XX_RBBM_INT_0_MASK_CP_RB | \
	  A6XX_RBBM_INT_0_MASK_CP_CACHE_FLUSH_TS | \
	  A6XX_RBBM_INT_0_MASK_RBBM_ATB_BUS_OVERFLOW | \
	  A6XX_RBBM_INT_0_MASK_RBBM_HANG_DETECT | \
	  A6XX_RBBM_INT_0_MASK_UCHE_OOB_ACCESS | \
	  A6XX_RBBM_INT_0_MASK_UCHE_TRAP_INTR)

static int a6xx_hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	int ret;

	/* Make sure the GMU keeps the GPU on while we set it up */
	a6xx_gmu_set_oob(&a6xx_gpu->gmu, GMU_OOB_GPU_SET);

	gpu_write(gpu, REG_A6XX_RBBM_SECVID_TSB_CNTL, 0);

	/*
	 * Disable the trusted memory range - we don't actually supported secure
	 * memory rendering at this point in time and we don't want to block off
	 * part of the virtual memory space.
	 */
	gpu_write64(gpu, REG_A6XX_RBBM_SECVID_TSB_TRUSTED_BASE_LO,
		REG_A6XX_RBBM_SECVID_TSB_TRUSTED_BASE_HI, 0x00000000);
	gpu_write(gpu, REG_A6XX_RBBM_SECVID_TSB_TRUSTED_SIZE, 0x00000000);

	/* Turn on 64 bit addressing for all blocks */
	gpu_write(gpu, REG_A6XX_CP_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A6XX_VSC_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A6XX_GRAS_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A6XX_RB_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A6XX_PC_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A6XX_HLSQ_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A6XX_VFD_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A6XX_VPC_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A6XX_UCHE_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A6XX_SP_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A6XX_TPL1_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A6XX_RBBM_SECVID_TSB_ADDR_MODE_CNTL, 0x1);

	/*
	 * enable hardware clockgating
	 * For now enable clock gating only for a630
	 */
	if (adreno_is_a630(adreno_gpu))
		a6xx_set_hwcg(gpu, true);

	/* VBIF/GBIF start*/
	gpu_write(gpu, REG_A6XX_RBBM_VBIF_CLIENT_QOS_CNTL, 0x3);
	if (adreno_is_a630(adreno_gpu))
		gpu_write(gpu, REG_A6XX_VBIF_GATE_OFF_WRREQ_EN, 0x00000009);

	/* Make all blocks contribute to the GPU BUSY perf counter */
	gpu_write(gpu, REG_A6XX_RBBM_PERFCTR_GPU_BUSY_MASKED, 0xffffffff);

	/* Disable L2 bypass in the UCHE */
	gpu_write(gpu, REG_A6XX_UCHE_WRITE_RANGE_MAX_LO, 0xffffffc0);
	gpu_write(gpu, REG_A6XX_UCHE_WRITE_RANGE_MAX_HI, 0x0001ffff);
	gpu_write(gpu, REG_A6XX_UCHE_TRAP_BASE_LO, 0xfffff000);
	gpu_write(gpu, REG_A6XX_UCHE_TRAP_BASE_HI, 0x0001ffff);
	gpu_write(gpu, REG_A6XX_UCHE_WRITE_THRU_BASE_LO, 0xfffff000);
	gpu_write(gpu, REG_A6XX_UCHE_WRITE_THRU_BASE_HI, 0x0001ffff);

	/* Set the GMEM VA range [0x100000:0x100000 + gpu->gmem - 1] */
	gpu_write64(gpu, REG_A6XX_UCHE_GMEM_RANGE_MIN_LO,
		REG_A6XX_UCHE_GMEM_RANGE_MIN_HI, 0x00100000);

	gpu_write64(gpu, REG_A6XX_UCHE_GMEM_RANGE_MAX_LO,
		REG_A6XX_UCHE_GMEM_RANGE_MAX_HI,
		0x00100000 + adreno_gpu->gmem - 1);

	gpu_write(gpu, REG_A6XX_UCHE_FILTER_CNTL, 0x804);
	gpu_write(gpu, REG_A6XX_UCHE_CACHE_WAYS, 0x4);

	gpu_write(gpu, REG_A6XX_CP_ROQ_THRESHOLDS_2, 0x010000c0);
	gpu_write(gpu, REG_A6XX_CP_ROQ_THRESHOLDS_1, 0x8040362c);

	/* Setting the mem pool size */
	gpu_write(gpu, REG_A6XX_CP_MEM_POOL_SIZE, 128);

	/* Setting the primFifo thresholds default values */
	gpu_write(gpu, REG_A6XX_PC_DBG_ECO_CNTL, (0x300 << 11));

	/* Set the AHB default slave response to "ERROR" */
	gpu_write(gpu, REG_A6XX_CP_AHB_CNTL, 0x1);

	/* Turn on performance counters */
	gpu_write(gpu, REG_A6XX_RBBM_PERFCTR_CNTL, 0x1);

	/* Select CP0 to always count cycles */
	gpu_write(gpu, REG_A6XX_CP_PERFCTR_CP_SEL_0, PERF_CP_ALWAYS_COUNT);

	if (adreno_is_a630(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_RB_NC_MODE_CNTL, 2 << 1);
		gpu_write(gpu, REG_A6XX_TPL1_NC_MODE_CNTL, 2 << 1);
		gpu_write(gpu, REG_A6XX_SP_NC_MODE_CNTL, 2 << 1);
		gpu_write(gpu, REG_A6XX_UCHE_MODE_CNTL, 2 << 21);
	}

	/* Enable fault detection */
	gpu_write(gpu, REG_A6XX_RBBM_INTERFACE_HANG_INT_CNTL,
		(1 << 30) | 0x1fffff);

	gpu_write(gpu, REG_A6XX_UCHE_CLIENT_PF, 1);

	/* Protect registers from the CP */
	gpu_write(gpu, REG_A6XX_CP_PROTECT_CNTL, 0x00000003);

	gpu_write(gpu, REG_A6XX_CP_PROTECT(0),
		A6XX_PROTECT_RDONLY(0x600, 0x51));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(1), A6XX_PROTECT_RW(0xae50, 0x2));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(2), A6XX_PROTECT_RW(0x9624, 0x13));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(3), A6XX_PROTECT_RW(0x8630, 0x8));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(4), A6XX_PROTECT_RW(0x9e70, 0x1));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(5), A6XX_PROTECT_RW(0x9e78, 0x187));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(6), A6XX_PROTECT_RW(0xf000, 0x810));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(7),
		A6XX_PROTECT_RDONLY(0xfc00, 0x3));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(8), A6XX_PROTECT_RW(0x50e, 0x0));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(9), A6XX_PROTECT_RDONLY(0x50f, 0x0));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(10), A6XX_PROTECT_RW(0x510, 0x0));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(11),
		A6XX_PROTECT_RDONLY(0x0, 0x4f9));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(12),
		A6XX_PROTECT_RDONLY(0x501, 0xa));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(13),
		A6XX_PROTECT_RDONLY(0x511, 0x44));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(14), A6XX_PROTECT_RW(0xe00, 0xe));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(15), A6XX_PROTECT_RW(0x8e00, 0x0));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(16), A6XX_PROTECT_RW(0x8e50, 0xf));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(17), A6XX_PROTECT_RW(0xbe02, 0x0));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(18),
		A6XX_PROTECT_RW(0xbe20, 0x11f3));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(19), A6XX_PROTECT_RW(0x800, 0x82));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(20), A6XX_PROTECT_RW(0x8a0, 0x8));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(21), A6XX_PROTECT_RW(0x8ab, 0x19));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(22), A6XX_PROTECT_RW(0x900, 0x4d));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(23), A6XX_PROTECT_RW(0x98d, 0x76));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(24),
			A6XX_PROTECT_RDONLY(0x980, 0x4));
	gpu_write(gpu, REG_A6XX_CP_PROTECT(25), A6XX_PROTECT_RW(0xa630, 0x0));

	/* Enable interrupts */
	gpu_write(gpu, REG_A6XX_RBBM_INT_0_MASK, A6XX_INT_MASK);

	ret = adreno_hw_init(gpu);
	if (ret)
		goto out;

	ret = a6xx_ucode_init(gpu);
	if (ret)
		goto out;

	/* Always come up on rb 0 */
	a6xx_gpu->cur_ring = gpu->rb[0];

	/* Enable the SQE_to start the CP engine */
	gpu_write(gpu, REG_A6XX_CP_SQE_CNTL, 1);

	ret = a6xx_cp_init(gpu);
	if (ret)
		goto out;

	/*
	 * Try to load a zap shader into the secure world. If successful
	 * we can use the CP to switch out of secure mode. If not then we
	 * have no resource but to try to switch ourselves out manually. If we
	 * guessed wrong then access to the RBBM_SECVID_TRUST_CNTL register will
	 * be blocked and a permissions violation will soon follow.
	 */
	ret = a6xx_zap_shader_init(gpu);
	if (!ret) {
		OUT_PKT7(gpu->rb[0], CP_SET_SECURE_MODE, 1);
		OUT_RING(gpu->rb[0], 0x00000000);

		a6xx_flush(gpu, gpu->rb[0]);
		if (!a6xx_idle(gpu, gpu->rb[0]))
			return -EINVAL;
	} else if (ret == -ENODEV) {
		/*
		 * This device does not use zap shader (but print a warning
		 * just in case someone got their dt wrong.. hopefully they
		 * have a debug UART to realize the error of their ways...
		 * if you mess this up you are about to crash horribly)
		 */
		dev_warn_once(gpu->dev->dev,
			"Zap shader not enabled - using SECVID_TRUST_CNTL instead\n");
		gpu_write(gpu, REG_A6XX_RBBM_SECVID_TRUST_CNTL, 0x0);
		ret = 0;
	} else {
		return ret;
	}

out:
	/*
	 * Tell the GMU that we are done touching the GPU and it can start power
	 * management
	 */
	a6xx_gmu_clear_oob(&a6xx_gpu->gmu, GMU_OOB_GPU_SET);

	/* Take the GMU out of its special boot mode */
	a6xx_gmu_clear_oob(&a6xx_gpu->gmu, GMU_OOB_BOOT_SLUMBER);

	return ret;
}

static void a6xx_dump(struct msm_gpu *gpu)
{
	DRM_DEV_INFO(&gpu->pdev->dev, "status:   %08x\n",
			gpu_read(gpu, REG_A6XX_RBBM_STATUS));
	adreno_dump(gpu);
}

#define VBIF_RESET_ACK_TIMEOUT	100
#define VBIF_RESET_ACK_MASK	0x00f0

static void a6xx_recover(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	int i;

	adreno_dump_info(gpu);

	for (i = 0; i < 8; i++)
		DRM_DEV_INFO(&gpu->pdev->dev, "CP_SCRATCH_REG%d: %u\n", i,
			gpu_read(gpu, REG_A6XX_CP_SCRATCH_REG(i)));

	if (hang_debug)
		a6xx_dump(gpu);

	/*
	 * Turn off keep alive that might have been enabled by the hang
	 * interrupt
	 */
	gmu_write(&a6xx_gpu->gmu, REG_A6XX_GMU_GMU_PWR_COL_KEEPALIVE, 0);

	gpu->funcs->pm_suspend(gpu);
	gpu->funcs->pm_resume(gpu);

	msm_gpu_hw_init(gpu);
}

static int a6xx_fault_handler(void *arg, unsigned long iova, int flags)
{
	struct msm_gpu *gpu = arg;

	pr_warn_ratelimited("*** gpu fault: iova=%08lx, flags=%d (%u,%u,%u,%u)\n",
			iova, flags,
			gpu_read(gpu, REG_A6XX_CP_SCRATCH_REG(4)),
			gpu_read(gpu, REG_A6XX_CP_SCRATCH_REG(5)),
			gpu_read(gpu, REG_A6XX_CP_SCRATCH_REG(6)),
			gpu_read(gpu, REG_A6XX_CP_SCRATCH_REG(7)));

	return -EFAULT;
}

static void a6xx_cp_hw_err_irq(struct msm_gpu *gpu)
{
	u32 status = gpu_read(gpu, REG_A6XX_CP_INTERRUPT_STATUS);

	if (status & A6XX_CP_INT_CP_OPCODE_ERROR) {
		u32 val;

		gpu_write(gpu, REG_A6XX_CP_SQE_STAT_ADDR, 1);
		val = gpu_read(gpu, REG_A6XX_CP_SQE_STAT_DATA);
		dev_err_ratelimited(&gpu->pdev->dev,
			"CP | opcode error | possible opcode=0x%8.8X\n",
			val);
	}

	if (status & A6XX_CP_INT_CP_UCODE_ERROR)
		dev_err_ratelimited(&gpu->pdev->dev,
			"CP ucode error interrupt\n");

	if (status & A6XX_CP_INT_CP_HW_FAULT_ERROR)
		dev_err_ratelimited(&gpu->pdev->dev, "CP | HW fault | status=0x%8.8X\n",
			gpu_read(gpu, REG_A6XX_CP_HW_FAULT));

	if (status & A6XX_CP_INT_CP_REGISTER_PROTECTION_ERROR) {
		u32 val = gpu_read(gpu, REG_A6XX_CP_PROTECT_STATUS);

		dev_err_ratelimited(&gpu->pdev->dev,
			"CP | protected mode error | %s | addr=0x%8.8X | status=0x%8.8X\n",
			val & (1 << 20) ? "READ" : "WRITE",
			(val & 0x3ffff), val);
	}

	if (status & A6XX_CP_INT_CP_AHB_ERROR)
		dev_err_ratelimited(&gpu->pdev->dev, "CP AHB error interrupt\n");

	if (status & A6XX_CP_INT_CP_VSD_PARITY_ERROR)
		dev_err_ratelimited(&gpu->pdev->dev, "CP VSD decoder parity error\n");

	if (status & A6XX_CP_INT_CP_ILLEGAL_INSTR_ERROR)
		dev_err_ratelimited(&gpu->pdev->dev, "CP illegal instruction error\n");

}

static void a6xx_fault_detect_irq(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct drm_device *dev = gpu->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_ringbuffer *ring = gpu->funcs->active_ring(gpu);

	/*
	 * Force the GPU to stay on until after we finish
	 * collecting information
	 */
	gmu_write(&a6xx_gpu->gmu, REG_A6XX_GMU_GMU_PWR_COL_KEEPALIVE, 1);

	DRM_DEV_ERROR(&gpu->pdev->dev,
		"gpu fault ring %d fence %x status %8.8X rb %4.4x/%4.4x ib1 %16.16llX/%4.4x ib2 %16.16llX/%4.4x\n",
		ring ? ring->id : -1, ring ? ring->seqno : 0,
		gpu_read(gpu, REG_A6XX_RBBM_STATUS),
		gpu_read(gpu, REG_A6XX_CP_RB_RPTR),
		gpu_read(gpu, REG_A6XX_CP_RB_WPTR),
		gpu_read64(gpu, REG_A6XX_CP_IB1_BASE, REG_A6XX_CP_IB1_BASE_HI),
		gpu_read(gpu, REG_A6XX_CP_IB1_REM_SIZE),
		gpu_read64(gpu, REG_A6XX_CP_IB2_BASE, REG_A6XX_CP_IB2_BASE_HI),
		gpu_read(gpu, REG_A6XX_CP_IB2_REM_SIZE));

	/* Turn off the hangcheck timer to keep it from bothering us */
	del_timer(&gpu->hangcheck_timer);

	queue_work(priv->wq, &gpu->recover_work);
}

static irqreturn_t a6xx_irq(struct msm_gpu *gpu)
{
	u32 status = gpu_read(gpu, REG_A6XX_RBBM_INT_0_STATUS);

	gpu_write(gpu, REG_A6XX_RBBM_INT_CLEAR_CMD, status);

	if (status & A6XX_RBBM_INT_0_MASK_RBBM_HANG_DETECT)
		a6xx_fault_detect_irq(gpu);

	if (status & A6XX_RBBM_INT_0_MASK_CP_AHB_ERROR)
		dev_err_ratelimited(&gpu->pdev->dev, "CP | AHB bus error\n");

	if (status & A6XX_RBBM_INT_0_MASK_CP_HW_ERROR)
		a6xx_cp_hw_err_irq(gpu);

	if (status & A6XX_RBBM_INT_0_MASK_RBBM_ATB_ASYNCFIFO_OVERFLOW)
		dev_err_ratelimited(&gpu->pdev->dev, "RBBM | ATB ASYNC overflow\n");

	if (status & A6XX_RBBM_INT_0_MASK_RBBM_ATB_BUS_OVERFLOW)
		dev_err_ratelimited(&gpu->pdev->dev, "RBBM | ATB bus overflow\n");

	if (status & A6XX_RBBM_INT_0_MASK_UCHE_OOB_ACCESS)
		dev_err_ratelimited(&gpu->pdev->dev, "UCHE | Out of bounds access\n");

	if (status & A6XX_RBBM_INT_0_MASK_CP_CACHE_FLUSH_TS)
		msm_gpu_retire(gpu);

	return IRQ_HANDLED;
}

static const u32 a6xx_register_offsets[REG_ADRENO_REGISTER_MAX] = {
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_BASE, REG_A6XX_CP_RB_BASE),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_BASE_HI, REG_A6XX_CP_RB_BASE_HI),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_RPTR_ADDR,
		REG_A6XX_CP_RB_RPTR_ADDR_LO),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_RPTR_ADDR_HI,
		REG_A6XX_CP_RB_RPTR_ADDR_HI),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_RPTR, REG_A6XX_CP_RB_RPTR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_WPTR, REG_A6XX_CP_RB_WPTR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_CNTL, REG_A6XX_CP_RB_CNTL),
};

static int a6xx_pm_resume(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	int ret;

	gpu->needs_hw_init = true;

	ret = a6xx_gmu_resume(a6xx_gpu);
	if (ret)
		return ret;

	msm_gpu_resume_devfreq(gpu);

	return 0;
}

static int a6xx_pm_suspend(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	devfreq_suspend_device(gpu->devfreq.devfreq);

	return a6xx_gmu_stop(a6xx_gpu);
}

static int a6xx_get_timestamp(struct msm_gpu *gpu, uint64_t *value)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	/* Force the GPU power on so we can read this register */
	a6xx_gmu_set_oob(&a6xx_gpu->gmu, GMU_OOB_GPU_SET);

	*value = gpu_read64(gpu, REG_A6XX_RBBM_PERFCTR_CP_0_LO,
		REG_A6XX_RBBM_PERFCTR_CP_0_HI);

	a6xx_gmu_clear_oob(&a6xx_gpu->gmu, GMU_OOB_GPU_SET);
	return 0;
}

static struct msm_ringbuffer *a6xx_active_ring(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	return a6xx_gpu->cur_ring;
}

static void a6xx_destroy(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	if (a6xx_gpu->sqe_bo) {
		msm_gem_unpin_iova(a6xx_gpu->sqe_bo, gpu->aspace);
		drm_gem_object_put_unlocked(a6xx_gpu->sqe_bo);
	}

	a6xx_gmu_remove(a6xx_gpu);

	adreno_gpu_cleanup(adreno_gpu);
	kfree(a6xx_gpu);
}

static unsigned long a6xx_gpu_busy(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	u64 busy_cycles, busy_time;

	busy_cycles = gmu_read64(&a6xx_gpu->gmu,
			REG_A6XX_GMU_CX_GMU_POWER_COUNTER_XOCLK_0_L,
			REG_A6XX_GMU_CX_GMU_POWER_COUNTER_XOCLK_0_H);

	busy_time = (busy_cycles - gpu->devfreq.busy_cycles) * 10;
	do_div(busy_time, 192);

	gpu->devfreq.busy_cycles = busy_cycles;

	if (WARN_ON(busy_time > ~0LU))
		return ~0LU;

	return (unsigned long)busy_time;
}

static const struct adreno_gpu_funcs funcs = {
	.base = {
		.get_param = adreno_get_param,
		.hw_init = a6xx_hw_init,
		.pm_suspend = a6xx_pm_suspend,
		.pm_resume = a6xx_pm_resume,
		.recover = a6xx_recover,
		.submit = a6xx_submit,
		.flush = a6xx_flush,
		.active_ring = a6xx_active_ring,
		.irq = a6xx_irq,
		.destroy = a6xx_destroy,
#if defined(CONFIG_DRM_MSM_GPU_STATE)
		.show = a6xx_show,
#endif
		.gpu_busy = a6xx_gpu_busy,
		.gpu_get_freq = a6xx_gmu_get_freq,
		.gpu_set_freq = a6xx_gmu_set_freq,
#if defined(CONFIG_DRM_MSM_GPU_STATE)
		.gpu_state_get = a6xx_gpu_state_get,
		.gpu_state_put = a6xx_gpu_state_put,
#endif
	},
	.get_timestamp = a6xx_get_timestamp,
};

struct msm_gpu *a6xx_gpu_init(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = priv->gpu_pdev;
	struct device_node *node;
	struct a6xx_gpu *a6xx_gpu;
	struct adreno_gpu *adreno_gpu;
	struct msm_gpu *gpu;
	int ret;

	a6xx_gpu = kzalloc(sizeof(*a6xx_gpu), GFP_KERNEL);
	if (!a6xx_gpu)
		return ERR_PTR(-ENOMEM);

	adreno_gpu = &a6xx_gpu->base;
	gpu = &adreno_gpu->base;

	adreno_gpu->registers = NULL;
	adreno_gpu->reg_offsets = a6xx_register_offsets;

	ret = adreno_gpu_init(dev, pdev, adreno_gpu, &funcs, 1);
	if (ret) {
		a6xx_destroy(&(a6xx_gpu->base.base));
		return ERR_PTR(ret);
	}

	/* Check if there is a GMU phandle and set it up */
	node = of_parse_phandle(pdev->dev.of_node, "qcom,gmu", 0);

	/* FIXME: How do we gracefully handle this? */
	BUG_ON(!node);

	ret = a6xx_gmu_init(a6xx_gpu, node);
	if (ret) {
		a6xx_destroy(&(a6xx_gpu->base.base));
		return ERR_PTR(ret);
	}

	if (gpu->aspace)
		msm_mmu_set_fault_handler(gpu->aspace->mmu, gpu,
				a6xx_fault_handler);

	return gpu;
}
