/* Copyright (c) 2014 The Linux Foundation. All rights reserved.
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
#include "a4xx_gpu.h"
#ifdef CONFIG_MSM_OCMEM
#  include <soc/qcom/ocmem.h>
#endif

#define A4XX_INT0_MASK \
	(A4XX_INT0_RBBM_AHB_ERROR |        \
	 A4XX_INT0_RBBM_ATB_BUS_OVERFLOW | \
	 A4XX_INT0_CP_T0_PACKET_IN_IB |    \
	 A4XX_INT0_CP_OPCODE_ERROR |       \
	 A4XX_INT0_CP_RESERVED_BIT_ERROR | \
	 A4XX_INT0_CP_HW_FAULT |           \
	 A4XX_INT0_CP_IB1_INT |            \
	 A4XX_INT0_CP_IB2_INT |            \
	 A4XX_INT0_CP_RB_INT |             \
	 A4XX_INT0_CP_REG_PROTECT_FAULT |  \
	 A4XX_INT0_CP_AHB_ERROR_HALT |     \
	 A4XX_INT0_UCHE_OOB_ACCESS)

extern bool hang_debug;
static void a4xx_dump(struct msm_gpu *gpu);

/*
 * a4xx_enable_hwcg() - Program the clock control registers
 * @device: The adreno device pointer
 */
static void a4xx_enable_hwcg(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	unsigned int i;
	for (i = 0; i < 4; i++)
		gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL_TP(i), 0x02222202);
	for (i = 0; i < 4; i++)
		gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL2_TP(i), 0x00002222);
	for (i = 0; i < 4; i++)
		gpu_write(gpu, REG_A4XX_RBBM_CLOCK_HYST_TP(i), 0x0E739CE7);
	for (i = 0; i < 4; i++)
		gpu_write(gpu, REG_A4XX_RBBM_CLOCK_DELAY_TP(i), 0x00111111);
	for (i = 0; i < 4; i++)
		gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL_SP(i), 0x22222222);
	for (i = 0; i < 4; i++)
		gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL2_SP(i), 0x00222222);
	for (i = 0; i < 4; i++)
		gpu_write(gpu, REG_A4XX_RBBM_CLOCK_HYST_SP(i), 0x00000104);
	for (i = 0; i < 4; i++)
		gpu_write(gpu, REG_A4XX_RBBM_CLOCK_DELAY_SP(i), 0x00000081);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL_UCHE, 0x22222222);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL2_UCHE, 0x02222222);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL3_UCHE, 0x00000000);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL4_UCHE, 0x00000000);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_HYST_UCHE, 0x00004444);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_DELAY_UCHE, 0x00001112);
	for (i = 0; i < 4; i++)
		gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL_RB(i), 0x22222222);

	/* Disable L1 clocking in A420 due to CCU issues with it */
	for (i = 0; i < 4; i++) {
		if (adreno_is_a420(adreno_gpu)) {
			gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL2_RB(i),
					0x00002020);
		} else {
			gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL2_RB(i),
					0x00022020);
		}
	}

	for (i = 0; i < 4; i++) {
		gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL_MARB_CCU(i),
				0x00000922);
	}

	for (i = 0; i < 4; i++) {
		gpu_write(gpu, REG_A4XX_RBBM_CLOCK_HYST_RB_MARB_CCU(i),
				0x00000000);
	}

	for (i = 0; i < 4; i++) {
		gpu_write(gpu, REG_A4XX_RBBM_CLOCK_DELAY_RB_MARB_CCU_L1(i),
				0x00000001);
	}

	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_MODE_GPC, 0x02222222);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_HYST_GPC, 0x04100104);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_DELAY_GPC, 0x00022222);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL_COM_DCOM, 0x00000022);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_HYST_COM_DCOM, 0x0000010F);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_DELAY_COM_DCOM, 0x00000022);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL_TSE_RAS_RBBM, 0x00222222);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_HYST_TSE_RAS_RBBM, 0x00004104);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_DELAY_TSE_RAS_RBBM, 0x00000222);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL_HLSQ , 0x00000000);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_HYST_HLSQ, 0x00000000);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_DELAY_HLSQ, 0x00020000);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL, 0xAAAAAAAA);
	gpu_write(gpu, REG_A4XX_RBBM_CLOCK_CTL2, 0);
}

