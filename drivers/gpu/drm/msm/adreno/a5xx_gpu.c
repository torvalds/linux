// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016-2017 The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/qcom_scm.h>
#include <linux/pm_opp.h>
#include <linux/nvmem-consumer.h>
#include <linux/slab.h>
#include "msm_gem.h"
#include "msm_mmu.h"
#include "a5xx_gpu.h"

extern bool hang_debug;
static void a5xx_dump(struct msm_gpu *gpu);

#define GPU_PAS_ID 13

static void update_shadow_rptr(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);

	if (a5xx_gpu->has_whereami) {
		OUT_PKT7(ring, CP_WHERE_AM_I, 2);
		OUT_RING(ring, lower_32_bits(shadowptr(a5xx_gpu, ring)));
		OUT_RING(ring, upper_32_bits(shadowptr(a5xx_gpu, ring)));
	}
}

void a5xx_flush(struct msm_gpu *gpu, struct msm_ringbuffer *ring,
		bool sync)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);
	uint32_t wptr;
	unsigned long flags;

	/*
	 * Most flush operations need to issue a WHERE_AM_I opcode to sync up
	 * the rptr shadow
	 */
	if (sync)
		update_shadow_rptr(gpu, ring);

	spin_lock_irqsave(&ring->preempt_lock, flags);

	/* Copy the shadow to the actual register */
	ring->cur = ring->next;

	/* Make sure to wrap wptr if we need to */
	wptr = get_wptr(ring);

	spin_unlock_irqrestore(&ring->preempt_lock, flags);

	/* Make sure everything is posted before making a decision */
	mb();

	/* Update HW if this is the current ring and we are not in preempt */
	if (a5xx_gpu->cur_ring == ring && !a5xx_in_preempt(a5xx_gpu))
		gpu_write(gpu, REG_A5XX_CP_RB_WPTR, wptr);
}

static void a5xx_submit_in_rb(struct msm_gpu *gpu, struct msm_gem_submit *submit)
{
	struct msm_ringbuffer *ring = submit->ring;
	struct msm_gem_object *obj;
	uint32_t *ptr, dwords;
	unsigned int i;

	for (i = 0; i < submit->nr_cmds; i++) {
		switch (submit->cmd[i].type) {
		case MSM_SUBMIT_CMD_IB_TARGET_BUF:
			break;
		case MSM_SUBMIT_CMD_CTX_RESTORE_BUF:
			if (gpu->cur_ctx_seqno == submit->queue->ctx->seqno)
				break;
			fallthrough;
		case MSM_SUBMIT_CMD_BUF:
			/* copy commands into RB: */
			obj = submit->bos[submit->cmd[i].idx].obj;
			dwords = submit->cmd[i].size;

			ptr = msm_gem_get_vaddr(&obj->base);

			/* _get_vaddr() shouldn't fail at this point,
			 * since we've already mapped it once in
			 * submit_reloc()
			 */
			if (WARN_ON(!ptr))
				return;

			for (i = 0; i < dwords; i++) {
				/* normally the OUT_PKTn() would wait
				 * for space for the packet.  But since
				 * we just OUT_RING() the whole thing,
				 * need to call adreno_wait_ring()
				 * ourself:
				 */
				adreno_wait_ring(ring, 1);
				OUT_RING(ring, ptr[i]);
			}

			msm_gem_put_vaddr(&obj->base);

			break;
		}
	}

	a5xx_flush(gpu, ring, true);
	a5xx_preempt_trigger(gpu);

	/* we might not necessarily have a cmd from userspace to
	 * trigger an event to know that submit has completed, so
	 * do this manually:
	 */
	a5xx_idle(gpu, ring);
	ring->memptrs->fence = submit->seqno;
	msm_gpu_retire(gpu);
}

static void a5xx_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = submit->ring;
	unsigned int i, ibs = 0;

	if (IS_ENABLED(CONFIG_DRM_MSM_GPU_SUDO) && submit->in_rb) {
		gpu->cur_ctx_seqno = 0;
		a5xx_submit_in_rb(gpu, submit);
		return;
	}

	OUT_PKT7(ring, CP_PREEMPT_ENABLE_GLOBAL, 1);
	OUT_RING(ring, 0x02);

	/* Turn off protected mode to write to special registers */
	OUT_PKT7(ring, CP_SET_PROTECTED_MODE, 1);
	OUT_RING(ring, 0);

	/* Set the save preemption record for the ring/command */
	OUT_PKT4(ring, REG_A5XX_CP_CONTEXT_SWITCH_SAVE_ADDR_LO, 2);
	OUT_RING(ring, lower_32_bits(a5xx_gpu->preempt_iova[submit->ring->id]));
	OUT_RING(ring, upper_32_bits(a5xx_gpu->preempt_iova[submit->ring->id]));

	/* Turn back on protected mode */
	OUT_PKT7(ring, CP_SET_PROTECTED_MODE, 1);
	OUT_RING(ring, 1);

	/* Enable local preemption for finegrain preemption */
	OUT_PKT7(ring, CP_PREEMPT_ENABLE_GLOBAL, 1);
	OUT_RING(ring, 0x02);

	/* Allow CP_CONTEXT_SWITCH_YIELD packets in the IB2 */
	OUT_PKT7(ring, CP_YIELD_ENABLE, 1);
	OUT_RING(ring, 0x02);

	/* Submit the commands */
	for (i = 0; i < submit->nr_cmds; i++) {
		switch (submit->cmd[i].type) {
		case MSM_SUBMIT_CMD_IB_TARGET_BUF:
			break;
		case MSM_SUBMIT_CMD_CTX_RESTORE_BUF:
			if (gpu->cur_ctx_seqno == submit->queue->ctx->seqno)
				break;
			fallthrough;
		case MSM_SUBMIT_CMD_BUF:
			OUT_PKT7(ring, CP_INDIRECT_BUFFER_PFE, 3);
			OUT_RING(ring, lower_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, upper_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, submit->cmd[i].size);
			ibs++;
			break;
		}

		/*
		 * Periodically update shadow-wptr if needed, so that we
		 * can see partial progress of submits with large # of
		 * cmds.. otherwise we could needlessly stall waiting for
		 * ringbuffer state, simply due to looking at a shadow
		 * rptr value that has not been updated
		 */
		if ((ibs % 32) == 0)
			update_shadow_rptr(gpu, ring);
	}

	/*
	 * Write the render mode to NULL (0) to indicate to the CP that the IBs
	 * are done rendering - otherwise a lucky preemption would start
	 * replaying from the last checkpoint
	 */
	OUT_PKT7(ring, CP_SET_RENDER_MODE, 5);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);

	/* Turn off IB level preemptions */
	OUT_PKT7(ring, CP_YIELD_ENABLE, 1);
	OUT_RING(ring, 0x01);

	/* Write the fence to the scratch register */
	OUT_PKT4(ring, REG_A5XX_CP_SCRATCH_REG(2), 1);
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

	/* Yield the floor on command completion */
	OUT_PKT7(ring, CP_CONTEXT_SWITCH_YIELD, 4);
	/*
	 * If dword[2:1] are non zero, they specify an address for the CP to
	 * write the value of dword[3] to on preemption complete. Write 0 to
	 * skip the write
	 */
	OUT_RING(ring, 0x00);
	OUT_RING(ring, 0x00);
	/* Data value - not used if the address above is 0 */
	OUT_RING(ring, 0x01);
	/* Set bit 0 to trigger an interrupt on preempt complete */
	OUT_RING(ring, 0x01);

	/* A WHERE_AM_I packet is not needed after a YIELD */
	a5xx_flush(gpu, ring, false);

	/* Check to see if we need to start preemption */
	a5xx_preempt_trigger(gpu);
}

