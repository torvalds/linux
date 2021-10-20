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
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	uint32_t wptr;
	unsigned long flags;

	/* Expanded APRIV doesn't need to issue the WHERE_AM_I opcode */
	if (a6xx_gpu->has_whereami && !adreno_gpu->base.hw_apriv) {
		struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

		OUT_PKT7(ring, CP_WHERE_AM_I, 2);
		OUT_RING(ring, lower_32_bits(shadowptr(a6xx_gpu, ring)));
		OUT_RING(ring, upper_32_bits(shadowptr(a6xx_gpu, ring)));
	}

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
	OUT_RING(ring, CP_REG_TO_MEM_0_REG(counter) |
		CP_REG_TO_MEM_0_CNT(2) |
		CP_REG_TO_MEM_0_64B);
	OUT_RING(ring, lower_32_bits(iova));
	OUT_RING(ring, upper_32_bits(iova));
}

static void a6xx_set_pagetable(struct a6xx_gpu *a6xx_gpu,
		struct msm_ringbuffer *ring, struct msm_file_private *ctx)
{
	phys_addr_t ttbr;
	u32 asid;
	u64 memptr = rbmemptr(ring, ttbr0);

	if (ctx->seqno == a6xx_gpu->cur_ctx_seqno)
		return;

	if (msm_iommu_pagetable_params(ctx->aspace->mmu, &ttbr, &asid))
		return;

	/* Execute the table update */
	OUT_PKT7(ring, CP_SMMU_TABLE_UPDATE, 4);
	OUT_RING(ring, CP_SMMU_TABLE_UPDATE_0_TTBR0_LO(lower_32_bits(ttbr)));

	OUT_RING(ring,
		CP_SMMU_TABLE_UPDATE_1_TTBR0_HI(upper_32_bits(ttbr)) |
		CP_SMMU_TABLE_UPDATE_1_ASID(asid));
	OUT_RING(ring, CP_SMMU_TABLE_UPDATE_2_CONTEXTIDR(0));
	OUT_RING(ring, CP_SMMU_TABLE_UPDATE_3_CONTEXTBANK(0));

	/*
	 * Write the new TTBR0 to the memstore. This is good for debugging.
	 */
	OUT_PKT7(ring, CP_MEM_WRITE, 4);
	OUT_RING(ring, CP_MEM_WRITE_0_ADDR_LO(lower_32_bits(memptr)));
	OUT_RING(ring, CP_MEM_WRITE_1_ADDR_HI(upper_32_bits(memptr)));
	OUT_RING(ring, lower_32_bits(ttbr));
	OUT_RING(ring, (asid << 16) | upper_32_bits(ttbr));

	/*
	 * And finally, trigger a uche flush to be sure there isn't anything
	 * lingering in that part of the GPU
	 */

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, 0x31);

	a6xx_gpu->cur_ctx_seqno = ctx->seqno;
}

static void a6xx_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit)
{
	unsigned int index = submit->seqno % MSM_GPU_SUBMIT_STATS_COUNT;
	struct msm_drm_private *priv = gpu->dev->dev_private;
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = submit->ring;
	unsigned int i;

	a6xx_set_pagetable(a6xx_gpu, ring, submit->queue->ctx);

	get_stats_counter(ring, REG_A6XX_RBBM_PERFCTR_CP_0_LO,
		rbmemptr_stats(ring, index, cpcycles_start));

	/*
	 * For PM4 the GMU register offsets are calculated from the base of the
	 * GPU registers so we need to add 0x1a800 to the register value on A630
	 * to get the right value from PM4.
	 */
	get_stats_counter(ring, REG_A6XX_CP_ALWAYS_ON_COUNTER_LO,
		rbmemptr_stats(ring, index, alwayson_start));

	/* Invalidate CCU depth and color */
	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(PC_CCU_INVALIDATE_DEPTH));

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(PC_CCU_INVALIDATE_COLOR));

	/* Submit the commands */
	for (i = 0; i < submit->nr_cmds; i++) {
		switch (submit->cmd[i].type) {
		case MSM_SUBMIT_CMD_IB_TARGET_BUF:
			break;
		case MSM_SUBMIT_CMD_CTX_RESTORE_BUF:
			if (priv->lastctx == submit->queue->ctx)
				break;
			fallthrough;
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
	get_stats_counter(ring, REG_A6XX_CP_ALWAYS_ON_COUNTER_LO,
		rbmemptr_stats(ring, index, alwayson_end));

	/* Write the fence to the scratch register */
	OUT_PKT4(ring, REG_A6XX_CP_SCRATCH_REG(2), 1);
	OUT_RING(ring, submit->seqno);

	/*
	 * Execute a CACHE_FLUSH_TS event. This will ensure that the
	 * timestamp is written to the memory and then triggers the interrupt
	 */
	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(CACHE_FLUSH_TS) |
		CP_EVENT_WRITE_0_IRQ);
	OUT_RING(ring, lower_32_bits(rbmemptr(ring, fence)));
	OUT_RING(ring, upper_32_bits(rbmemptr(ring, fence)));
	OUT_RING(ring, submit->seqno);

	trace_msm_gpu_submit_flush(submit,
		gpu_read64(gpu, REG_A6XX_CP_ALWAYS_ON_COUNTER_LO,
			REG_A6XX_CP_ALWAYS_ON_COUNTER_HI));

	a6xx_flush(gpu, ring);
}

