// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018 The Linux Foundation. All rights reserved. */

#include "a2xx_gpu.h"
#include "msm_gem.h"
#include "msm_mmu.h"

extern bool hang_debug;

static void a2xx_dump(struct msm_gpu *gpu);
static bool a2xx_idle(struct msm_gpu *gpu);

static void a2xx_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit)
{
	struct msm_ringbuffer *ring = submit->ring;
	unsigned int i;

	for (i = 0; i < submit->nr_cmds; i++) {
		switch (submit->cmd[i].type) {
		case MSM_SUBMIT_CMD_IB_TARGET_BUF:
			/* ignore IB-targets */
			break;
		case MSM_SUBMIT_CMD_CTX_RESTORE_BUF:
			/* ignore if there has not been a ctx switch: */
			if (ring->cur_ctx_seqno == submit->queue->ctx->seqno)
				break;
			fallthrough;
		case MSM_SUBMIT_CMD_BUF:
			OUT_PKT3(ring, CP_INDIRECT_BUFFER_PFD, 2);
			OUT_RING(ring, lower_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, submit->cmd[i].size);
			OUT_PKT2(ring);
			break;
		}
	}

	OUT_PKT0(ring, REG_AXXX_CP_SCRATCH_REG2, 1);
	OUT_RING(ring, submit->seqno);

	/* wait for idle before cache flush/interrupt */
	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_EVENT_WRITE, 3);
	OUT_RING(ring, CACHE_FLUSH_TS);
	OUT_RING(ring, rbmemptr(ring, fence));
	OUT_RING(ring, submit->seqno);
	OUT_PKT3(ring, CP_INTERRUPT, 1);
	OUT_RING(ring, 0x80000000);

	adreno_flush(gpu, ring, REG_AXXX_CP_RB_WPTR);
}

static bool a2xx_me_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a2xx_gpu *a2xx_gpu = to_a2xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = gpu->rb[0];

	OUT_PKT3(ring, CP_ME_INIT, 18);

	/* All fields present (bits 9:0) */
	OUT_RING(ring, 0x000003ff);
	/* Disable/Enable Real-Time Stream processing (present but ignored) */
	OUT_RING(ring, 0x00000000);
	/* Enable (2D <-> 3D) implicit synchronization (present but ignored) */
	OUT_RING(ring, 0x00000000);

	OUT_RING(ring, REG_A2XX_RB_SURFACE_INFO - 0x2000);
	OUT_RING(ring, REG_A2XX_PA_SC_WINDOW_OFFSET - 0x2000);
	OUT_RING(ring, REG_A2XX_VGT_MAX_VTX_INDX - 0x2000);
	OUT_RING(ring, REG_A2XX_SQ_PROGRAM_CNTL - 0x2000);
	OUT_RING(ring, REG_A2XX_RB_DEPTHCONTROL - 0x2000);
	OUT_RING(ring, REG_A2XX_PA_SU_POINT_SIZE - 0x2000);
	OUT_RING(ring, REG_A2XX_PA_SC_LINE_CNTL - 0x2000);
	OUT_RING(ring, REG_A2XX_PA_SU_POLY_OFFSET_FRONT_SCALE - 0x2000);

	/* Vertex and Pixel Shader Start Addresses in instructions
	 * (3 DWORDS per instruction) */
	OUT_RING(ring, 0x80000180);
	/* Maximum Contexts */
	OUT_RING(ring, 0x00000001);
	/* Write Confirm Interval and The CP will wait the
	 * wait_interval * 16 clocks between polling  */
	OUT_RING(ring, 0x00000000);
	/* NQ and External Memory Swap */
	OUT_RING(ring, 0x00000000);
	/* protected mode error checking (0x1f2 is REG_AXXX_CP_INT_CNTL) */
	if (a2xx_gpu->protection_disabled)
		OUT_RING(ring, 0x00000000);
	else
		OUT_RING(ring, 0x200001f2);
	/* Disable header dumping and Header dump address */
	OUT_RING(ring, 0x00000000);
	/* Header dump size */
	OUT_RING(ring, 0x00000000);

	if (!a2xx_gpu->protection_disabled) {
		/* enable protected mode */
		OUT_PKT3(ring, CP_SET_PROTECTED_MODE, 1);
		OUT_RING(ring, 1);
	}

	adreno_flush(gpu, ring, REG_AXXX_CP_RB_WPTR);
	return a2xx_idle(gpu);
}