static const struct adreno_five_hwcg_regs {
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
}, a50x_hwcg[] = {
	{REG_A5XX_RBBM_CLOCK_CNTL_SP0, 0x02222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_SP0, 0x02222220},
	{REG_A5XX_RBBM_CLOCK_HYST_SP0, 0x0000F3CF},
	{REG_A5XX_RBBM_CLOCK_DELAY_SP0, 0x00000080},
	{REG_A5XX_RBBM_CLOCK_CNTL_TP0, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_TP0, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL3_TP0, 0x00002222},
	{REG_A5XX_RBBM_CLOCK_HYST_TP0, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST2_TP0, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST3_TP0, 0x00007777},
	{REG_A5XX_RBBM_CLOCK_DELAY_TP0, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY2_TP0, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY3_TP0, 0x00001111},
	{REG_A5XX_RBBM_CLOCK_CNTL2_UCHE, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL3_UCHE, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL4_UCHE, 0x00222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_UCHE, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_HYST_UCHE, 0x00FFFFF4},
	{REG_A5XX_RBBM_CLOCK_DELAY_UCHE, 0x00000002},
	{REG_A5XX_RBBM_CLOCK_CNTL_RB0, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_RB0, 0x00222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_CCU0, 0x00022220},
	{REG_A5XX_RBBM_CLOCK_CNTL_RAC, 0x05522222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_RAC, 0x00505555},
	{REG_A5XX_RBBM_CLOCK_HYST_RB_CCU0, 0x04040404},
	{REG_A5XX_RBBM_CLOCK_HYST_RAC, 0x07444044},
	{REG_A5XX_RBBM_CLOCK_DELAY_RB_CCU_L1_0, 0x00000002},
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
	{REG_A5XX_RBBM_CLOCK_DELAY_VFD, 0x00002222},
}, a512_hwcg[] = {
	{REG_A5XX_RBBM_CLOCK_CNTL_SP0, 0x02222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_SP1, 0x02222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_SP0, 0x02222220},
	{REG_A5XX_RBBM_CLOCK_CNTL2_SP1, 0x02222220},
	{REG_A5XX_RBBM_CLOCK_HYST_SP0, 0x0000F3CF},
	{REG_A5XX_RBBM_CLOCK_HYST_SP1, 0x0000F3CF},
	{REG_A5XX_RBBM_CLOCK_DELAY_SP0, 0x00000080},
	{REG_A5XX_RBBM_CLOCK_DELAY_SP1, 0x00000080},
	{REG_A5XX_RBBM_CLOCK_CNTL_TP0, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_TP1, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_TP0, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_TP1, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL3_TP0, 0x00002222},
	{REG_A5XX_RBBM_CLOCK_CNTL3_TP1, 0x00002222},
	{REG_A5XX_RBBM_CLOCK_HYST_TP0, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST_TP1, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST2_TP0, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST2_TP1, 0x77777777},
	{REG_A5XX_RBBM_CLOCK_HYST3_TP0, 0x00007777},
	{REG_A5XX_RBBM_CLOCK_HYST3_TP1, 0x00007777},
	{REG_A5XX_RBBM_CLOCK_DELAY_TP0, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY_TP1, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY2_TP0, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY2_TP1, 0x11111111},
	{REG_A5XX_RBBM_CLOCK_DELAY3_TP0, 0x00001111},
	{REG_A5XX_RBBM_CLOCK_DELAY3_TP1, 0x00001111},
	{REG_A5XX_RBBM_CLOCK_CNTL_UCHE, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_UCHE, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL3_UCHE, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL4_UCHE, 0x00222222},
	{REG_A5XX_RBBM_CLOCK_HYST_UCHE, 0x00444444},
	{REG_A5XX_RBBM_CLOCK_DELAY_UCHE, 0x00000002},
	{REG_A5XX_RBBM_CLOCK_CNTL_RB0, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_RB1, 0x22222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_RB0, 0x00222222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_RB1, 0x00222222},
	{REG_A5XX_RBBM_CLOCK_CNTL_CCU0, 0x00022220},
	{REG_A5XX_RBBM_CLOCK_CNTL_CCU1, 0x00022220},
	{REG_A5XX_RBBM_CLOCK_CNTL_RAC, 0x05522222},
	{REG_A5XX_RBBM_CLOCK_CNTL2_RAC, 0x00505555},
	{REG_A5XX_RBBM_CLOCK_HYST_RB_CCU0, 0x04040404},
	{REG_A5XX_RBBM_CLOCK_HYST_RB_CCU1, 0x04040404},
	{REG_A5XX_RBBM_CLOCK_HYST_RAC, 0x07444044},
	{REG_A5XX_RBBM_CLOCK_DELAY_RB_CCU_L1_0, 0x00000002},
	{REG_A5XX_RBBM_CLOCK_DELAY_RB_CCU_L1_1, 0x00000002},
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
	{REG_A5XX_RBBM_CLOCK_DELAY_VFD, 0x00002222},
};