const struct adreno_reglist a630_hwcg[] = {
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
	{REG_A6XX_RBBM_CLOCK_HYST_GMU_GX, 0x00000555},
	{},
};

const struct adreno_reglist a640_hwcg[] = {
	{REG_A6XX_RBBM_CLOCK_CNTL_SP0, 0x02222222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_SP0, 0x02222220},
	{REG_A6XX_RBBM_CLOCK_DELAY_SP0, 0x00000080},
	{REG_A6XX_RBBM_CLOCK_HYST_SP0, 0x0000F3CF},
	{REG_A6XX_RBBM_CLOCK_CNTL_TP0, 0x02222222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_TP0, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL3_TP0, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL4_TP0, 0x00022222},
	{REG_A6XX_RBBM_CLOCK_DELAY_TP0, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY2_TP0, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY3_TP0, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY4_TP0, 0x00011111},
	{REG_A6XX_RBBM_CLOCK_HYST_TP0, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST2_TP0, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST3_TP0, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST4_TP0, 0x00077777},
	{REG_A6XX_RBBM_CLOCK_CNTL_RB0, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_RB0, 0x01002222},
	{REG_A6XX_RBBM_CLOCK_CNTL_CCU0, 0x00002220},
	{REG_A6XX_RBBM_CLOCK_HYST_RB_CCU0, 0x00040F00},
	{REG_A6XX_RBBM_CLOCK_CNTL_RAC, 0x05222022},
	{REG_A6XX_RBBM_CLOCK_CNTL2_RAC, 0x00005555},
	{REG_A6XX_RBBM_CLOCK_DELAY_RAC, 0x00000011},
	{REG_A6XX_RBBM_CLOCK_HYST_RAC, 0x00445044},
	{REG_A6XX_RBBM_CLOCK_CNTL_TSE_RAS_RBBM, 0x04222222},
	{REG_A6XX_RBBM_CLOCK_MODE_VFD, 0x00002222},
	{REG_A6XX_RBBM_CLOCK_MODE_GPC, 0x00222222},
	{REG_A6XX_RBBM_CLOCK_DELAY_HLSQ_2, 0x00000002},
	{REG_A6XX_RBBM_CLOCK_MODE_HLSQ, 0x00002222},
	{REG_A6XX_RBBM_CLOCK_DELAY_TSE_RAS_RBBM, 0x00004000},
	{REG_A6XX_RBBM_CLOCK_DELAY_VFD, 0x00002222},
	{REG_A6XX_RBBM_CLOCK_DELAY_GPC, 0x00000200},
	{REG_A6XX_RBBM_CLOCK_DELAY_HLSQ, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_HYST_TSE_RAS_RBBM, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_HYST_VFD, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_HYST_GPC, 0x04104004},
	{REG_A6XX_RBBM_CLOCK_HYST_HLSQ, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_CNTL_TEX_FCHE, 0x00000222},
	{REG_A6XX_RBBM_CLOCK_DELAY_TEX_FCHE, 0x00000111},
	{REG_A6XX_RBBM_CLOCK_HYST_TEX_FCHE, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_CNTL_UCHE, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_HYST_UCHE, 0x00000004},
	{REG_A6XX_RBBM_CLOCK_DELAY_UCHE, 0x00000002},
	{REG_A6XX_RBBM_ISDB_CNT, 0x00000182},
	{REG_A6XX_RBBM_RAC_THRESHOLD_CNT, 0x00000000},
	{REG_A6XX_RBBM_SP_HYST_CNT, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_CNTL_GMU_GX, 0x00000222},
	{REG_A6XX_RBBM_CLOCK_DELAY_GMU_GX, 0x00000111},
	{REG_A6XX_RBBM_CLOCK_HYST_GMU_GX, 0x00000555},
	{},
};