static int a2xx_hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a2xx_gpu *a2xx_gpu = to_a2xx_gpu(adreno_gpu);
	dma_addr_t pt_base, tran_error;
	uint32_t *ptr, len;
	int i, ret;

	a2xx_gpummu_params(gpu->aspace->mmu, &pt_base, &tran_error);

	DBG("%s", gpu->name);

	/* halt ME to avoid ucode upload issues on a20x */
	gpu_write(gpu, REG_AXXX_CP_ME_CNTL, AXXX_CP_ME_CNTL_HALT);

	gpu_write(gpu, REG_A2XX_RBBM_PM_OVERRIDE1, 0xfffffffe);
	gpu_write(gpu, REG_A2XX_RBBM_PM_OVERRIDE2, 0xffffffff);

	/* note: kgsl uses 0x00000001 after first reset on a22x */
	gpu_write(gpu, REG_A2XX_RBBM_SOFT_RESET, 0xffffffff);
	msleep(30);
	gpu_write(gpu, REG_A2XX_RBBM_SOFT_RESET, 0x00000000);

	if (adreno_is_a225(adreno_gpu))
		gpu_write(gpu, REG_A2XX_SQ_FLOW_CONTROL, 0x18000000);

	/* note: kgsl uses 0x0000ffff for a20x */
	gpu_write(gpu, REG_A2XX_RBBM_CNTL, 0x00004442);

	/* MPU: physical range */
	gpu_write(gpu, REG_A2XX_MH_MMU_MPU_BASE, 0x00000000);
	gpu_write(gpu, REG_A2XX_MH_MMU_MPU_END, 0xfffff000);

	gpu_write(gpu, REG_A2XX_MH_MMU_CONFIG, A2XX_MH_MMU_CONFIG_MMU_ENABLE |
		A2XX_MH_MMU_CONFIG_RB_W_CLNT_BEHAVIOR(BEH_TRAN_RNG) |
		A2XX_MH_MMU_CONFIG_CP_W_CLNT_BEHAVIOR(BEH_TRAN_RNG) |
		A2XX_MH_MMU_CONFIG_CP_R0_CLNT_BEHAVIOR(BEH_TRAN_RNG) |
		A2XX_MH_MMU_CONFIG_CP_R1_CLNT_BEHAVIOR(BEH_TRAN_RNG) |
		A2XX_MH_MMU_CONFIG_CP_R2_CLNT_BEHAVIOR(BEH_TRAN_RNG) |
		A2XX_MH_MMU_CONFIG_CP_R3_CLNT_BEHAVIOR(BEH_TRAN_RNG) |
		A2XX_MH_MMU_CONFIG_CP_R4_CLNT_BEHAVIOR(BEH_TRAN_RNG) |
		A2XX_MH_MMU_CONFIG_VGT_R0_CLNT_BEHAVIOR(BEH_TRAN_RNG) |
		A2XX_MH_MMU_CONFIG_VGT_R1_CLNT_BEHAVIOR(BEH_TRAN_RNG) |
		A2XX_MH_MMU_CONFIG_TC_R_CLNT_BEHAVIOR(BEH_TRAN_RNG) |
		A2XX_MH_MMU_CONFIG_PA_W_CLNT_BEHAVIOR(BEH_TRAN_RNG));

	/* same as parameters in adreno_gpu */
	gpu_write(gpu, REG_A2XX_MH_MMU_VA_RANGE, SZ_16M |
		A2XX_MH_MMU_VA_RANGE_NUM_64KB_REGIONS(0xfff));

	gpu_write(gpu, REG_A2XX_MH_MMU_PT_BASE, pt_base);
	gpu_write(gpu, REG_A2XX_MH_MMU_TRAN_ERROR, tran_error);

	gpu_write(gpu, REG_A2XX_MH_MMU_INVALIDATE,
		A2XX_MH_MMU_INVALIDATE_INVALIDATE_ALL |
		A2XX_MH_MMU_INVALIDATE_INVALIDATE_TC);

	gpu_write(gpu, REG_A2XX_MH_ARBITER_CONFIG,
		A2XX_MH_ARBITER_CONFIG_SAME_PAGE_LIMIT(16) |
		A2XX_MH_ARBITER_CONFIG_L1_ARB_ENABLE |
		A2XX_MH_ARBITER_CONFIG_L1_ARB_HOLD_ENABLE |
		A2XX_MH_ARBITER_CONFIG_PAGE_SIZE(1) |
		A2XX_MH_ARBITER_CONFIG_TC_REORDER_ENABLE |
		A2XX_MH_ARBITER_CONFIG_TC_ARB_HOLD_ENABLE |
		A2XX_MH_ARBITER_CONFIG_IN_FLIGHT_LIMIT_ENABLE |
		A2XX_MH_ARBITER_CONFIG_IN_FLIGHT_LIMIT(8) |
		A2XX_MH_ARBITER_CONFIG_CP_CLNT_ENABLE |
		A2XX_MH_ARBITER_CONFIG_VGT_CLNT_ENABLE |
		A2XX_MH_ARBITER_CONFIG_TC_CLNT_ENABLE |
		A2XX_MH_ARBITER_CONFIG_RB_CLNT_ENABLE |
		A2XX_MH_ARBITER_CONFIG_PA_CLNT_ENABLE);
	if (!adreno_is_a20x(adreno_gpu))
		gpu_write(gpu, REG_A2XX_MH_CLNT_INTF_CTRL_CONFIG1, 0x00032f07);

	gpu_write(gpu, REG_A2XX_SQ_VS_PROGRAM, 0x00000000);
	gpu_write(gpu, REG_A2XX_SQ_PS_PROGRAM, 0x00000000);

	gpu_write(gpu, REG_A2XX_RBBM_PM_OVERRIDE1, 0); /* 0x200 for msm8960? */
	gpu_write(gpu, REG_A2XX_RBBM_PM_OVERRIDE2, 0); /* 0x80/0x1a0 for a22x? */

	/* note: gsl doesn't set this */
	gpu_write(gpu, REG_A2XX_RBBM_DEBUG, 0x00080000);

	gpu_write(gpu, REG_A2XX_RBBM_INT_CNTL,
		A2XX_RBBM_INT_CNTL_RDERR_INT_MASK);
	gpu_write(gpu, REG_AXXX_CP_INT_CNTL,
		AXXX_CP_INT_CNTL_T0_PACKET_IN_IB_MASK |
		AXXX_CP_INT_CNTL_OPCODE_ERROR_MASK |
		AXXX_CP_INT_CNTL_PROTECTED_MODE_ERROR_MASK |
		AXXX_CP_INT_CNTL_RESERVED_BIT_ERROR_MASK |
		AXXX_CP_INT_CNTL_IB_ERROR_MASK |
		AXXX_CP_INT_CNTL_IB1_INT_MASK |
		AXXX_CP_INT_CNTL_RB_INT_MASK);
	gpu_write(gpu, REG_A2XX_SQ_INT_CNTL, 0);
	gpu_write(gpu, REG_A2XX_MH_INTERRUPT_MASK,
		A2XX_MH_INTERRUPT_MASK_AXI_READ_ERROR |
		A2XX_MH_INTERRUPT_MASK_AXI_WRITE_ERROR |
		A2XX_MH_INTERRUPT_MASK_MMU_PAGE_FAULT);

	for (i = 3; i <= 5; i++)
		if ((SZ_16K << i) == adreno_gpu->info->gmem)
			break;
	gpu_write(gpu, REG_A2XX_RB_EDRAM_INFO, i);

	ret = adreno_hw_init(gpu);
	if (ret)
		return ret;

	gpu_write(gpu, REG_AXXX_CP_RB_CNTL,
		MSM_GPU_RB_CNTL_DEFAULT | AXXX_CP_RB_CNTL_NO_UPDATE);

	gpu_write(gpu, REG_AXXX_CP_RB_BASE, lower_32_bits(gpu->rb[0]->iova));

	/* NOTE: PM4/micro-engine firmware registers look to be the same
	 * for a2xx and a3xx.. we could possibly push that part down to
	 * adreno_gpu base class.  Or push both PM4 and PFP but
	 * parameterize the pfp ucode addr/data registers..
	 */

	/* Load PM4: */
	ptr = (uint32_t *)(adreno_gpu->fw[ADRENO_FW_PM4]->data);
	len = adreno_gpu->fw[ADRENO_FW_PM4]->size / 4;
	DBG("loading PM4 ucode version: %x", ptr[1]);

	/*
	 * New firmware files seem to have GPU and firmware version in this
	 * word (0x20xxxx for A200, 0x220xxx for A220, 0x225xxx for A225).
	 * Older firmware files, which lack protection support, have 0 instead.
	 */
	if (ptr[1] == 0) {
		dev_warn(gpu->dev->dev,
			 "Legacy firmware detected, disabling protection support\n");
		a2xx_gpu->protection_disabled = true;
	}

	gpu_write(gpu, REG_AXXX_CP_DEBUG,
			AXXX_CP_DEBUG_MIU_128BIT_WRITE_ENABLE);
	gpu_write(gpu, REG_AXXX_CP_ME_RAM_WADDR, 0);
	for (i = 1; i < len; i++)
		gpu_write(gpu, REG_AXXX_CP_ME_RAM_DATA, ptr[i]);

	/* Load PFP: */
	ptr = (uint32_t *)(adreno_gpu->fw[ADRENO_FW_PFP]->data);
	len = adreno_gpu->fw[ADRENO_FW_PFP]->size / 4;
	DBG("loading PFP ucode version: %x", ptr[5]);

	gpu_write(gpu, REG_A2XX_CP_PFP_UCODE_ADDR, 0);
	for (i = 1; i < len; i++)
		gpu_write(gpu, REG_A2XX_CP_PFP_UCODE_DATA, ptr[i]);

	gpu_write(gpu, REG_AXXX_CP_QUEUE_THRESHOLDS, 0x000C0804);

	/* clear ME_HALT to start micro engine */
	gpu_write(gpu, REG_AXXX_CP_ME_CNTL, 0);

	return a2xx_me_init(gpu) ? 0 : -EINVAL;
}