void a5xx_set_hwcg(struct msm_gpu *gpu, bool state)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	const struct adreno_five_hwcg_regs *regs;
	unsigned int i, sz;

	if (adreno_is_a506(adreno_gpu) || adreno_is_a508(adreno_gpu)) {
		regs = a50x_hwcg;
		sz = ARRAY_SIZE(a50x_hwcg);
	} else if (adreno_is_a509(adreno_gpu) || adreno_is_a512(adreno_gpu)) {
		regs = a512_hwcg;
		sz = ARRAY_SIZE(a512_hwcg);
	} else {
		regs = a5xx_hwcg;
		sz = ARRAY_SIZE(a5xx_hwcg);
	}

	for (i = 0; i < sz; i++)
		gpu_write(gpu, regs[i].offset,
			  state ? regs[i].value : 0);

	if (adreno_is_a540(adreno_gpu)) {
		gpu_write(gpu, REG_A5XX_RBBM_CLOCK_DELAY_GPMU, state ? 0x00000770 : 0);
		gpu_write(gpu, REG_A5XX_RBBM_CLOCK_HYST_GPMU, state ? 0x00000004 : 0);
	}

	gpu_write(gpu, REG_A5XX_RBBM_CLOCK_CNTL, state ? 0xAAA8AA00 : 0);
	gpu_write(gpu, REG_A5XX_RBBM_ISDB_CNT, state ? 0x182 : 0x180);
}

static int a5xx_me_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct msm_ringbuffer *ring = gpu->rb[0];

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
	if (adreno_is_a506(adreno_gpu) || adreno_is_a530(adreno_gpu)) {
		/* Workaround for token end syncs
		 * Force a WFI after every direct-render 3D mode draw and every
		 * 2D mode 3 draw
		 */
		OUT_RING(ring, 0x0000000B);
	} else if (adreno_is_a510(adreno_gpu)) {
		/* Workaround for token and syncs */
		OUT_RING(ring, 0x00000001);
	} else {
		/* No workarounds enabled */
		OUT_RING(ring, 0x00000000);
	}

	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);

	a5xx_flush(gpu, ring, true);
	return a5xx_idle(gpu, ring) ? 0 : -EINVAL;
}

static int a5xx_preempt_start(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = gpu->rb[0];

	if (gpu->nr_rings == 1)
		return 0;

	/* Turn off protected mode to write to special registers */
	OUT_PKT7(ring, CP_SET_PROTECTED_MODE, 1);
	OUT_RING(ring, 0);

	/* Set the save preemption record for the ring/command */
	OUT_PKT4(ring, REG_A5XX_CP_CONTEXT_SWITCH_SAVE_ADDR_LO, 2);
	OUT_RING(ring, lower_32_bits(a5xx_gpu->preempt_iova[ring->id]));
	OUT_RING(ring, upper_32_bits(a5xx_gpu->preempt_iova[ring->id]));

	/* Turn back on protected mode */
	OUT_PKT7(ring, CP_SET_PROTECTED_MODE, 1);
	OUT_RING(ring, 1);

	OUT_PKT7(ring, CP_PREEMPT_ENABLE_GLOBAL, 1);
	OUT_RING(ring, 0x00);

	OUT_PKT7(ring, CP_PREEMPT_ENABLE_LOCAL, 1);
	OUT_RING(ring, 0x01);

	OUT_PKT7(ring, CP_YIELD_ENABLE, 1);
	OUT_RING(ring, 0x01);

	/* Yield the floor on command completion */
	OUT_PKT7(ring, CP_CONTEXT_SWITCH_YIELD, 4);
	OUT_RING(ring, 0x00);
	OUT_RING(ring, 0x00);
	OUT_RING(ring, 0x01);
	OUT_RING(ring, 0x01);

	/* The WHERE_AMI_I packet is not needed after a YIELD is issued */
	a5xx_flush(gpu, ring, false);

	return a5xx_idle(gpu, ring) ? 0 : -EINVAL;
}

static void a5xx_ucode_check_version(struct a5xx_gpu *a5xx_gpu,
		struct drm_gem_object *obj)
{
	u32 *buf = msm_gem_get_vaddr(obj);

	if (IS_ERR(buf))
		return;

	/*
	 * If the lowest nibble is 0xa that is an indication that this microcode
	 * has been patched. The actual version is in dword [3] but we only care
	 * about the patchlevel which is the lowest nibble of dword [3]
	 */
	if (((buf[0] & 0xf) == 0xa) && (buf[2] & 0xf) >= 1)
		a5xx_gpu->has_whereami = true;

	msm_gem_put_vaddr(obj);
}

static int a5xx_ucode_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);
	int ret;

	if (!a5xx_gpu->pm4_bo) {
		a5xx_gpu->pm4_bo = adreno_fw_create_bo(gpu,
			adreno_gpu->fw[ADRENO_FW_PM4], &a5xx_gpu->pm4_iova);


		if (IS_ERR(a5xx_gpu->pm4_bo)) {
			ret = PTR_ERR(a5xx_gpu->pm4_bo);
			a5xx_gpu->pm4_bo = NULL;
			DRM_DEV_ERROR(gpu->dev->dev, "could not allocate PM4: %d\n",
				ret);
			return ret;
		}

		msm_gem_object_set_name(a5xx_gpu->pm4_bo, "pm4fw");
	}

	if (!a5xx_gpu->pfp_bo) {
		a5xx_gpu->pfp_bo = adreno_fw_create_bo(gpu,
			adreno_gpu->fw[ADRENO_FW_PFP], &a5xx_gpu->pfp_iova);

		if (IS_ERR(a5xx_gpu->pfp_bo)) {
			ret = PTR_ERR(a5xx_gpu->pfp_bo);
			a5xx_gpu->pfp_bo = NULL;
			DRM_DEV_ERROR(gpu->dev->dev, "could not allocate PFP: %d\n",
				ret);
			return ret;
		}

		msm_gem_object_set_name(a5xx_gpu->pfp_bo, "pfpfw");
		a5xx_ucode_check_version(a5xx_gpu, a5xx_gpu->pfp_bo);
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
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	int ret;

	/*
	 * Adreno 506 have CPZ Retention feature and doesn't require
	 * to resume zap shader
	 */
	if (adreno_is_a506(adreno_gpu))
		return 0;

	ret = qcom_scm_set_remote_state(SCM_GPU_ZAP_SHADER_RESUME, GPU_PAS_ID);
	if (ret)
		DRM_ERROR("%s: zap-shader resume failed: %d\n",
			gpu->name, ret);

	return ret;
}

static int a5xx_zap_shader_init(struct msm_gpu *gpu)
{
	static bool loaded;
	int ret;

	/*
	 * If the zap shader is already loaded into memory we just need to kick
	 * the remote processor to reinitialize it
	 */
	if (loaded)
		return a5xx_zap_shader_resume(gpu);

	ret = adreno_zap_shader_load(gpu, GPU_PAS_ID);

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
	  A5XX_RBBM_INT_0_MASK_MISC_HANG_DETECT | \
	  A5XX_RBBM_INT_0_MASK_CP_SW | \
	  A5XX_RBBM_INT_0_MASK_CP_CACHE_FLUSH_TS | \
	  A5XX_RBBM_INT_0_MASK_UCHE_OOB_ACCESS | \
	  A5XX_RBBM_INT_0_MASK_GPMU_VOLTAGE_DROOP)