const struct adreno_reglist a650_hwcg[] = {
	{REG_A6XX_RBBM_CLOCK_CNTL_SP0, 0x02222222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_SP0, 0x02222220},
	{REG_A6XX_RBBM_CLOCK_DELAY_SP0, 0x00000080},
	{REG_A6XX_RBBM_CLOCK_HYST_SP0, 0x0000F3CF},
	{REG_A6XX_RBBM_CLOCK_CNTL_TP0, 0x02222222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_TP0, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL3_TP0, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL4_TP0, 0x00022222},
	{REG_A6XX_RBBM_CLOCK_DELAY_TP0, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY2_TP0, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY3_TP0, 0x11111111},
	{REG_A6XX_RBBM_CLOCK_DELAY4_TP0, 0x00011111},
	{REG_A6XX_RBBM_CLOCK_HYST_TP0, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST2_TP0, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST3_TP0, 0x77777777},
	{REG_A6XX_RBBM_CLOCK_HYST4_TP0, 0x00077777},
	{REG_A6XX_RBBM_CLOCK_CNTL_RB0, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_CNTL2_RB0, 0x01002222},
	{REG_A6XX_RBBM_CLOCK_CNTL_CCU0, 0x00002220},
	{REG_A6XX_RBBM_CLOCK_HYST_RB_CCU0, 0x00040F00},
	{REG_A6XX_RBBM_CLOCK_CNTL_RAC, 0x25222022},
	{REG_A6XX_RBBM_CLOCK_CNTL2_RAC, 0x00005555},
	{REG_A6XX_RBBM_CLOCK_DELAY_RAC, 0x00000011},
	{REG_A6XX_RBBM_CLOCK_HYST_RAC, 0x00445044},
	{REG_A6XX_RBBM_CLOCK_CNTL_TSE_RAS_RBBM, 0x04222222},
	{REG_A6XX_RBBM_CLOCK_MODE_VFD, 0x00002222},
	{REG_A6XX_RBBM_CLOCK_MODE_GPC, 0x00222222},
	{REG_A6XX_RBBM_CLOCK_DELAY_HLSQ_2, 0x00000002},
	{REG_A6XX_RBBM_CLOCK_MODE_HLSQ, 0x00002222},
	{REG_A6XX_RBBM_CLOCK_DELAY_TSE_RAS_RBBM, 0x00004000},
	{REG_A6XX_RBBM_CLOCK_DELAY_VFD, 0x00002222},
	{REG_A6XX_RBBM_CLOCK_DELAY_GPC, 0x00000200},
	{REG_A6XX_RBBM_CLOCK_DELAY_HLSQ, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_HYST_TSE_RAS_RBBM, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_HYST_VFD, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_HYST_GPC, 0x04104004},
	{REG_A6XX_RBBM_CLOCK_HYST_HLSQ, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_CNTL_TEX_FCHE, 0x00000222},
	{REG_A6XX_RBBM_CLOCK_DELAY_TEX_FCHE, 0x00000111},
	{REG_A6XX_RBBM_CLOCK_HYST_TEX_FCHE, 0x00000777},
	{REG_A6XX_RBBM_CLOCK_CNTL_UCHE, 0x22222222},
	{REG_A6XX_RBBM_CLOCK_HYST_UCHE, 0x00000004},
	{REG_A6XX_RBBM_CLOCK_DELAY_UCHE, 0x00000002},
	{REG_A6XX_RBBM_ISDB_CNT, 0x00000182},
	{REG_A6XX_RBBM_RAC_THRESHOLD_CNT, 0x00000000},
	{REG_A6XX_RBBM_SP_HYST_CNT, 0x00000000},
	{REG_A6XX_RBBM_CLOCK_CNTL_GMU_GX, 0x00000222},
	{REG_A6XX_RBBM_CLOCK_DELAY_GMU_GX, 0x00000111},
	{REG_A6XX_RBBM_CLOCK_HYST_GMU_GX, 0x00000555},
	{},
};