static void a4xx_me_init(struct msm_gpu *gpu)
{
	struct msm_ringbuffer *ring = gpu->rb;

	OUT_PKT3(ring, CP_ME_INIT, 17);
	OUT_RING(ring, 0x000003f7);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000080);
	OUT_RING(ring, 0x00000100);
	OUT_RING(ring, 0x00000180);
	OUT_RING(ring, 0x00006600);
	OUT_RING(ring, 0x00000150);
	OUT_RING(ring, 0x0000014e);
	OUT_RING(ring, 0x00000154);
	OUT_RING(ring, 0x00000001);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);

	gpu->funcs->flush(gpu);
	gpu->funcs->idle(gpu);
}

static int a4xx_hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a4xx_gpu *a4xx_gpu = to_a4xx_gpu(adreno_gpu);
	uint32_t *ptr, len;
	int i, ret;

	if (adreno_is_a4xx(adreno_gpu)) {
		gpu_write(gpu, REG_A4XX_VBIF_ABIT_SORT, 0x0001001F);
		gpu_write(gpu, REG_A4XX_VBIF_ABIT_SORT_CONF, 0x000000A4);
		gpu_write(gpu, REG_A4XX_VBIF_GATE_OFF_WRREQ_EN, 0x00000001);
		gpu_write(gpu, REG_A4XX_VBIF_IN_RD_LIM_CONF0, 0x18181818);
		gpu_write(gpu, REG_A4XX_VBIF_IN_RD_LIM_CONF1, 0x00000018);
		gpu_write(gpu, REG_A4XX_VBIF_IN_WR_LIM_CONF0, 0x18181818);
		gpu_write(gpu, REG_A4XX_VBIF_IN_WR_LIM_CONF1, 0x00000018);
		gpu_write(gpu, REG_A4XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x00000003);
	} else {
		BUG();
	}

	/* Make all blocks contribute to the GPU BUSY perf counter */
	gpu_write(gpu, REG_A4XX_RBBM_GPU_BUSY_MASKED, 0xffffffff);

	/* Tune the hystersis counters for SP and CP idle detection */
	gpu_write(gpu, REG_A4XX_RBBM_SP_HYST_CNT, 0x10);
	gpu_write(gpu, REG_A4XX_RBBM_WAIT_IDLE_CLOCKS_CTL, 0x10);

	 /* Enable the RBBM error reporting bits */
	gpu_write(gpu, REG_A4XX_RBBM_AHB_CTL0, 0x00000001);

	/* Enable AHB error reporting*/
	gpu_write(gpu, REG_A4XX_RBBM_AHB_CTL1, 0xa6ffffff);

	/* Enable power counters*/
	gpu_write(gpu, REG_A4XX_RBBM_RBBM_CTL, 0x00000030);

	/*
	 * Turn on hang detection - this spews a lot of useful information
	 * into the RBBM registers on a hang:
	 */
	gpu_write(gpu, REG_A4XX_RBBM_INTERFACE_HANG_INT_CTL,
			(1 << 30) | 0xFFFF);

	gpu_write(gpu, REG_A4XX_RB_GMEM_BASE_ADDR,
			(unsigned int)(a4xx_gpu->ocmem_base >> 14));

	/* Turn on performance counters: */
	gpu_write(gpu, REG_A4XX_RBBM_PERFCTR_CTL, 0x01);

	/* Disable L2 bypass to avoid UCHE out of bounds errors */
	gpu_write(gpu, REG_A4XX_UCHE_TRAP_BASE_LO, 0xffff0000);
	gpu_write(gpu, REG_A4XX_UCHE_TRAP_BASE_HI, 0xffff0000);

	gpu_write(gpu, REG_A4XX_CP_DEBUG, (1 << 25) |
			(adreno_is_a420(adreno_gpu) ? (1 << 29) : 0));

	a4xx_enable_hwcg(gpu);

	/*
	 * For A420 set RBBM_CLOCK_DELAY_HLSQ.CGC_HLSQ_TP_EARLY_CYC >= 2
	 * due to timing issue with HLSQ_TP_CLK_EN
	 */
	if (adreno_is_a420(adreno_gpu)) {
		unsigned int val;
		val = gpu_read(gpu, REG_A4XX_RBBM_CLOCK_DELAY_HLSQ);
		val &= ~A4XX_CGC_HLSQ_EARLY_CYC__MASK;
		val |= 2 << A4XX_CGC_HLSQ_EARLY_CYC__SHIFT;
		gpu_write(gpu, REG_A4XX_RBBM_CLOCK_DELAY_HLSQ, val);
	}

	ret = adreno_hw_init(gpu);
	if (ret)
		return ret;

	/* setup access protection: */
	gpu_write(gpu, REG_A4XX_CP_PROTECT_CTRL, 0x00000007);

	/* RBBM registers */
	gpu_write(gpu, REG_A4XX_CP_PROTECT(0), 0x62000010);
	gpu_write(gpu, REG_A4XX_CP_PROTECT(1), 0x63000020);
	gpu_write(gpu, REG_A4XX_CP_PROTECT(2), 0x64000040);
	gpu_write(gpu, REG_A4XX_CP_PROTECT(3), 0x65000080);
	gpu_write(gpu, REG_A4XX_CP_PROTECT(4), 0x66000100);
	gpu_write(gpu, REG_A4XX_CP_PROTECT(5), 0x64000200);

	/* CP registers */
	gpu_write(gpu, REG_A4XX_CP_PROTECT(6), 0x67000800);
	gpu_write(gpu, REG_A4XX_CP_PROTECT(7), 0x64001600);


	/* RB registers */
	gpu_write(gpu, REG_A4XX_CP_PROTECT(8), 0x60003300);

	/* HLSQ registers */
	gpu_write(gpu, REG_A4XX_CP_PROTECT(9), 0x60003800);

	/* VPC registers */
	gpu_write(gpu, REG_A4XX_CP_PROTECT(10), 0x61003980);

	/* SMMU registers */
	gpu_write(gpu, REG_A4XX_CP_PROTECT(11), 0x6e010000);

	gpu_write(gpu, REG_A4XX_RBBM_INT_0_MASK, A4XX_INT0_MASK);

	ret = adreno_hw_init(gpu);
	if (ret)
		return ret;

	/* Load PM4: */
	ptr = (uint32_t *)(adreno_gpu->pm4->data);
	len = adreno_gpu->pm4->size / 4;
	DBG("loading PM4 ucode version: %u", ptr[0]);
	gpu_write(gpu, REG_A4XX_CP_ME_RAM_WADDR, 0);
	for (i = 1; i < len; i++)
		gpu_write(gpu, REG_A4XX_CP_ME_RAM_DATA, ptr[i]);

	/* Load PFP: */
	ptr = (uint32_t *)(adreno_gpu->pfp->data);
	len = adreno_gpu->pfp->size / 4;
	DBG("loading PFP ucode version: %u", ptr[0]);

	gpu_write(gpu, REG_A4XX_CP_PFP_UCODE_ADDR, 0);
	for (i = 1; i < len; i++)
		gpu_write(gpu, REG_A4XX_CP_PFP_UCODE_DATA, ptr[i]);

	/* clear ME_HALT to start micro engine */
	gpu_write(gpu, REG_A4XX_CP_ME_CNTL, 0);

	a4xx_me_init(gpu);
	return 0;
}