static int a5xx_hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);
	u32 regbit;
	int ret;

	gpu_write(gpu, REG_A5XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x00000003);

	if (adreno_is_a509(adreno_gpu) || adreno_is_a512(adreno_gpu) ||
	    adreno_is_a540(adreno_gpu))
		gpu_write(gpu, REG_A5XX_VBIF_GATE_OFF_WRREQ_EN, 0x00000009);

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

	/* Select CP0 to always count cycles */
	gpu_write(gpu, REG_A5XX_CP_PERFCTR_CP_SEL_0, PERF_CP_ALWAYS_COUNT);

	/* Select RBBM0 to countable 6 to get the busy status for devfreq */
	gpu_write(gpu, REG_A5XX_RBBM_PERFCTR_RBBM_SEL_0, 6);

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

	if (adreno_is_a506(adreno_gpu) || adreno_is_a508(adreno_gpu) ||
	    adreno_is_a510(adreno_gpu)) {
		gpu_write(gpu, REG_A5XX_CP_MEQ_THRESHOLDS, 0x20);
		if (adreno_is_a506(adreno_gpu) || adreno_is_a508(adreno_gpu))
			gpu_write(gpu, REG_A5XX_CP_MERCIU_SIZE, 0x400);
		else
			gpu_write(gpu, REG_A5XX_CP_MERCIU_SIZE, 0x20);
		gpu_write(gpu, REG_A5XX_CP_ROQ_THRESHOLDS_2, 0x40000030);
		gpu_write(gpu, REG_A5XX_CP_ROQ_THRESHOLDS_1, 0x20100D0A);
	} else {
		gpu_write(gpu, REG_A5XX_CP_MEQ_THRESHOLDS, 0x40);
		if (adreno_is_a530(adreno_gpu))
			gpu_write(gpu, REG_A5XX_CP_MERCIU_SIZE, 0x40);
		else
			gpu_write(gpu, REG_A5XX_CP_MERCIU_SIZE, 0x400);
		gpu_write(gpu, REG_A5XX_CP_ROQ_THRESHOLDS_2, 0x80000060);
		gpu_write(gpu, REG_A5XX_CP_ROQ_THRESHOLDS_1, 0x40201B16);
	}

	if (adreno_is_a506(adreno_gpu) || adreno_is_a508(adreno_gpu))
		gpu_write(gpu, REG_A5XX_PC_DBG_ECO_CNTL,
			  (0x100 << 11 | 0x100 << 22));
	else if (adreno_is_a509(adreno_gpu) || adreno_is_a510(adreno_gpu) ||
		 adreno_is_a512(adreno_gpu))
		gpu_write(gpu, REG_A5XX_PC_DBG_ECO_CNTL,
			  (0x200 << 11 | 0x200 << 22));
	else
		gpu_write(gpu, REG_A5XX_PC_DBG_ECO_CNTL,
			  (0x400 << 11 | 0x300 << 22));

	if (adreno_gpu->info->quirks & ADRENO_QUIRK_TWO_PASS_USE_WFI)
		gpu_rmw(gpu, REG_A5XX_PC_DBG_ECO_CNTL, 0, (1 << 8));

	/*
	 * Disable the RB sampler datapath DP2 clock gating optimization
	 * for 1-SP GPUs, as it is enabled by default.
	 */
	if (adreno_is_a506(adreno_gpu) || adreno_is_a508(adreno_gpu) ||
	    adreno_is_a509(adreno_gpu) || adreno_is_a512(adreno_gpu))
		gpu_rmw(gpu, REG_A5XX_RB_DBG_ECO_CNTL, 0, (1 << 9));

	/* Disable UCHE global filter as SP can invalidate/flush independently */
	gpu_write(gpu, REG_A5XX_UCHE_MODE_CNTL, BIT(29));

	/* Enable USE_RETENTION_FLOPS */
	gpu_write(gpu, REG_A5XX_CP_CHICKEN_DBG, 0x02000000);

	/* Enable ME/PFP split notification */
	gpu_write(gpu, REG_A5XX_RBBM_AHB_CNTL1, 0xA6FFFFFF);

	/*
	 *  In A5x, CCU can send context_done event of a particular context to
	 *  UCHE which ultimately reaches CP even when there is valid
	 *  transaction of that context inside CCU. This can let CP to program
	 *  config registers, which will make the "valid transaction" inside
	 *  CCU to be interpreted differently. This can cause gpu fault. This
	 *  bug is fixed in latest A510 revision. To enable this bug fix -
	 *  bit[11] of RB_DBG_ECO_CNTL need to be set to 0, default is 1
	 *  (disable). For older A510 version this bit is unused.
	 */
	if (adreno_is_a510(adreno_gpu))
		gpu_rmw(gpu, REG_A5XX_RB_DBG_ECO_CNTL, (1 << 11), 0);

	/* Enable HWCG */
	a5xx_set_hwcg(gpu, true);

	gpu_write(gpu, REG_A5XX_RBBM_AHB_CNTL2, 0x0000003F);

	/* Set the highest bank bit */
	if (adreno_is_a540(adreno_gpu))
		regbit = 2;
	else
		regbit = 1;

	gpu_write(gpu, REG_A5XX_TPL1_MODE_CNTL, regbit << 7);
	gpu_write(gpu, REG_A5XX_RB_MODE_CNTL, regbit << 1);

	if (adreno_is_a509(adreno_gpu) || adreno_is_a512(adreno_gpu) ||
	    adreno_is_a540(adreno_gpu))
		gpu_write(gpu, REG_A5XX_UCHE_DBG_ECO_CNTL_2, regbit);

	/* Disable All flat shading optimization (ALLFLATOPTDIS) */
	gpu_rmw(gpu, REG_A5XX_VPC_DBG_ECO_CNTL, 0, (1 << 10));

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
	gpu_write(gpu, REG_A5XX_CP_PROTECT(15), ADRENO_PROTECT_RW(0xE70, 16));

	/* UCHE */
	gpu_write(gpu, REG_A5XX_CP_PROTECT(16), ADRENO_PROTECT_RW(0xE80, 16));

	/* SMMU */
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

	/* Put the GPU into 64 bit by default */
	gpu_write(gpu, REG_A5XX_CP_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A5XX_VSC_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A5XX_GRAS_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A5XX_RB_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A5XX_PC_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A5XX_HLSQ_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A5XX_VFD_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A5XX_VPC_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A5XX_UCHE_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A5XX_SP_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A5XX_TPL1_ADDR_MODE_CNTL, 0x1);
	gpu_write(gpu, REG_A5XX_RBBM_SECVID_TSB_ADDR_MODE_CNTL, 0x1);

	/*
	 * VPC corner case with local memory load kill leads to corrupt
	 * internal state. Normal Disable does not work for all a5x chips.
	 * So do the following setting to disable it.
	 */
	if (adreno_gpu->info->quirks & ADRENO_QUIRK_LMLOADKILL_DISABLE) {
		gpu_rmw(gpu, REG_A5XX_VPC_DBG_ECO_CNTL, 0, BIT(23));
		gpu_rmw(gpu, REG_A5XX_HLSQ_DBG_ECO_CNTL, BIT(18), 0);
	}

	ret = adreno_hw_init(gpu);
	if (ret)
		return ret;

	if (adreno_is_a530(adreno_gpu) || adreno_is_a540(adreno_gpu))
		a5xx_gpmu_ucode_init(gpu);

	ret = a5xx_ucode_init(gpu);
	if (ret)
		return ret;

	/* Set the ringbuffer address */
	gpu_write64(gpu, REG_A5XX_CP_RB_BASE, REG_A5XX_CP_RB_BASE_HI,
		gpu->rb[0]->iova);

	/*
	 * If the microcode supports the WHERE_AM_I opcode then we can use that
	 * in lieu of the RPTR shadow and enable preemption. Otherwise, we
	 * can't safely use the RPTR shadow or preemption. In either case, the
	 * RPTR shadow should be disabled in hardware.
	 */
	gpu_write(gpu, REG_A5XX_CP_RB_CNTL,
		MSM_GPU_RB_CNTL_DEFAULT | AXXX_CP_RB_CNTL_NO_UPDATE);

	/* Create a privileged buffer for the RPTR shadow */
	if (a5xx_gpu->has_whereami) {
		if (!a5xx_gpu->shadow_bo) {
			a5xx_gpu->shadow = msm_gem_kernel_new(gpu->dev,
				sizeof(u32) * gpu->nr_rings,
				MSM_BO_WC | MSM_BO_MAP_PRIV,
				gpu->aspace, &a5xx_gpu->shadow_bo,
				&a5xx_gpu->shadow_iova);

			if (IS_ERR(a5xx_gpu->shadow))
				return PTR_ERR(a5xx_gpu->shadow);

			msm_gem_object_set_name(a5xx_gpu->shadow_bo, "shadow");
		}

		gpu_write64(gpu, REG_A5XX_CP_RB_RPTR_ADDR,
			REG_A5XX_CP_RB_RPTR_ADDR_HI, shadowptr(a5xx_gpu, gpu->rb[0]));
	} else if (gpu->nr_rings > 1) {
		/* Disable preemption if WHERE_AM_I isn't available */
		a5xx_preempt_fini(gpu);
		gpu->nr_rings = 1;
	}

	a5xx_preempt_hw_init(gpu);

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
		OUT_PKT7(gpu->rb[0], CP_EVENT_WRITE, 1);
		OUT_RING(gpu->rb[0], CP_EVENT_WRITE_0_EVENT(STAT_EVENT));

		a5xx_flush(gpu, gpu->rb[0], true);
		if (!a5xx_idle(gpu, gpu->rb[0]))
			return -EINVAL;
	}

	/*
	 * If the chip that we are using does support loading one, then
	 * try to load a zap shader into the secure world. If successful
	 * we can use the CP to switch out of secure mode. If not then we
	 * have no resource but to try to switch ourselves out manually. If we
	 * guessed wrong then access to the RBBM_SECVID_TRUST_CNTL register will
	 * be blocked and a permissions violation will soon follow.
	 */
	ret = a5xx_zap_shader_init(gpu);
	if (!ret) {
		OUT_PKT7(gpu->rb[0], CP_SET_SECURE_MODE, 1);
		OUT_RING(gpu->rb[0], 0x00000000);

		a5xx_flush(gpu, gpu->rb[0], true);
		if (!a5xx_idle(gpu, gpu->rb[0]))
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
		gpu_write(gpu, REG_A5XX_RBBM_SECVID_TRUST_CNTL, 0x0);
	} else {
		return ret;
	}

	/* Last step - yield the ringbuffer */
	a5xx_preempt_start(gpu);

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

	a5xx_preempt_fini(gpu);

	if (a5xx_gpu->pm4_bo) {
		msm_gem_unpin_iova(a5xx_gpu->pm4_bo, gpu->aspace);
		drm_gem_object_put(a5xx_gpu->pm4_bo);
	}

	if (a5xx_gpu->pfp_bo) {
		msm_gem_unpin_iova(a5xx_gpu->pfp_bo, gpu->aspace);
		drm_gem_object_put(a5xx_gpu->pfp_bo);
	}

	if (a5xx_gpu->gpmu_bo) {
		msm_gem_unpin_iova(a5xx_gpu->gpmu_bo, gpu->aspace);
		drm_gem_object_put(a5xx_gpu->gpmu_bo);
	}

	if (a5xx_gpu->shadow_bo) {
		msm_gem_unpin_iova(a5xx_gpu->shadow_bo, gpu->aspace);
		drm_gem_object_put(a5xx_gpu->shadow_bo);
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

bool a5xx_idle(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);

	if (ring != a5xx_gpu->cur_ring) {
		WARN(1, "Tried to idle a non-current ringbuffer\n");
		return false;
	}

	/* wait for CP to drain ringbuffer: */
	if (!adreno_idle(gpu, ring))
		return false;

	if (spin_until(_a5xx_check_idle(gpu))) {
		DRM_ERROR("%s: %ps: timeout waiting for GPU to idle: status %8.8X irq %8.8X rptr/wptr %d/%d\n",
			gpu->name, __builtin_return_address(0),
			gpu_read(gpu, REG_A5XX_RBBM_STATUS),
			gpu_read(gpu, REG_A5XX_RBBM_INT_0_STATUS),
			gpu_read(gpu, REG_A5XX_CP_RB_RPTR),
			gpu_read(gpu, REG_A5XX_CP_RB_WPTR));
		return false;
	}

	return true;
}