static void a2xx_recover(struct msm_gpu *gpu)
{
	int i;

	adreno_dump_info(gpu);

	for (i = 0; i < 8; i++) {
		printk("CP_SCRATCH_REG%d: %u\n", i,
			gpu_read(gpu, REG_AXXX_CP_SCRATCH_REG0 + i));
	}

	/* dump registers before resetting gpu, if enabled: */
	if (hang_debug)
		a2xx_dump(gpu);

	gpu_write(gpu, REG_A2XX_RBBM_SOFT_RESET, 1);
	gpu_read(gpu, REG_A2XX_RBBM_SOFT_RESET);
	gpu_write(gpu, REG_A2XX_RBBM_SOFT_RESET, 0);
	adreno_recover(gpu);
}

static void a2xx_destroy(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a2xx_gpu *a2xx_gpu = to_a2xx_gpu(adreno_gpu);

	DBG("%s", gpu->name);

	adreno_gpu_cleanup(adreno_gpu);

	kfree(a2xx_gpu);
}

static bool a2xx_idle(struct msm_gpu *gpu)
{
	/* wait for ringbuffer to drain: */
	if (!adreno_idle(gpu, gpu->rb[0]))
		return false;

	/* then wait for GPU to finish: */
	if (spin_until(!(gpu_read(gpu, REG_A2XX_RBBM_STATUS) &
			A2XX_RBBM_STATUS_GUI_ACTIVE))) {
		DRM_ERROR("%s: timeout waiting for GPU to idle!\n", gpu->name);

		/* TODO maybe we need to reset GPU here to recover from hang? */
		return false;
	}

	return true;
}