static void a4xx_recover(struct msm_gpu *gpu)
{
	/* dump registers before resetting gpu, if enabled: */
	if (hang_debug)
		a4xx_dump(gpu);

	gpu_write(gpu, REG_A4XX_RBBM_SW_RESET_CMD, 1);
	gpu_read(gpu, REG_A4XX_RBBM_SW_RESET_CMD);
	gpu_write(gpu, REG_A4XX_RBBM_SW_RESET_CMD, 0);
	adreno_recover(gpu);
}

static void a4xx_destroy(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a4xx_gpu *a4xx_gpu = to_a4xx_gpu(adreno_gpu);

	DBG("%s", gpu->name);

	adreno_gpu_cleanup(adreno_gpu);

#ifdef CONFIG_MSM_OCMEM
	if (a4xx_gpu->ocmem_base)
		ocmem_free(OCMEM_GRAPHICS, a4xx_gpu->ocmem_hdl);
#endif

	kfree(a4xx_gpu);
}

static void a4xx_idle(struct msm_gpu *gpu)
{
	/* wait for ringbuffer to drain: */
	adreno_idle(gpu);

	/* then wait for GPU to finish: */
	if (spin_until(!(gpu_read(gpu, REG_A4XX_RBBM_STATUS) &
					A4XX_RBBM_STATUS_GPU_BUSY)))
		DRM_ERROR("%s: timeout waiting for GPU to idle!\n", gpu->name);

	/* TODO maybe we need to reset GPU here to recover from hang? */
}