static int a5xx_fault_handler(void *arg, unsigned long iova, int flags, void *data)
{
	struct msm_gpu *gpu = arg;
	pr_warn_ratelimited("*** gpu fault: iova=%08lx, flags=%d (%u,%u,%u,%u)\n",
			iova, flags,
			gpu_read(gpu, REG_A5XX_CP_SCRATCH_REG(4)),
			gpu_read(gpu, REG_A5XX_CP_SCRATCH_REG(5)),
			gpu_read(gpu, REG_A5XX_CP_SCRATCH_REG(6)),
			gpu_read(gpu, REG_A5XX_CP_SCRATCH_REG(7)));

	return 0;
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

static void a5xx_fault_detect_irq(struct msm_gpu *gpu)
{
	struct drm_device *dev = gpu->dev;
	struct msm_ringbuffer *ring = gpu->funcs->active_ring(gpu);

	/*
	 * If stalled on SMMU fault, we could trip the GPU's hang detection,
	 * but the fault handler will trigger the devcore dump, and we want
	 * to otherwise resume normally rather than killing the submit, so
	 * just bail.
	 */
	if (gpu_read(gpu, REG_A5XX_RBBM_STATUS3) & BIT(24))
		return;

	DRM_DEV_ERROR(dev->dev, "gpu fault ring %d fence %x status %8.8X rb %4.4x/%4.4x ib1 %16.16llX/%4.4x ib2 %16.16llX/%4.4x\n",
		ring ? ring->id : -1, ring ? ring->fctx->last_fence : 0,
		gpu_read(gpu, REG_A5XX_RBBM_STATUS),
		gpu_read(gpu, REG_A5XX_CP_RB_RPTR),
		gpu_read(gpu, REG_A5XX_CP_RB_WPTR),
		gpu_read64(gpu, REG_A5XX_CP_IB1_BASE, REG_A5XX_CP_IB1_BASE_HI),
		gpu_read(gpu, REG_A5XX_CP_IB1_BUFSZ),
		gpu_read64(gpu, REG_A5XX_CP_IB2_BASE, REG_A5XX_CP_IB2_BASE_HI),
		gpu_read(gpu, REG_A5XX_CP_IB2_BUFSZ));

	/* Turn off the hangcheck timer to keep it from bothering us */
	del_timer(&gpu->hangcheck_timer);

	kthread_queue_work(gpu->worker, &gpu->recover_work);
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
	struct msm_drm_private *priv = gpu->dev->dev_private;
	u32 status = gpu_read(gpu, REG_A5XX_RBBM_INT_0_STATUS);

	/*
	 * Clear all the interrupts except RBBM_AHB_ERROR - if we clear it
	 * before the source is cleared the interrupt will storm.
	 */
	gpu_write(gpu, REG_A5XX_RBBM_INT_CLEAR_CMD,
		status & ~A5XX_RBBM_INT_0_MASK_RBBM_AHB_ERROR);

	if (priv->disable_err_irq) {
		status &= A5XX_RBBM_INT_0_MASK_CP_CACHE_FLUSH_TS |
			  A5XX_RBBM_INT_0_MASK_CP_SW;
	}

	/* Pass status to a5xx_rbbm_err_irq because we've already cleared it */
	if (status & RBBM_ERROR_MASK)
		a5xx_rbbm_err_irq(gpu, status);

	if (status & A5XX_RBBM_INT_0_MASK_CP_HW_ERROR)
		a5xx_cp_err_irq(gpu);

	if (status & A5XX_RBBM_INT_0_MASK_MISC_HANG_DETECT)
		a5xx_fault_detect_irq(gpu);

	if (status & A5XX_RBBM_INT_0_MASK_UCHE_OOB_ACCESS)
		a5xx_uche_err_irq(gpu);

	if (status & A5XX_RBBM_INT_0_MASK_GPMU_VOLTAGE_DROOP)
		a5xx_gpmu_err_irq(gpu);

	if (status & A5XX_RBBM_INT_0_MASK_CP_CACHE_FLUSH_TS) {
		a5xx_preempt_trigger(gpu);
		msm_gpu_retire(gpu);
	}

	if (status & A5XX_RBBM_INT_0_MASK_CP_SW)
		a5xx_preempt_irq(gpu);

	return IRQ_HANDLED;
}

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
	0xEAA5, 0xEAC2, 0xA800, 0xA800, 0xA820, 0xA828, 0xA840, 0xA87D,
	0XA880, 0xA88D, 0xA890, 0xA8A3, 0xA8D0, 0xA8D8, 0xA8E0, 0xA8F5,
	0xAC60, 0xAC60, ~0,
};