static irqreturn_t a2xx_irq(struct msm_gpu *gpu)
{
	uint32_t mstatus, status;

	mstatus = gpu_read(gpu, REG_A2XX_MASTER_INT_SIGNAL);

	if (mstatus & A2XX_MASTER_INT_SIGNAL_MH_INT_STAT) {
		status = gpu_read(gpu, REG_A2XX_MH_INTERRUPT_STATUS);

		dev_warn(gpu->dev->dev, "MH_INT: %08X\n", status);
		dev_warn(gpu->dev->dev, "MMU_PAGE_FAULT: %08X\n",
			gpu_read(gpu, REG_A2XX_MH_MMU_PAGE_FAULT));

		gpu_write(gpu, REG_A2XX_MH_INTERRUPT_CLEAR, status);
	}

	if (mstatus & A2XX_MASTER_INT_SIGNAL_CP_INT_STAT) {
		status = gpu_read(gpu, REG_AXXX_CP_INT_STATUS);

		/* only RB_INT is expected */
		if (status & ~AXXX_CP_INT_CNTL_RB_INT_MASK)
			dev_warn(gpu->dev->dev, "CP_INT: %08X\n", status);

		gpu_write(gpu, REG_AXXX_CP_INT_ACK, status);
	}

	if (mstatus & A2XX_MASTER_INT_SIGNAL_RBBM_INT_STAT) {
		status = gpu_read(gpu, REG_A2XX_RBBM_INT_STATUS);

		dev_warn(gpu->dev->dev, "RBBM_INT: %08X\n", status);

		gpu_write(gpu, REG_A2XX_RBBM_INT_ACK, status);
	}

	msm_gpu_retire(gpu);

	return IRQ_HANDLED;
}