static void a6xx_set_hwcg(struct msm_gpu *gpu, bool state)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	const struct adreno_reglist *reg;
	unsigned int i;
	u32 val, clock_cntl_on;

	if (!adreno_gpu->info->hwcg)
		return;

	if (adreno_is_a630(adreno_gpu))
		clock_cntl_on = 0x8aa8aa02;
	else
		clock_cntl_on = 0x8aa8aa82;

	val = gpu_read(gpu, REG_A6XX_RBBM_CLOCK_CNTL);

	/* Don't re-program the registers if they are already correct */
	if ((!state && !val) || (state && (val == clock_cntl_on)))
		return;

	/* Disable SP clock before programming HWCG registers */
	gmu_rmw(gmu, REG_A6XX_GPU_GMU_GX_SPTPRAC_CLOCK_CONTROL, 1, 0);

	for (i = 0; (reg = &adreno_gpu->info->hwcg[i], reg->offset); i++)
		gpu_write(gpu, reg->offset, state ? reg->value : 0);

	/* Enable SP clock */
	gmu_rmw(gmu, REG_A6XX_GPU_GMU_GX_SPTPRAC_CLOCK_CONTROL, 0, 1);

	gpu_write(gpu, REG_A6XX_RBBM_CLOCK_CNTL, state ? clock_cntl_on : 0);
}

/* For a615, a616, a618, A619, a630, a640 and a680 */
static const u32 a6xx_protect[] = {
	A6XX_PROTECT_RDONLY(0x00000, 0x04ff),
	A6XX_PROTECT_RDONLY(0x00501, 0x0005),
	A6XX_PROTECT_RDONLY(0x0050b, 0x02f4),
	A6XX_PROTECT_NORDWR(0x0050e, 0x0000),
	A6XX_PROTECT_NORDWR(0x00510, 0x0000),
	A6XX_PROTECT_NORDWR(0x00534, 0x0000),
	A6XX_PROTECT_NORDWR(0x00800, 0x0082),
	A6XX_PROTECT_NORDWR(0x008a0, 0x0008),
	A6XX_PROTECT_NORDWR(0x008ab, 0x0024),
	A6XX_PROTECT_RDONLY(0x008de, 0x00ae),
	A6XX_PROTECT_NORDWR(0x00900, 0x004d),
	A6XX_PROTECT_NORDWR(0x0098d, 0x0272),
	A6XX_PROTECT_NORDWR(0x00e00, 0x0001),
	A6XX_PROTECT_NORDWR(0x00e03, 0x000c),
	A6XX_PROTECT_NORDWR(0x03c00, 0x00c3),
	A6XX_PROTECT_RDONLY(0x03cc4, 0x1fff),
	A6XX_PROTECT_NORDWR(0x08630, 0x01cf),
	A6XX_PROTECT_NORDWR(0x08e00, 0x0000),
	A6XX_PROTECT_NORDWR(0x08e08, 0x0000),
	A6XX_PROTECT_NORDWR(0x08e50, 0x001f),
	A6XX_PROTECT_NORDWR(0x09624, 0x01db),
	A6XX_PROTECT_NORDWR(0x09e70, 0x0001),
	A6XX_PROTECT_NORDWR(0x09e78, 0x0187),
	A6XX_PROTECT_NORDWR(0x0a630, 0x01cf),
	A6XX_PROTECT_NORDWR(0x0ae02, 0x0000),
	A6XX_PROTECT_NORDWR(0x0ae50, 0x032f),
	A6XX_PROTECT_NORDWR(0x0b604, 0x0000),
	A6XX_PROTECT_NORDWR(0x0be02, 0x0001),
	A6XX_PROTECT_NORDWR(0x0be20, 0x17df),
	A6XX_PROTECT_NORDWR(0x0f000, 0x0bff),
	A6XX_PROTECT_RDONLY(0x0fc00, 0x1fff),
	A6XX_PROTECT_NORDWR(0x11c00, 0x0000), /* note: infinite range */
};