static void a5xx_dump(struct msm_gpu *gpu)
{
	DRM_DEV_INFO(gpu->dev->dev, "status:   %08x\n",
		gpu_read(gpu, REG_A5XX_RBBM_STATUS));
	adreno_dump(gpu);
}

static int a5xx_pm_resume(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	int ret;

	/* Turn on the core power */
	ret = msm_gpu_pm_resume(gpu);
	if (ret)
		return ret;

	/* Adreno 506, 508, 509, 510, 512 needs manual RBBM sus/res control */
	if (!(adreno_is_a530(adreno_gpu) || adreno_is_a540(adreno_gpu))) {
		/* Halt the sp_input_clk at HM level */
		gpu_write(gpu, REG_A5XX_RBBM_CLOCK_CNTL, 0x00000055);
		a5xx_set_hwcg(gpu, true);
		/* Turn on sp_input_clk at HM level */
		gpu_rmw(gpu, REG_A5XX_RBBM_CLOCK_CNTL, 0xff, 0);
		return 0;
	}

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
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);
	u32 mask = 0xf;
	int i, ret;

	/* A506, A508, A510 have 3 XIN ports in VBIF */
	if (adreno_is_a506(adreno_gpu) || adreno_is_a508(adreno_gpu) ||
	    adreno_is_a510(adreno_gpu))
		mask = 0x7;

	/* Clear the VBIF pipe before shutting down */
	gpu_write(gpu, REG_A5XX_VBIF_XIN_HALT_CTRL0, mask);
	spin_until((gpu_read(gpu, REG_A5XX_VBIF_XIN_HALT_CTRL1) &
				mask) == mask);

	gpu_write(gpu, REG_A5XX_VBIF_XIN_HALT_CTRL0, 0);

	/*
	 * Reset the VBIF before power collapse to avoid issue with FIFO
	 * entries on Adreno A510 and A530 (the others will tend to lock up)
	 */
	if (adreno_is_a510(adreno_gpu) || adreno_is_a530(adreno_gpu)) {
		gpu_write(gpu, REG_A5XX_RBBM_BLOCK_SW_RESET_CMD, 0x003C0000);
		gpu_write(gpu, REG_A5XX_RBBM_BLOCK_SW_RESET_CMD, 0x00000000);
	}

	ret = msm_gpu_pm_suspend(gpu);
	if (ret)
		return ret;

	if (a5xx_gpu->has_whereami)
		for (i = 0; i < gpu->nr_rings; i++)
			a5xx_gpu->shadow[i] = 0;

	return 0;
}