static const unsigned int a200_registers[] = {
	0x0000, 0x0002, 0x0004, 0x000B, 0x003B, 0x003D, 0x0040, 0x0044,
	0x0046, 0x0047, 0x01C0, 0x01C1, 0x01C3, 0x01C8, 0x01D5, 0x01D9,
	0x01DC, 0x01DD, 0x01EA, 0x01EA, 0x01EE, 0x01F3, 0x01F6, 0x01F7,
	0x01FC, 0x01FF, 0x0391, 0x0392, 0x039B, 0x039E, 0x03B2, 0x03B5,
	0x03B7, 0x03B7, 0x03F8, 0x03FB, 0x0440, 0x0440, 0x0443, 0x0444,
	0x044B, 0x044B, 0x044D, 0x044F, 0x0452, 0x0452, 0x0454, 0x045B,
	0x047F, 0x047F, 0x0578, 0x0587, 0x05C9, 0x05C9, 0x05D0, 0x05D0,
	0x0601, 0x0604, 0x0606, 0x0609, 0x060B, 0x060E, 0x0613, 0x0614,
	0x0A29, 0x0A2B, 0x0A2F, 0x0A31, 0x0A40, 0x0A43, 0x0A45, 0x0A45,
	0x0A4E, 0x0A4F, 0x0C2C, 0x0C2C, 0x0C30, 0x0C30, 0x0C38, 0x0C3C,
	0x0C40, 0x0C40, 0x0C44, 0x0C44, 0x0C80, 0x0C86, 0x0C88, 0x0C94,
	0x0C99, 0x0C9A, 0x0CA4, 0x0CA5, 0x0D00, 0x0D03, 0x0D06, 0x0D06,
	0x0D08, 0x0D0B, 0x0D34, 0x0D35, 0x0DAE, 0x0DC1, 0x0DC8, 0x0DD4,
	0x0DD8, 0x0DD9, 0x0E00, 0x0E00, 0x0E02, 0x0E04, 0x0E17, 0x0E1E,
	0x0EC0, 0x0EC9, 0x0ECB, 0x0ECC, 0x0ED0, 0x0ED0, 0x0ED4, 0x0ED7,
	0x0EE0, 0x0EE2, 0x0F01, 0x0F02, 0x0F0C, 0x0F0C, 0x0F0E, 0x0F12,
	0x0F26, 0x0F2A, 0x0F2C, 0x0F2C, 0x2000, 0x2002, 0x2006, 0x200F,
	0x2080, 0x2082, 0x2100, 0x2109, 0x210C, 0x2114, 0x2180, 0x2184,
	0x21F5, 0x21F7, 0x2200, 0x2208, 0x2280, 0x2283, 0x2293, 0x2294,
	0x2300, 0x2308, 0x2312, 0x2312, 0x2316, 0x231D, 0x2324, 0x2326,
	0x2380, 0x2383, 0x2400, 0x2402, 0x2406, 0x240F, 0x2480, 0x2482,
	0x2500, 0x2509, 0x250C, 0x2514, 0x2580, 0x2584, 0x25F5, 0x25F7,
	0x2600, 0x2608, 0x2680, 0x2683, 0x2693, 0x2694, 0x2700, 0x2708,
	0x2712, 0x2712, 0x2716, 0x271D, 0x2724, 0x2726, 0x2780, 0x2783,
	0x4000, 0x4003, 0x4800, 0x4805, 0x4900, 0x4900, 0x4908, 0x4908,
	~0   /* sentinel */
};