/* These are for a620 and a650 */
static const u32 a650_protect[] = {
	A6XX_PROTECT_RDONLY(0x00000, 0x04ff),
	A6XX_PROTECT_RDONLY(0x00501, 0x0005),
	A6XX_PROTECT_RDONLY(0x0050b, 0x02f4),
	A6XX_PROTECT_NORDWR(0x0050e, 0x0000),
	A6XX_PROTECT_NORDWR(0x00510, 0x0000),
	A6XX_PROTECT_NORDWR(0x00534, 0x0000),
	A6XX_PROTECT_NORDWR(0x00800, 0x0082),
	A6XX_PROTECT_NORDWR(0x008a0, 0x0008),
	A6XX_PROTECT_NORDWR(0x008ab, 0x0024),
	A6XX_PROTECT_RDONLY(0x008de, 0x00ae),
	A6XX_PROTECT_NORDWR(0x00900, 0x004d),
	A6XX_PROTECT_NORDWR(0x0098d, 0x0272),
	A6XX_PROTECT_NORDWR(0x00e00, 0x0001),
	A6XX_PROTECT_NORDWR(0x00e03, 0x000c),
	A6XX_PROTECT_NORDWR(0x03c00, 0x00c3),
	A6XX_PROTECT_RDONLY(0x03cc4, 0x1fff),
	A6XX_PROTECT_NORDWR(0x08630, 0x01cf),
	A6XX_PROTECT_NORDWR(0x08e00, 0x0000),
	A6XX_PROTECT_NORDWR(0x08e08, 0x0000),
	A6XX_PROTECT_NORDWR(0x08e50, 0x001f),
	A6XX_PROTECT_NORDWR(0x08e80, 0x027f),
	A6XX_PROTECT_NORDWR(0x09624, 0x01db),
	A6XX_PROTECT_NORDWR(0x09e60, 0x0011),
	A6XX_PROTECT_NORDWR(0x09e78, 0x0187),
	A6XX_PROTECT_NORDWR(0x0a630, 0x01cf),
	A6XX_PROTECT_NORDWR(0x0ae02, 0x0000),
	A6XX_PROTECT_NORDWR(0x0ae50, 0x032f),
	A6XX_PROTECT_NORDWR(0x0b604, 0x0000),
	A6XX_PROTECT_NORDWR(0x0b608, 0x0007),
	A6XX_PROTECT_NORDWR(0x0be02, 0x0001),
	A6XX_PROTECT_NORDWR(0x0be20, 0x17df),
	A6XX_PROTECT_NORDWR(0x0f000, 0x0bff),
	A6XX_PROTECT_RDONLY(0x0fc00, 0x1fff),
	A6XX_PROTECT_NORDWR(0x18400, 0x1fff),
	A6XX_PROTECT_NORDWR(0x1a800, 0x1fff),
	A6XX_PROTECT_NORDWR(0x1f400, 0x0443),
	A6XX_PROTECT_RDONLY(0x1f844, 0x007b),
	A6XX_PROTECT_NORDWR(0x1f887, 0x001b),
	A6XX_PROTECT_NORDWR(0x1f8c0, 0x0000), /* note: infinite range */
};

static void a6xx_set_cp_protect(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	const u32 *regs = a6xx_protect;
	unsigned i, count = ARRAY_SIZE(a6xx_protect), count_max = 32;

	BUILD_BUG_ON(ARRAY_SIZE(a6xx_protect) > 32);
	BUILD_BUG_ON(ARRAY_SIZE(a650_protect) > 48);

	if (adreno_is_a650(adreno_gpu)) {
		regs = a650_protect;
		count = ARRAY_SIZE(a650_protect);
		count_max = 48;
	}

	/*
	 * Enable access protection to privileged registers, fault on an access
	 * protect violation and select the last span to protect from the start
	 * address all the way to the end of the register address space
	 */
	gpu_write(gpu, REG_A6XX_CP_PROTECT_CNTL, BIT(0) | BIT(1) | BIT(3));

	for (i = 0; i < count - 1; i++)
		gpu_write(gpu, REG_A6XX_CP_PROTECT(i), regs[i]);
	/* last CP_PROTECT to have "infinite" length on the last entry */
	gpu_write(gpu, REG_A6XX_CP_PROTECT(count_max - 1), regs[i]);
}