static irqreturn_t a4xx_irq(struct msm_gpu *gpu)
{
	uint32_t status;

	status = gpu_read(gpu, REG_A4XX_RBBM_INT_0_STATUS);
	DBG("%s: Int status %08x", gpu->name, status);

	gpu_write(gpu, REG_A4XX_RBBM_INT_CLEAR_CMD, status);

	msm_gpu_retire(gpu);

	return IRQ_HANDLED;
}

static const unsigned int a4xx_registers[] = {
	/* RBBM */
	0x0000, 0x0002, 0x0004, 0x0021, 0x0023, 0x0024, 0x0026, 0x0026,
	0x0028, 0x002B, 0x002E, 0x0034, 0x0037, 0x0044, 0x0047, 0x0066,
	0x0068, 0x0095, 0x009C, 0x0170, 0x0174, 0x01AF,
	/* CP */
	0x0200, 0x0233, 0x0240, 0x0250, 0x04C0, 0x04DD, 0x0500, 0x050B,
	0x0578, 0x058F,
	/* VSC */
	0x0C00, 0x0C03, 0x0C08, 0x0C41, 0x0C50, 0x0C51,
	/* GRAS */
	0x0C80, 0x0C81, 0x0C88, 0x0C8F,
	/* RB */
	0x0CC0, 0x0CC0, 0x0CC4, 0x0CD2,
	/* PC */
	0x0D00, 0x0D0C, 0x0D10, 0x0D17, 0x0D20, 0x0D23,
	/* VFD */
	0x0E40, 0x0E4A,
	/* VPC */
	0x0E60, 0x0E61, 0x0E63, 0x0E68,
	/* UCHE */
	0x0E80, 0x0E84, 0x0E88, 0x0E95,
	/* VMIDMT */
	0x1000, 0x1000, 0x1002, 0x1002, 0x1004, 0x1004, 0x1008, 0x100A,
	0x100C, 0x100D, 0x100F, 0x1010, 0x1012, 0x1016, 0x1024, 0x1024,
	0x1027, 0x1027, 0x1100, 0x1100, 0x1102, 0x1102, 0x1104, 0x1104,
	0x1110, 0x1110, 0x1112, 0x1116, 0x1124, 0x1124, 0x1300, 0x1300,
	0x1380, 0x1380,
	/* GRAS CTX 0 */
	0x2000, 0x2004, 0x2008, 0x2067, 0x2070, 0x2078, 0x207B, 0x216E,
	/* PC CTX 0 */
	0x21C0, 0x21C6, 0x21D0, 0x21D0, 0x21D9, 0x21D9, 0x21E5, 0x21E7,
	/* VFD CTX 0 */
	0x2200, 0x2204, 0x2208, 0x22A9,
	/* GRAS CTX 1 */
	0x2400, 0x2404, 0x2408, 0x2467, 0x2470, 0x2478, 0x247B, 0x256E,
	/* PC CTX 1 */
	0x25C0, 0x25C6, 0x25D0, 0x25D0, 0x25D9, 0x25D9, 0x25E5, 0x25E7,
	/* VFD CTX 1 */
	0x2600, 0x2604, 0x2608, 0x26A9,
	/* XPU */
	0x2C00, 0x2C01, 0x2C10, 0x2C10, 0x2C12, 0x2C16, 0x2C1D, 0x2C20,
	0x2C28, 0x2C28, 0x2C30, 0x2C30, 0x2C32, 0x2C36, 0x2C40, 0x2C40,
	0x2C50, 0x2C50, 0x2C52, 0x2C56, 0x2C80, 0x2C80, 0x2C94, 0x2C95,
	/* VBIF */
	0x3000, 0x3007, 0x300C, 0x3014, 0x3018, 0x301D, 0x3020, 0x3022,
	0x3024, 0x3026, 0x3028, 0x302A, 0x302C, 0x302D, 0x3030, 0x3031,
	0x3034, 0x3036, 0x3038, 0x3038, 0x303C, 0x303D, 0x3040, 0x3040,
	0x3049, 0x3049, 0x3058, 0x3058, 0x305B, 0x3061, 0x3064, 0x3068,
	0x306C, 0x306D, 0x3080, 0x3088, 0x308B, 0x308C, 0x3090, 0x3094,
	0x3098, 0x3098, 0x309C, 0x309C, 0x30C0, 0x30C0, 0x30C8, 0x30C8,
	0x30D0, 0x30D0, 0x30D8, 0x30D8, 0x30E0, 0x30E0, 0x3100, 0x3100,
	0x3108, 0x3108, 0x3110, 0x3110, 0x3118, 0x3118, 0x3120, 0x3120,
	0x3124, 0x3125, 0x3129, 0x3129, 0x3131, 0x3131, 0x330C, 0x330C,
	0x3310, 0x3310, 0x3400, 0x3401, 0x3410, 0x3410, 0x3412, 0x3416,
	0x341D, 0x3420, 0x3428, 0x3428, 0x3430, 0x3430, 0x3432, 0x3436,
	0x3440, 0x3440, 0x3450, 0x3450, 0x3452, 0x3456, 0x3480, 0x3480,
	0x3494, 0x3495, 0x4000, 0x4000, 0x4002, 0x4002, 0x4004, 0x4004,
	0x4008, 0x400A, 0x400C, 0x400D, 0x400F, 0x4012, 0x4014, 0x4016,
	0x401D, 0x401D, 0x4020, 0x4027, 0x4060, 0x4062, 0x4200, 0x4200,
	0x4300, 0x4300, 0x4400, 0x4400, 0x4500, 0x4500, 0x4800, 0x4802,
	0x480F, 0x480F, 0x4811, 0x4811, 0x4813, 0x4813, 0x4815, 0x4816,
	0x482B, 0x482B, 0x4857, 0x4857, 0x4883, 0x4883, 0x48AF, 0x48AF,
	0x48C5, 0x48C5, 0x48E5, 0x48E5, 0x4905, 0x4905, 0x4925, 0x4925,
	0x4945, 0x4945, 0x4950, 0x4950, 0x495B, 0x495B, 0x4980, 0x498E,
	0x4B00, 0x4B00, 0x4C00, 0x4C00, 0x4D00, 0x4D00, 0x4E00, 0x4E00,
	0x4E80, 0x4E80, 0x4F00, 0x4F00, 0x4F08, 0x4F08, 0x4F10, 0x4F10,
	0x4F18, 0x4F18, 0x4F20, 0x4F20, 0x4F30, 0x4F30, 0x4F60, 0x4F60,
	0x4F80, 0x4F81, 0x4F88, 0x4F89, 0x4FEE, 0x4FEE, 0x4FF3, 0x4FF3,
	0x6000, 0x6001, 0x6008, 0x600F, 0x6014, 0x6016, 0x6018, 0x601B,
	0x61FD, 0x61FD, 0x623C, 0x623C, 0x6380, 0x6380, 0x63A0, 0x63A0,
	0x63C0, 0x63C1, 0x63C8, 0x63C9, 0x63D0, 0x63D4, 0x63D6, 0x63D6,
	0x63EE, 0x63EE, 0x6400, 0x6401, 0x6408, 0x640F, 0x6414, 0x6416,
	0x6418, 0x641B, 0x65FD, 0x65FD, 0x663C, 0x663C, 0x6780, 0x6780,
	0x67A0, 0x67A0, 0x67C0, 0x67C1, 0x67C8, 0x67C9, 0x67D0, 0x67D4,
	0x67D6, 0x67D6, 0x67EE, 0x67EE, 0x6800, 0x6801, 0x6808, 0x680F,
	0x6814, 0x6816, 0x6818, 0x681B, 0x69FD, 0x69FD, 0x6A3C, 0x6A3C,
	0x6B80, 0x6B80, 0x6BA0, 0x6BA0, 0x6BC0, 0x6BC1, 0x6BC8, 0x6BC9,
	0x6BD0, 0x6BD4, 0x6BD6, 0x6BD6, 0x6BEE, 0x6BEE,
	~0 /* sentinel */
};