static const unsigned int a220_registers[] = {
	0x0000, 0x0002, 0x0004, 0x000B, 0x003B, 0x003D, 0x0040, 0x0044,
	0x0046, 0x0047, 0x01C0, 0x01C1, 0x01C3, 0x01C8, 0x01D5, 0x01D9,
	0x01DC, 0x01DD, 0x01EA, 0x01EA, 0x01EE, 0x01F3, 0x01F6, 0x01F7,
	0x01FC, 0x01FF, 0x0391, 0x0392, 0x039B, 0x039E, 0x03B2, 0x03B5,
	0x03B7, 0x03B7, 0x03F8, 0x03FB, 0x0440, 0x0440, 0x0443, 0x0444,
	0x044B, 0x044B, 0x044D, 0x044F, 0x0452, 0x0452, 0x0454, 0x045B,
	0x047F, 0x047F, 0x0578, 0x0587, 0x05C9, 0x05C9, 0x05D0, 0x05D0,
	0x0601, 0x0604, 0x0606, 0x0609, 0x060B, 0x060E, 0x0613, 0x0614,
	0x0A29, 0x0A2B, 0x0A2F, 0x0A31, 0x0A40, 0x0A40, 0x0A42, 0x0A43,
	0x0A45, 0x0A45, 0x0A4E, 0x0A4F, 0x0C30, 0x0C30, 0x0C38, 0x0C39,
	0x0C3C, 0x0C3C, 0x0C80, 0x0C81, 0x0C88, 0x0C93, 0x0D00, 0x0D03,
	0x0D05, 0x0D06, 0x0D08, 0x0D0B, 0x0D34, 0x0D35, 0x0DAE, 0x0DC1,
	0x0DC8, 0x0DD4, 0x0DD8, 0x0DD9, 0x0E00, 0x0E00, 0x0E02, 0x0E04,
	0x0E17, 0x0E1E, 0x0EC0, 0x0EC9, 0x0ECB, 0x0ECC, 0x0ED0, 0x0ED0,
	0x0ED4, 0x0ED7, 0x0EE0, 0x0EE2, 0x0F01, 0x0F02, 0x2000, 0x2002,
	0x2006, 0x200F, 0x2080, 0x2082, 0x2100, 0x2102, 0x2104, 0x2109,
	0x210C, 0x2114, 0x2180, 0x2184, 0x21F5, 0x21F7, 0x2200, 0x2202,
	0x2204, 0x2204, 0x2208, 0x2208, 0x2280, 0x2282, 0x2294, 0x2294,
	0x2300, 0x2308, 0x2309, 0x230A, 0x2312, 0x2312, 0x2316, 0x2316,
	0x2318, 0x231D, 0x2324, 0x2326, 0x2380, 0x2383, 0x2400, 0x2402,
	0x2406, 0x240F, 0x2480, 0x2482, 0x2500, 0x2502, 0x2504, 0x2509,
	0x250C, 0x2514, 0x2580, 0x2584, 0x25F5, 0x25F7, 0x2600, 0x2602,
	0x2604, 0x2606, 0x2608, 0x2608, 0x2680, 0x2682, 0x2694, 0x2694,
	0x2700, 0x2708, 0x2712, 0x2712, 0x2716, 0x2716, 0x2718, 0x271D,
	0x2724, 0x2726, 0x2780, 0x2783, 0x4000, 0x4003, 0x4800, 0x4805,
	0x4900, 0x4900, 0x4908, 0x4908,
	~0   /* sentinel */
};