static int a5xx_get_timestamp(struct msm_gpu *gpu, uint64_t *value)
{
	*value = gpu_read64(gpu, REG_A5XX_RBBM_ALWAYSON_COUNTER_LO,
		REG_A5XX_RBBM_ALWAYSON_COUNTER_HI);

	return 0;
}

struct a5xx_crashdumper {
	void *ptr;
	struct drm_gem_object *bo;
	u64 iova;
};

struct a5xx_gpu_state {
	struct msm_gpu_state base;
	u32 *hlsqregs;
};

static int a5xx_crashdumper_init(struct msm_gpu *gpu,
		struct a5xx_crashdumper *dumper)
{
	dumper->ptr = msm_gem_kernel_new(gpu->dev,
		SZ_1M, MSM_BO_WC, gpu->aspace,
		&dumper->bo, &dumper->iova);

	if (!IS_ERR(dumper->ptr))
		msm_gem_object_set_name(dumper->bo, "crashdump");

	return PTR_ERR_OR_ZERO(dumper->ptr);
}

static int a5xx_crashdumper_run(struct msm_gpu *gpu,
		struct a5xx_crashdumper *dumper)
{
	u32 val;

	if (IS_ERR_OR_NULL(dumper->ptr))
		return -EINVAL;

	gpu_write64(gpu, REG_A5XX_CP_CRASH_SCRIPT_BASE_LO,
		REG_A5XX_CP_CRASH_SCRIPT_BASE_HI, dumper->iova);

	gpu_write(gpu, REG_A5XX_CP_CRASH_DUMP_CNTL, 1);

	return gpu_poll_timeout(gpu, REG_A5XX_CP_CRASH_DUMP_CNTL, val,
		val & 0x04, 100, 10000);
}

/*
 * These are a list of the registers that need to be read through the HLSQ
 * aperture through the crashdumper.  These are not nominally accessible from
 * the CPU on a secure platform.
 */
static const struct {
	u32 type;
	u32 regoffset;
	u32 count;
} a5xx_hlsq_aperture_regs[] = {
	{ 0x35, 0xe00, 0x32 },   /* HSLQ non-context */
	{ 0x31, 0x2080, 0x1 },   /* HLSQ 2D context 0 */
	{ 0x33, 0x2480, 0x1 },   /* HLSQ 2D context 1 */
	{ 0x32, 0xe780, 0x62 },  /* HLSQ 3D context 0 */
	{ 0x34, 0xef80, 0x62 },  /* HLSQ 3D context 1 */
	{ 0x3f, 0x0ec0, 0x40 },  /* SP non-context */
	{ 0x3d, 0x2040, 0x1 },   /* SP 2D context 0 */
	{ 0x3b, 0x2440, 0x1 },   /* SP 2D context 1 */
	{ 0x3e, 0xe580, 0x170 }, /* SP 3D context 0 */
	{ 0x3c, 0xed80, 0x170 }, /* SP 3D context 1 */
	{ 0x3a, 0x0f00, 0x1c },  /* TP non-context */
	{ 0x38, 0x2000, 0xa },   /* TP 2D context 0 */
	{ 0x36, 0x2400, 0xa },   /* TP 2D context 1 */
	{ 0x39, 0xe700, 0x80 },  /* TP 3D context 0 */
	{ 0x37, 0xef00, 0x80 },  /* TP 3D context 1 */
};

static void a5xx_gpu_state_get_hlsq_regs(struct msm_gpu *gpu,
		struct a5xx_gpu_state *a5xx_state)
{
	struct a5xx_crashdumper dumper = { 0 };
	u32 offset, count = 0;
	u64 *ptr;
	int i;

	if (a5xx_crashdumper_init(gpu, &dumper))
		return;

	/* The script will be written at offset 0 */
	ptr = dumper.ptr;

	/* Start writing the data at offset 256k */
	offset = dumper.iova + (256 * SZ_1K);

	/* Count how many additional registers to get from the HLSQ aperture */
	for (i = 0; i < ARRAY_SIZE(a5xx_hlsq_aperture_regs); i++)
		count += a5xx_hlsq_aperture_regs[i].count;

	a5xx_state->hlsqregs = kcalloc(count, sizeof(u32), GFP_KERNEL);
	if (!a5xx_state->hlsqregs)
		return;

	/* Build the crashdump script */
	for (i = 0; i < ARRAY_SIZE(a5xx_hlsq_aperture_regs); i++) {
		u32 type = a5xx_hlsq_aperture_regs[i].type;
		u32 c = a5xx_hlsq_aperture_regs[i].count;

		/* Write the register to select the desired bank */
		*ptr++ = ((u64) type << 8);
		*ptr++ = (((u64) REG_A5XX_HLSQ_DBG_READ_SEL) << 44) |
			(1 << 21) | 1;

		*ptr++ = offset;
		*ptr++ = (((u64) REG_A5XX_HLSQ_DBG_AHB_READ_APERTURE) << 44)
			| c;

		offset += c * sizeof(u32);
	}

	/* Write two zeros to close off the script */
	*ptr++ = 0;
	*ptr++ = 0;

	if (a5xx_crashdumper_run(gpu, &dumper)) {
		kfree(a5xx_state->hlsqregs);
		msm_gem_kernel_put(dumper.bo, gpu->aspace);
		return;
	}

	/* Copy the data from the crashdumper to the state */
	memcpy(a5xx_state->hlsqregs, dumper.ptr + (256 * SZ_1K),
		count * sizeof(u32));

	msm_gem_kernel_put(dumper.bo, gpu->aspace);
}

static struct msm_gpu_state *a5xx_gpu_state_get(struct msm_gpu *gpu)
{
	struct a5xx_gpu_state *a5xx_state = kzalloc(sizeof(*a5xx_state),
			GFP_KERNEL);
	bool stalled = !!(gpu_read(gpu, REG_A5XX_RBBM_STATUS3) & BIT(24));

	if (!a5xx_state)
		return ERR_PTR(-ENOMEM);

	/* Temporarily disable hardware clock gating before reading the hw */
	a5xx_set_hwcg(gpu, false);

	/* First get the generic state from the adreno core */
	adreno_gpu_state_get(gpu, &(a5xx_state->base));

	a5xx_state->base.rbbm_status = gpu_read(gpu, REG_A5XX_RBBM_STATUS);