#ifdef CONFIG_DEBUG_FS
static void a4xx_show(struct msm_gpu *gpu, struct seq_file *m)
{
	gpu->funcs->pm_resume(gpu);

	seq_printf(m, "status:   %08x\n",
			gpu_read(gpu, REG_A4XX_RBBM_STATUS));
	gpu->funcs->pm_suspend(gpu);

	adreno_show(gpu, m);

}
#endif

/* Register offset defines for A4XX, in order of enum adreno_regs */
static const unsigned int a4xx_register_offsets[REG_ADRENO_REGISTER_MAX] = {
	REG_ADRENO_DEFINE(REG_ADRENO_CP_DEBUG, REG_A4XX_CP_DEBUG),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_ME_RAM_WADDR, REG_A4XX_CP_ME_RAM_WADDR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_ME_RAM_DATA, REG_A4XX_CP_ME_RAM_DATA),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_PFP_UCODE_DATA,
			REG_A4XX_CP_PFP_UCODE_DATA),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_PFP_UCODE_ADDR,
			REG_A4XX_CP_PFP_UCODE_ADDR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_WFI_PEND_CTR, REG_A4XX_CP_WFI_PEND_CTR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_BASE, REG_A4XX_CP_RB_BASE),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_RPTR_ADDR, REG_A4XX_CP_RB_RPTR_ADDR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_RPTR, REG_A4XX_CP_RB_RPTR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_WPTR, REG_A4XX_CP_RB_WPTR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_PROTECT_CTRL, REG_A4XX_CP_PROTECT_CTRL),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_ME_CNTL, REG_A4XX_CP_ME_CNTL),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_CNTL, REG_A4XX_CP_RB_CNTL),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_IB1_BASE, REG_A4XX_CP_IB1_BASE),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_IB1_BUFSZ, REG_A4XX_CP_IB1_BUFSZ),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_IB2_BASE, REG_A4XX_CP_IB2_BASE),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_IB2_BUFSZ, REG_A4XX_CP_IB2_BUFSZ),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_TIMESTAMP, REG_AXXX_CP_SCRATCH_REG0),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_ME_RAM_RADDR, REG_A4XX_CP_ME_RAM_RADDR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_ROQ_ADDR, REG_A4XX_CP_ROQ_ADDR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_ROQ_DATA, REG_A4XX_CP_ROQ_DATA),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_MERCIU_ADDR, REG_A4XX_CP_MERCIU_ADDR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_MERCIU_DATA, REG_A4XX_CP_MERCIU_DATA),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_MERCIU_DATA2, REG_A4XX_CP_MERCIU_DATA2),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_MEQ_ADDR, REG_A4XX_CP_MEQ_ADDR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_MEQ_DATA, REG_A4XX_CP_MEQ_DATA),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_HW_FAULT, REG_A4XX_CP_HW_FAULT),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_PROTECT_STATUS,
			REG_A4XX_CP_PROTECT_STATUS),
	REG_ADRENO_DEFINE(REG_ADRENO_SCRATCH_ADDR, REG_A4XX_CP_SCRATCH_ADDR),
	REG_ADRENO_DEFINE(REG_ADRENO_SCRATCH_UMSK, REG_A4XX_CP_SCRATCH_UMASK),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_STATUS, REG_A4XX_RBBM_STATUS),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_PERFCTR_CTL,
			REG_A4XX_RBBM_PERFCTR_CTL),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_PERFCTR_LOAD_CMD0,
			REG_A4XX_RBBM_PERFCTR_LOAD_CMD0),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_PERFCTR_LOAD_CMD1,
			REG_A4XX_RBBM_PERFCTR_LOAD_CMD1),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_PERFCTR_LOAD_CMD2,
			REG_A4XX_RBBM_PERFCTR_LOAD_CMD2),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_PERFCTR_PWR_1_LO,
			REG_A4XX_RBBM_PERFCTR_PWR_1_LO),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_INT_0_MASK, REG_A4XX_RBBM_INT_0_MASK),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_INT_0_STATUS,
			REG_A4XX_RBBM_INT_0_STATUS),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_AHB_ERROR_STATUS,
			REG_A4XX_RBBM_AHB_ERROR_STATUS),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_AHB_CMD, REG_A4XX_RBBM_AHB_CMD),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_CLOCK_CTL, REG_A4XX_RBBM_CLOCK_CTL),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_AHB_ME_SPLIT_STATUS,
			REG_A4XX_RBBM_AHB_ME_SPLIT_STATUS),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_AHB_PFP_SPLIT_STATUS,
			REG_A4XX_RBBM_AHB_PFP_SPLIT_STATUS),
	REG_ADRENO_DEFINE(REG_ADRENO_VPC_DEBUG_RAM_SEL,
			REG_A4XX_VPC_DEBUG_RAM_SEL),
	REG_ADRENO_DEFINE(REG_ADRENO_VPC_DEBUG_RAM_READ,
			REG_A4XX_VPC_DEBUG_RAM_READ),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_INT_CLEAR_CMD,
			REG_A4XX_RBBM_INT_CLEAR_CMD),
	REG_ADRENO_DEFINE(REG_ADRENO_VSC_SIZE_ADDRESS,
			REG_A4XX_VSC_SIZE_ADDRESS),
	REG_ADRENO_DEFINE(REG_ADRENO_VFD_CONTROL_0, REG_A4XX_VFD_CONTROL_0),
	REG_ADRENO_DEFINE(REG_ADRENO_SP_VS_PVT_MEM_ADDR_REG,
			REG_A4XX_SP_VS_PVT_MEM_ADDR),
	REG_ADRENO_DEFINE(REG_ADRENO_SP_FS_PVT_MEM_ADDR_REG,
			REG_A4XX_SP_FS_PVT_MEM_ADDR),
	REG_ADRENO_DEFINE(REG_ADRENO_SP_VS_OBJ_START_REG,
			REG_A4XX_SP_VS_OBJ_START),
	REG_ADRENO_DEFINE(REG_ADRENO_SP_FS_OBJ_START_REG,
			REG_A4XX_SP_FS_OBJ_START),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_RBBM_CTL, REG_A4XX_RBBM_RBBM_CTL),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_SW_RESET_CMD,
			REG_A4XX_RBBM_SW_RESET_CMD),
	REG_ADRENO_DEFINE(REG_ADRENO_UCHE_INVALIDATE0,
			REG_A4XX_UCHE_INVALIDATE0),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_PERFCTR_LOAD_VALUE_LO,
			REG_A4XX_RBBM_PERFCTR_LOAD_VALUE_LO),
	REG_ADRENO_DEFINE(REG_ADRENO_RBBM_PERFCTR_LOAD_VALUE_HI,
			REG_A4XX_RBBM_PERFCTR_LOAD_VALUE_HI),
};