static void a6xx_set_ubwc_config(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	u32 lower_bit = 2;
	u32 amsbc = 0;
	u32 rgb565_predicator = 0;
	u32 uavflagprd_inv = 0;

	/* a618 is using the hw default values */
	if (adreno_is_a618(adreno_gpu))
		return;

	if (adreno_is_a640(adreno_gpu))
		amsbc = 1;

	if (adreno_is_a650(adreno_gpu)) {
		/* TODO: get ddr type from bootloader and use 2 for LPDDR4 */
		lower_bit = 3;
		amsbc = 1;
		rgb565_predicator = 1;
		uavflagprd_inv = 2;
	}

	gpu_write(gpu, REG_A6XX_RB_NC_MODE_CNTL,
		rgb565_predicator << 11 | amsbc << 4 | lower_bit << 1);
	gpu_write(gpu, REG_A6XX_TPL1_NC_MODE_CNTL, lower_bit << 1);
	gpu_write(gpu, REG_A6XX_SP_NC_MODE_CNTL,
		uavflagprd_inv << 4 | lower_bit << 1);
	gpu_write(gpu, REG_A6XX_UCHE_MODE_CNTL, lower_bit << 21);
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

static void a6xx_ucode_check_version(struct a6xx_gpu *a6xx_gpu,
		struct drm_gem_object *obj)
{
	u32 *buf = msm_gem_get_vaddr_active(obj);

	if (IS_ERR(buf))
		return;

	/*
	 * If the lowest nibble is 0xa that is an indication that this microcode
	 * has been patched. The actual version is in dword [3] but we only care
	 * about the patchlevel which is the lowest nibble of dword [3]
	 *
	 * Otherwise check that the firmware is greater than or equal to 1.90
	 * which was the first version that had this fix built in
	 */
	if (((buf[0] & 0xf) == 0xa) && (buf[2] & 0xf) >= 1)
		a6xx_gpu->has_whereami = true;
	else if ((buf[0] & 0xfff) > 0x190)
		a6xx_gpu->has_whereami = true;

	msm_gem_put_vaddr(obj);
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
		a6xx_ucode_check_version(a6xx_gpu, a6xx_gpu->sqe_bo);
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

	/* enable hardware clockgating */
	a6xx_set_hwcg(gpu, true);

	/* VBIF/GBIF start*/
	if (adreno_is_a640(adreno_gpu) || adreno_is_a650(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_GBIF_QSB_SIDE0, 0x00071620);
		gpu_write(gpu, REG_A6XX_GBIF_QSB_SIDE1, 0x00071620);
		gpu_write(gpu, REG_A6XX_GBIF_QSB_SIDE2, 0x00071620);
		gpu_write(gpu, REG_A6XX_GBIF_QSB_SIDE3, 0x00071620);
		gpu_write(gpu, REG_A6XX_GBIF_QSB_SIDE3, 0x00071620);
		gpu_write(gpu, REG_A6XX_RBBM_GBIF_CLIENT_QOS_CNTL, 0x3);
	} else {
		gpu_write(gpu, REG_A6XX_RBBM_VBIF_CLIENT_QOS_CNTL, 0x3);
	}

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

	if (!adreno_is_a650(adreno_gpu)) {
		/* Set the GMEM VA range [0x100000:0x100000 + gpu->gmem - 1] */
		gpu_write64(gpu, REG_A6XX_UCHE_GMEM_RANGE_MIN_LO,
			REG_A6XX_UCHE_GMEM_RANGE_MIN_HI, 0x00100000);

		gpu_write64(gpu, REG_A6XX_UCHE_GMEM_RANGE_MAX_LO,
			REG_A6XX_UCHE_GMEM_RANGE_MAX_HI,
			0x00100000 + adreno_gpu->gmem - 1);
	}

	gpu_write(gpu, REG_A6XX_UCHE_FILTER_CNTL, 0x804);
	gpu_write(gpu, REG_A6XX_UCHE_CACHE_WAYS, 0x4);

	if (adreno_is_a640(adreno_gpu) || adreno_is_a650(adreno_gpu))
		gpu_write(gpu, REG_A6XX_CP_ROQ_THRESHOLDS_2, 0x02000140);
	else
		gpu_write(gpu, REG_A6XX_CP_ROQ_THRESHOLDS_2, 0x010000c0);
	gpu_write(gpu, REG_A6XX_CP_ROQ_THRESHOLDS_1, 0x8040362c);

	/* Setting the mem pool size */
	gpu_write(gpu, REG_A6XX_CP_MEM_POOL_SIZE, 128);

	/* Setting the primFifo thresholds default values */
	if (adreno_is_a650(adreno_gpu))
		gpu_write(gpu, REG_A6XX_PC_DBG_ECO_CNTL, 0x00300000);
	else if (adreno_is_a640(adreno_gpu))
		gpu_write(gpu, REG_A6XX_PC_DBG_ECO_CNTL, 0x00200000);
	else
		gpu_write(gpu, REG_A6XX_PC_DBG_ECO_CNTL, (0x300 << 11));

	/* Set the AHB default slave response to "ERROR" */
	gpu_write(gpu, REG_A6XX_CP_AHB_CNTL, 0x1);

	/* Turn on performance counters */
	gpu_write(gpu, REG_A6XX_RBBM_PERFCTR_CNTL, 0x1);

	/* Select CP0 to always count cycles */
	gpu_write(gpu, REG_A6XX_CP_PERFCTR_CP_SEL_0, PERF_CP_ALWAYS_COUNT);

	a6xx_set_ubwc_config(gpu);

	/* Enable fault detection */
	gpu_write(gpu, REG_A6XX_RBBM_INTERFACE_HANG_INT_CNTL,
		(1 << 30) | 0x1fffff);

	gpu_write(gpu, REG_A6XX_UCHE_CLIENT_PF, 1);

	/* Set weights for bicubic filtering */
	if (adreno_is_a650(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE_0, 0);
		gpu_write(gpu, REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE_1,
			0x3fe05ff4);
		gpu_write(gpu, REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE_2,
			0x3fa0ebee);
		gpu_write(gpu, REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE_3,
			0x3f5193ed);
		gpu_write(gpu, REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE_4,
			0x3f0243f0);
	}

	/* Protect registers from the CP */
	a6xx_set_cp_protect(gpu);

	/* Enable expanded apriv for targets that support it */
	if (gpu->hw_apriv) {
		gpu_write(gpu, REG_A6XX_CP_APRIV_CNTL,
			(1 << 6) | (1 << 5) | (1 << 3) | (1 << 2) | (1 << 1));
	}

	/* Enable interrupts */
	gpu_write(gpu, REG_A6XX_RBBM_INT_0_MASK, A6XX_INT_MASK);

	ret = adreno_hw_init(gpu);
	if (ret)
		goto out;

	ret = a6xx_ucode_init(gpu);
	if (ret)
		goto out;

	/* Set the ringbuffer address */
	gpu_write64(gpu, REG_A6XX_CP_RB_BASE, REG_A6XX_CP_RB_BASE_HI,
		gpu->rb[0]->iova);

	/* Targets that support extended APRIV can use the RPTR shadow from
	 * hardware but all the other ones need to disable the feature. Targets
	 * that support the WHERE_AM_I opcode can use that instead
	 */
	if (adreno_gpu->base.hw_apriv)
		gpu_write(gpu, REG_A6XX_CP_RB_CNTL, MSM_GPU_RB_CNTL_DEFAULT);
	else
		gpu_write(gpu, REG_A6XX_CP_RB_CNTL,
			MSM_GPU_RB_CNTL_DEFAULT | AXXX_CP_RB_CNTL_NO_UPDATE);

	/*
	 * Expanded APRIV and targets that support WHERE_AM_I both need a
	 * privileged buffer to store the RPTR shadow
	 */

	if (adreno_gpu->base.hw_apriv || a6xx_gpu->has_whereami) {
		if (!a6xx_gpu->shadow_bo) {
			a6xx_gpu->shadow = msm_gem_kernel_new_locked(gpu->dev,
				sizeof(u32) * gpu->nr_rings,
				MSM_BO_UNCACHED | MSM_BO_MAP_PRIV,
				gpu->aspace, &a6xx_gpu->shadow_bo,
				&a6xx_gpu->shadow_iova);

			if (IS_ERR(a6xx_gpu->shadow))
				return PTR_ERR(a6xx_gpu->shadow);
		}

		gpu_write64(gpu, REG_A6XX_CP_RB_RPTR_ADDR_LO,
			REG_A6XX_CP_RB_RPTR_ADDR_HI,
			shadowptr(a6xx_gpu, gpu->rb[0]));
	}

	/* Always come up on rb 0 */
	a6xx_gpu->cur_ring = gpu->rb[0];

	a6xx_gpu->cur_ctx_seqno = 0;

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

	if (a6xx_gpu->gmu.legacy) {
		/* Take the GMU out of its special boot mode */
		a6xx_gmu_clear_oob(&a6xx_gpu->gmu, GMU_OOB_BOOT_SLUMBER);
	}

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

static int a6xx_pm_resume(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	int ret;

	gpu->needs_hw_init = true;

	trace_msm_gpu_resume(0);

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
	int i, ret;

	trace_msm_gpu_suspend(0);

	devfreq_suspend_device(gpu->devfreq.devfreq);

	ret = a6xx_gmu_stop(a6xx_gpu);
	if (ret)
		return ret;

	if (a6xx_gpu->shadow_bo)
		for (i = 0; i < gpu->nr_rings; i++)
			a6xx_gpu->shadow[i] = 0;

	return 0;
}

static int a6xx_get_timestamp(struct msm_gpu *gpu, uint64_t *value)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	static DEFINE_MUTEX(perfcounter_oob);

	mutex_lock(&perfcounter_oob);

	/* Force the GPU power on so we can read this register */
	a6xx_gmu_set_oob(&a6xx_gpu->gmu, GMU_OOB_PERFCOUNTER_SET);

	*value = gpu_read64(gpu, REG_A6XX_CP_ALWAYS_ON_COUNTER_LO,
		REG_A6XX_CP_ALWAYS_ON_COUNTER_HI);

	a6xx_gmu_clear_oob(&a6xx_gpu->gmu, GMU_OOB_PERFCOUNTER_SET);
	mutex_unlock(&perfcounter_oob);
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
		drm_gem_object_put(a6xx_gpu->sqe_bo);
	}

	if (a6xx_gpu->shadow_bo) {
		msm_gem_unpin_iova(a6xx_gpu->shadow_bo, gpu->aspace);
		drm_gem_object_put(a6xx_gpu->shadow_bo);
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


	/* Only read the gpu busy if the hardware is already active */
	if (pm_runtime_get_if_in_use(a6xx_gpu->gmu.dev) == 0)
		return 0;

	busy_cycles = gmu_read64(&a6xx_gpu->gmu,
			REG_A6XX_GMU_CX_GMU_POWER_COUNTER_XOCLK_0_L,
			REG_A6XX_GMU_CX_GMU_POWER_COUNTER_XOCLK_0_H);

	busy_time = (busy_cycles - gpu->devfreq.busy_cycles) * 10;
	do_div(busy_time, 192);

	gpu->devfreq.busy_cycles = busy_cycles;

	pm_runtime_put(a6xx_gpu->gmu.dev);

	if (WARN_ON(busy_time > ~0LU))
		return ~0LU;

	return (unsigned long)busy_time;
}

static struct msm_gem_address_space *
a6xx_create_private_address_space(struct msm_gpu *gpu)
{
	struct msm_mmu *mmu;

	mmu = msm_iommu_pagetable_create(gpu->aspace->mmu);

	if (IS_ERR(mmu))
		return ERR_CAST(mmu);

	return msm_gem_address_space_create(mmu,
		"gpu", 0x100000000ULL, 0x1ffffffffULL);
}

static uint32_t a6xx_get_rptr(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	if (adreno_gpu->base.hw_apriv || a6xx_gpu->has_whereami)
		return a6xx_gpu->shadow[ring->id];

	return ring->memptrs->rptr = gpu_read(gpu, REG_A6XX_CP_RB_RPTR);
}

static const struct adreno_gpu_funcs funcs = {
	.base = {
		.get_param = adreno_get_param,
		.hw_init = a6xx_hw_init,
		.pm_suspend = a6xx_pm_suspend,
		.pm_resume = a6xx_pm_resume,
		.recover = a6xx_recover,
		.submit = a6xx_submit,
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
		.create_address_space = adreno_iommu_create_address_space,
		.create_private_address_space = a6xx_create_private_address_space,
		.get_rptr = a6xx_get_rptr,
	},
	.get_timestamp = a6xx_get_timestamp,
};

struct msm_gpu *a6xx_gpu_init(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = priv->gpu_pdev;
	struct adreno_platform_config *config = pdev->dev.platform_data;
	const struct adreno_info *info;
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

	/*
	 * We need to know the platform type before calling into adreno_gpu_init
	 * so that the hw_apriv flag can be correctly set. Snoop into the info
	 * and grab the revision number
	 */
	info = adreno_info(config->rev);

	if (info && info->revn == 650)
		adreno_gpu->base.hw_apriv = true;

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