static const unsigned int a225_registers[] = {
	0x0000, 0x0002, 0x0004, 0x000B, 0x003B, 0x003D, 0x0040, 0x0044,
	0x0046, 0x0047, 0x013C, 0x013C, 0x0140, 0x014F, 0x01C0, 0x01C1,
	0x01C3, 0x01C8, 0x01D5, 0x01D9, 0x01DC, 0x01DD, 0x01EA, 0x01EA,
	0x01EE, 0x01F3, 0x01F6, 0x01F7, 0x01FC, 0x01FF, 0x0391, 0x0392,
	0x039B, 0x039E, 0x03B2, 0x03B5, 0x03B7, 0x03B7, 0x03F8, 0x03FB,
	0x0440, 0x0440, 0x0443, 0x0444, 0x044B, 0x044B, 0x044D, 0x044F,
	0x0452, 0x0452, 0x0454, 0x045B, 0x047F, 0x047F, 0x0578, 0x0587,
	0x05C9, 0x05C9, 0x05D0, 0x05D0, 0x0601, 0x0604, 0x0606, 0x0609,
	0x060B, 0x060E, 0x0613, 0x0614, 0x0A29, 0x0A2B, 0x0A2F, 0x0A31,
	0x0A40, 0x0A40, 0x0A42, 0x0A43, 0x0A45, 0x0A45, 0x0A4E, 0x0A4F,
	0x0C01, 0x0C1D, 0x0C30, 0x0C30, 0x0C38, 0x0C39, 0x0C3C, 0x0C3C,
	0x0C80, 0x0C81, 0x0C88, 0x0C93, 0x0D00, 0x0D03, 0x0D05, 0x0D06,
	0x0D08, 0x0D0B, 0x0D34, 0x0D35, 0x0DAE, 0x0DC1, 0x0DC8, 0x0DD4,
	0x0DD8, 0x0DD9, 0x0E00, 0x0E00, 0x0E02, 0x0E04, 0x0E17, 0x0E1E,
	0x0EC0, 0x0EC9, 0x0ECB, 0x0ECC, 0x0ED0, 0x0ED0, 0x0ED4, 0x0ED7,
	0x0EE0, 0x0EE2, 0x0F01, 0x0F02, 0x2000, 0x200F, 0x2080, 0x2082,
	0x2100, 0x2109, 0x210C, 0x2114, 0x2180, 0x2184, 0x21F5, 0x21F7,
	0x2200, 0x2202, 0x2204, 0x2206, 0x2208, 0x2210, 0x2220, 0x2222,
	0x2280, 0x2282, 0x2294, 0x2294, 0x2297, 0x2297, 0x2300, 0x230A,
	0x2312, 0x2312, 0x2315, 0x2316, 0x2318, 0x231D, 0x2324, 0x2326,
	0x2340, 0x2357, 0x2360, 0x2360, 0x2380, 0x2383, 0x2400, 0x240F,
	0x2480, 0x2482, 0x2500, 0x2509, 0x250C, 0x2514, 0x2580, 0x2584,
	0x25F5, 0x25F7, 0x2600, 0x2602, 0x2604, 0x2606, 0x2608, 0x2610,
	0x2620, 0x2622, 0x2680, 0x2682, 0x2694, 0x2694, 0x2697, 0x2697,
	0x2700, 0x270A, 0x2712, 0x2712, 0x2715, 0x2716, 0x2718, 0x271D,
	0x2724, 0x2726, 0x2740, 0x2757, 0x2760, 0x2760, 0x2780, 0x2783,
	0x4000, 0x4003, 0x4800, 0x4806, 0x4808, 0x4808, 0x4900, 0x4900,
	0x4908, 0x4908,
	~0   /* sentinel */
};