static void a4xx_dump(struct msm_gpu *gpu)
{
	adreno_dump(gpu);
	printk("status:   %08x\n",
			gpu_read(gpu, REG_A4XX_RBBM_STATUS));
	adreno_dump(gpu);
}

static const struct adreno_gpu_funcs funcs = {
	.base = {
		.get_param = adreno_get_param,
		.hw_init = a4xx_hw_init,
		.pm_suspend = msm_gpu_pm_suspend,
		.pm_resume = msm_gpu_pm_resume,
		.recover = a4xx_recover,
		.last_fence = adreno_last_fence,
		.submit = adreno_submit,
		.flush = adreno_flush,
		.idle = a4xx_idle,
		.irq = a4xx_irq,
		.destroy = a4xx_destroy,
#ifdef CONFIG_DEBUG_FS
		.show = a4xx_show,
#endif
	},
};

struct msm_gpu *a4xx_gpu_init(struct drm_device *dev)
{
	struct a4xx_gpu *a4xx_gpu = NULL;
	struct adreno_gpu *adreno_gpu;
	struct msm_gpu *gpu;
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = priv->gpu_pdev;
	int ret;

	if (!pdev) {
		dev_err(dev->dev, "no a4xx device\n");
		ret = -ENXIO;
		goto fail;
	}