	/*
	 * Get the HLSQ regs with the help of the crashdumper, but only if
	 * we are not stalled in an iommu fault (in which case the crashdumper
	 * would not have access to memory)
	 */
	if (!stalled)
		a5xx_gpu_state_get_hlsq_regs(gpu, a5xx_state);

	a5xx_set_hwcg(gpu, true);

	return &a5xx_state->base;
}

static void a5xx_gpu_state_destroy(struct kref *kref)
{
	struct msm_gpu_state *state = container_of(kref,
		struct msm_gpu_state, ref);
	struct a5xx_gpu_state *a5xx_state = container_of(state,
		struct a5xx_gpu_state, base);

	kfree(a5xx_state->hlsqregs);

	adreno_gpu_state_destroy(state);
	kfree(a5xx_state);
}

static int a5xx_gpu_state_put(struct msm_gpu_state *state)
{
	if (IS_ERR_OR_NULL(state))
		return 1;

	return kref_put(&state->ref, a5xx_gpu_state_destroy);
}


#if defined(CONFIG_DEBUG_FS) || defined(CONFIG_DEV_COREDUMP)
static void a5xx_show(struct msm_gpu *gpu, struct msm_gpu_state *state,
		      struct drm_printer *p)
{
	int i, j;
	u32 pos = 0;
	struct a5xx_gpu_state *a5xx_state = container_of(state,
		struct a5xx_gpu_state, base);

	if (IS_ERR_OR_NULL(state))
		return;

	adreno_show(gpu, state, p);

	/* Dump the additional a5xx HLSQ registers */
	if (!a5xx_state->hlsqregs)
		return;

	drm_printf(p, "registers-hlsq:\n");

	for (i = 0; i < ARRAY_SIZE(a5xx_hlsq_aperture_regs); i++) {
		u32 o = a5xx_hlsq_aperture_regs[i].regoffset;
		u32 c = a5xx_hlsq_aperture_regs[i].count;

		for (j = 0; j < c; j++, pos++, o++) {
			/*
			 * To keep the crashdump simple we pull the entire range
			 * for each register type but not all of the registers
			 * in the range are valid. Fortunately invalid registers
			 * stick out like a sore thumb with a value of
			 * 0xdeadbeef
			 */
			if (a5xx_state->hlsqregs[pos] == 0xdeadbeef)
				continue;

			drm_printf(p, "  - { offset: 0x%04x, value: 0x%08x }\n",
				o << 2, a5xx_state->hlsqregs[pos]);
		}
	}
}
#endif

static struct msm_ringbuffer *a5xx_active_ring(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);

	return a5xx_gpu->cur_ring;
}

static u64 a5xx_gpu_busy(struct msm_gpu *gpu, unsigned long *out_sample_rate)
{
	u64 busy_cycles;

	busy_cycles = gpu_read64(gpu, REG_A5XX_RBBM_PERFCTR_RBBM_0_LO,
			REG_A5XX_RBBM_PERFCTR_RBBM_0_HI);
	*out_sample_rate = clk_get_rate(gpu->core_clk);

	return busy_cycles;
}

static uint32_t a5xx_get_rptr(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);

	if (a5xx_gpu->has_whereami)
		return a5xx_gpu->shadow[ring->id];

	return ring->memptrs->rptr = gpu_read(gpu, REG_A5XX_CP_RB_RPTR);
}

static const struct adreno_gpu_funcs funcs = {
	.base = {
		.get_param = adreno_get_param,
		.set_param = adreno_set_param,
		.hw_init = a5xx_hw_init,
		.pm_suspend = a5xx_pm_suspend,
		.pm_resume = a5xx_pm_resume,
		.recover = a5xx_recover,
		.submit = a5xx_submit,
		.active_ring = a5xx_active_ring,
		.irq = a5xx_irq,
		.destroy = a5xx_destroy,
#if defined(CONFIG_DEBUG_FS) || defined(CONFIG_DEV_COREDUMP)
		.show = a5xx_show,
#endif
#if defined(CONFIG_DEBUG_FS)
		.debugfs_init = a5xx_debugfs_init,
#endif
		.gpu_busy = a5xx_gpu_busy,
		.gpu_state_get = a5xx_gpu_state_get,
		.gpu_state_put = a5xx_gpu_state_put,
		.create_address_space = adreno_iommu_create_address_space,
		.get_rptr = a5xx_get_rptr,
	},
	.get_timestamp = a5xx_get_timestamp,
};

static void check_speed_bin(struct device *dev)
{
	struct nvmem_cell *cell;
	u32 val;

	/*
	 * If the OPP table specifies a opp-supported-hw property then we have
	 * to set something with dev_pm_opp_set_supported_hw() or the table
	 * doesn't get populated so pick an arbitrary value that should
	 * ensure the default frequencies are selected but not conflict with any
	 * actual bins
	 */
	val = 0x80;

	cell = nvmem_cell_get(dev, "speed_bin");

	if (!IS_ERR(cell)) {
		void *buf = nvmem_cell_read(cell, NULL);

		if (!IS_ERR(buf)) {
			u8 bin = *((u8 *) buf);

			val = (1 << bin);
			kfree(buf);
		}

		nvmem_cell_put(cell);
	}

	devm_pm_opp_set_supported_hw(dev, &val, 1);
}

struct msm_gpu *a5xx_gpu_init(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = priv->gpu_pdev;
	struct a5xx_gpu *a5xx_gpu = NULL;
	struct adreno_gpu *adreno_gpu;
	struct msm_gpu *gpu;
	int ret;

	if (!pdev) {
		DRM_DEV_ERROR(dev->dev, "No A5XX device is defined\n");
		return ERR_PTR(-ENXIO);
	}

	a5xx_gpu = kzalloc(sizeof(*a5xx_gpu), GFP_KERNEL);
	if (!a5xx_gpu)
		return ERR_PTR(-ENOMEM);

	adreno_gpu = &a5xx_gpu->base;
	gpu = &adreno_gpu->base;

	adreno_gpu->registers = a5xx_registers;

	a5xx_gpu->lm_leakage = 0x4E001A;

	check_speed_bin(&pdev->dev);

	ret = adreno_gpu_init(dev, pdev, adreno_gpu, &funcs, 4);
	if (ret) {
		a5xx_destroy(&(a5xx_gpu->base.base));
		return ERR_PTR(ret);
	}

	if (gpu->aspace)
		msm_mmu_set_fault_handler(gpu->aspace->mmu, gpu, a5xx_fault_handler);

	/* Set up the preemption specific bits and pieces for each ringbuffer */
	a5xx_preempt_init(gpu);

	return gpu;
}