/* would be nice to not have to duplicate the _show() stuff with printk(): */
static void a2xx_dump(struct msm_gpu *gpu)
{
	printk("status:   %08x\n",
			gpu_read(gpu, REG_A2XX_RBBM_STATUS));
	adreno_dump(gpu);
}

static struct msm_gpu_state *a2xx_gpu_state_get(struct msm_gpu *gpu)
{
	struct msm_gpu_state *state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (!state)
		return ERR_PTR(-ENOMEM);

	adreno_gpu_state_get(gpu, state);

	state->rbbm_status = gpu_read(gpu, REG_A2XX_RBBM_STATUS);

	return state;
}

static struct msm_gem_address_space *
a2xx_create_address_space(struct msm_gpu *gpu, struct platform_device *pdev)
{
	struct msm_mmu *mmu = a2xx_gpummu_new(&pdev->dev, gpu);
	struct msm_gem_address_space *aspace;

	aspace = msm_gem_address_space_create(mmu, "gpu", SZ_16M,
		0xfff * SZ_64K);

	if (IS_ERR(aspace) && !IS_ERR(mmu))
		mmu->funcs->destroy(mmu);

	return aspace;
}

static u32 a2xx_get_rptr(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	ring->memptrs->rptr = gpu_read(gpu, REG_AXXX_CP_RB_RPTR);
	return ring->memptrs->rptr;
}

static const struct adreno_gpu_funcs funcs = {
	.base = {
		.get_param = adreno_get_param,
		.set_param = adreno_set_param,
		.hw_init = a2xx_hw_init,
		.pm_suspend = msm_gpu_pm_suspend,
		.pm_resume = msm_gpu_pm_resume,
		.recover = a2xx_recover,
		.submit = a2xx_submit,
		.active_ring = adreno_active_ring,
		.irq = a2xx_irq,
		.destroy = a2xx_destroy,
#if defined(CONFIG_DEBUG_FS) || defined(CONFIG_DEV_COREDUMP)
		.show = adreno_show,
#endif
		.gpu_state_get = a2xx_gpu_state_get,
		.gpu_state_put = adreno_gpu_state_put,
		.create_address_space = a2xx_create_address_space,
		.get_rptr = a2xx_get_rptr,
	},
};

static const struct msm_gpu_perfcntr perfcntrs[] = {
/* TODO */
};

struct msm_gpu *a2xx_gpu_init(struct drm_device *dev)
{
	struct a2xx_gpu *a2xx_gpu = NULL;
	struct adreno_gpu *adreno_gpu;
	struct msm_gpu *gpu;
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = priv->gpu_pdev;
	int ret;

	if (!pdev) {
		dev_err(dev->dev, "no a2xx device\n");
		ret = -ENXIO;
		goto fail;
	}

	a2xx_gpu = kzalloc(sizeof(*a2xx_gpu), GFP_KERNEL);
	if (!a2xx_gpu) {
		ret = -ENOMEM;
		goto fail;
	}

	adreno_gpu = &a2xx_gpu->base;
	gpu = &adreno_gpu->base;

	gpu->perfcntrs = perfcntrs;
	gpu->num_perfcntrs = ARRAY_SIZE(perfcntrs);

	ret = adreno_gpu_init(dev, pdev, adreno_gpu, &funcs, 1);
	if (ret)
		goto fail;

	if (adreno_is_a20x(adreno_gpu))
		adreno_gpu->registers = a200_registers;
	else if (adreno_is_a225(adreno_gpu))
		adreno_gpu->registers = a225_registers;
	else
		adreno_gpu->registers = a220_registers;

	if (!gpu->aspace) {
		dev_err(dev->dev, "No memory protection without MMU\n");
		if (!allow_vram_carveout) {
			ret = -ENXIO;
			goto fail;
		}
	}

	return gpu;

fail:
	if (a2xx_gpu)
		a2xx_destroy(&a2xx_gpu->base.base);

	return ERR_PTR(ret);
}