	a4xx_gpu = kzalloc(sizeof(*a4xx_gpu), GFP_KERNEL);
	if (!a4xx_gpu) {
		ret = -ENOMEM;
		goto fail;
	}

	adreno_gpu = &a4xx_gpu->base;
	gpu = &adreno_gpu->base;

	a4xx_gpu->pdev = pdev;

	gpu->perfcntrs = NULL;
	gpu->num_perfcntrs = 0;

	adreno_gpu->registers = a4xx_registers;
	adreno_gpu->reg_offsets = a4xx_register_offsets;

	ret = adreno_gpu_init(dev, pdev, adreno_gpu, &funcs);
	if (ret)
		goto fail;

	/* if needed, allocate gmem: */
	if (adreno_is_a4xx(adreno_gpu)) {
#ifdef CONFIG_MSM_OCMEM
		/* TODO this is different/missing upstream: */
		struct ocmem_buf *ocmem_hdl =
				ocmem_allocate(OCMEM_GRAPHICS, adreno_gpu->gmem);

		a4xx_gpu->ocmem_hdl = ocmem_hdl;
		a4xx_gpu->ocmem_base = ocmem_hdl->addr;
		adreno_gpu->gmem = ocmem_hdl->len;
		DBG("using %dK of OCMEM at 0x%08x", adreno_gpu->gmem / 1024,
				a4xx_gpu->ocmem_base);
#endif
	}

	if (!gpu->mmu) {
		/* TODO we think it is possible to configure the GPU to
		 * restrict access to VRAM carveout.  But the required
		 * registers are unknown.  For now just bail out and
		 * limp along with just modesetting.  If it turns out
		 * to not be possible to restrict access, then we must
		 * implement a cmdstream validator.
		 */
		dev_err(dev->dev, "No memory protection without IOMMU\n");
		ret = -ENXIO;
		goto fail;
	}

	return gpu;

fail:
	if (a4xx_gpu)
		a4xx_destroy(&a4xx_gpu->base.base);

	return ERR_PTR(ret);
}
