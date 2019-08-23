// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 */

#ifdef CONFIG_MSM_OCMEM
#  include <mach/ocmem.h>
#endif

#include "a3xx_gpu.h"

#define A3XX_INT0_MASK \
	(A3XX_INT0_RBBM_AHB_ERROR |        \
	 A3XX_INT0_RBBM_ATB_BUS_OVERFLOW | \
	 A3XX_INT0_CP_T0_PACKET_IN_IB |    \
	 A3XX_INT0_CP_OPCODE_ERROR |       \
	 A3XX_INT0_CP_RESERVED_BIT_ERROR | \
	 A3XX_INT0_CP_HW_FAULT |           \
	 A3XX_INT0_CP_IB1_INT |            \
	 A3XX_INT0_CP_IB2_INT |            \
	 A3XX_INT0_CP_RB_INT |             \
	 A3XX_INT0_CP_REG_PROTECT_FAULT |  \
	 A3XX_INT0_CP_AHB_ERROR_HALT |     \
	 A3XX_INT0_CACHE_FLUSH_TS |        \
	 A3XX_INT0_UCHE_OOB_ACCESS)

extern bool hang_debug;

static void a3xx_dump(struct msm_gpu *gpu);
static bool a3xx_idle(struct msm_gpu *gpu);

static bool a3xx_me_init(struct msm_gpu *gpu)
{
	struct msm_ringbuffer *ring = gpu->rb[0];

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

	gpu->funcs->flush(gpu, ring);
	return a3xx_idle(gpu);
}

static int a3xx_hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a3xx_gpu *a3xx_gpu = to_a3xx_gpu(adreno_gpu);
	uint32_t *ptr, len;
	int i, ret;

	DBG("%s", gpu->name);

	if (adreno_is_a305(adreno_gpu)) {
		/* Set up 16 deep read/write request queues: */
		gpu_write(gpu, REG_A3XX_VBIF_IN_RD_LIM_CONF0, 0x10101010);
		gpu_write(gpu, REG_A3XX_VBIF_IN_RD_LIM_CONF1, 0x10101010);
		gpu_write(gpu, REG_A3XX_VBIF_OUT_RD_LIM_CONF0, 0x10101010);
		gpu_write(gpu, REG_A3XX_VBIF_OUT_WR_LIM_CONF0, 0x10101010);
		gpu_write(gpu, REG_A3XX_VBIF_DDR_OUT_MAX_BURST, 0x0000303);
		gpu_write(gpu, REG_A3XX_VBIF_IN_WR_LIM_CONF0, 0x10101010);
		gpu_write(gpu, REG_A3XX_VBIF_IN_WR_LIM_CONF1, 0x10101010);
		/* Enable WR-REQ: */
		gpu_write(gpu, REG_A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x0000ff);
		/* Set up round robin arbitration between both AXI ports: */
		gpu_write(gpu, REG_A3XX_VBIF_ARB_CTL, 0x00000030);
		/* Set up AOOO: */
		gpu_write(gpu, REG_A3XX_VBIF_OUT_AXI_AOOO_EN, 0x0000003c);
		gpu_write(gpu, REG_A3XX_VBIF_OUT_AXI_AOOO, 0x003c003c);
	} else if (adreno_is_a306(adreno_gpu)) {
		gpu_write(gpu, REG_A3XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x0003);
		gpu_write(gpu, REG_A3XX_VBIF_OUT_RD_LIM_CONF0, 0x0000000a);
		gpu_write(gpu, REG_A3XX_VBIF_OUT_WR_LIM_CONF0, 0x0000000a);
	} else if (adreno_is_a320(adreno_gpu)) {
		/* Set up 16 deep read/write request queues: */
		gpu_write(gpu, REG_A3XX_VBIF_IN_RD_LIM_CONF0, 0x10101010);
		gpu_write(gpu, REG_A3XX_VBIF_IN_RD_LIM_CONF1, 0x10101010);
		gpu_write(gpu, REG_A3XX_VBIF_OUT_RD_LIM_CONF0, 0x10101010);
		gpu_write(gpu, REG_A3XX_VBIF_OUT_WR_LIM_CONF0, 0x10101010);
		gpu_write(gpu, REG_A3XX_VBIF_DDR_OUT_MAX_BURST, 0x0000303);
		gpu_write(gpu, REG_A3XX_VBIF_IN_WR_LIM_CONF0, 0x10101010);
		gpu_write(gpu, REG_A3XX_VBIF_IN_WR_LIM_CONF1, 0x10101010);
		/* Enable WR-REQ: */
		gpu_write(gpu, REG_A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x0000ff);
		/* Set up round robin arbitration between both AXI ports: */
		gpu_write(gpu, REG_A3XX_VBIF_ARB_CTL, 0x00000030);
		/* Set up AOOO: */
		gpu_write(gpu, REG_A3XX_VBIF_OUT_AXI_AOOO_EN, 0x0000003c);
		gpu_write(gpu, REG_A3XX_VBIF_OUT_AXI_AOOO, 0x003c003c);
		/* Enable 1K sort: */
		gpu_write(gpu, REG_A3XX_VBIF_ABIT_SORT, 0x000000ff);
		gpu_write(gpu, REG_A3XX_VBIF_ABIT_SORT_CONF, 0x000000a4);

	} else if (adreno_is_a330v2(adreno_gpu)) {
		/*
		 * Most of the VBIF registers on 8974v2 have the correct
		 * values at power on, so we won't modify those if we don't
		 * need to
		 */
		/* Enable 1k sort: */
		gpu_write(gpu, REG_A3XX_VBIF_ABIT_SORT, 0x0001003f);
		gpu_write(gpu, REG_A3XX_VBIF_ABIT_SORT_CONF, 0x000000a4);
		/* Enable WR-REQ: */
		gpu_write(gpu, REG_A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x00003f);
		gpu_write(gpu, REG_A3XX_VBIF_DDR_OUT_MAX_BURST, 0x0000303);
		/* Set up VBIF_ROUND_ROBIN_QOS_ARB: */
		gpu_write(gpu, REG_A3XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x0003);

	} else if (adreno_is_a330(adreno_gpu)) {
		/* Set up 16 deep read/write request queues: */
		gpu_write(gpu, REG_A3XX_VBIF_IN_RD_LIM_CONF0, 0x18181818);
		gpu_write(gpu, REG_A3XX_VBIF_IN_RD_LIM_CONF1, 0x18181818);
		gpu_write(gpu, REG_A3XX_VBIF_OUT_RD_LIM_CONF0, 0x18181818);
		gpu_write(gpu, REG_A3XX_VBIF_OUT_WR_LIM_CONF0, 0x18181818);
		gpu_write(gpu, REG_A3XX_VBIF_DDR_OUT_MAX_BURST, 0x0000303);
		gpu_write(gpu, REG_A3XX_VBIF_IN_WR_LIM_CONF0, 0x18181818);
		gpu_write(gpu, REG_A3XX_VBIF_IN_WR_LIM_CONF1, 0x18181818);
		/* Enable WR-REQ: */
		gpu_write(gpu, REG_A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x00003f);
		/* Set up round robin arbitration between both AXI ports: */
		gpu_write(gpu, REG_A3XX_VBIF_ARB_CTL, 0x00000030);
		/* Set up VBIF_ROUND_ROBIN_QOS_ARB: */
		gpu_write(gpu, REG_A3XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x0001);
		/* Set up AOOO: */
		gpu_write(gpu, REG_A3XX_VBIF_OUT_AXI_AOOO_EN, 0x0000003f);
		gpu_write(gpu, REG_A3XX_VBIF_OUT_AXI_AOOO, 0x003f003f);
		/* Enable 1K sort: */
		gpu_write(gpu, REG_A3XX_VBIF_ABIT_SORT, 0x0001003f);
		gpu_write(gpu, REG_A3XX_VBIF_ABIT_SORT_CONF, 0x000000a4);
		/* Disable VBIF clock gating. This is to enable AXI running
		 * higher frequency than GPU:
		 */
		gpu_write(gpu, REG_A3XX_VBIF_CLKON, 0x00000001);

	} else {
		BUG();
	}

	/* Make all blocks contribute to the GPU BUSY perf counter: */
	gpu_write(gpu, REG_A3XX_RBBM_GPU_BUSY_MASKED, 0xffffffff);

	/* Tune the hystersis counters for SP and CP idle detection: */
	gpu_write(gpu, REG_A3XX_RBBM_SP_HYST_CNT, 0x10);
	gpu_write(gpu, REG_A3XX_RBBM_WAIT_IDLE_CLOCKS_CTL, 0x10);

	/* Enable the RBBM error reporting bits.  This lets us get
	 * useful information on failure:
	 */
	gpu_write(gpu, REG_A3XX_RBBM_AHB_CTL0, 0x00000001);

	/* Enable AHB error reporting: */
	gpu_write(gpu, REG_A3XX_RBBM_AHB_CTL1, 0xa6ffffff);

	/* Turn on the power counters: */
	gpu_write(gpu, REG_A3XX_RBBM_RBBM_CTL, 0x00030000);

	/* Turn on hang detection - this spews a lot of useful information
	 * into the RBBM registers on a hang:
	 */
	gpu_write(gpu, REG_A3XX_RBBM_INTERFACE_HANG_INT_CTL, 0x00010fff);

	/* Enable 64-byte cacheline size. HW Default is 32-byte (0x000000E0): */
	gpu_write(gpu, REG_A3XX_UCHE_CACHE_MODE_CONTROL_REG, 0x00000001);

	/* Enable Clock gating: */
	if (adreno_is_a306(adreno_gpu))
		gpu_write(gpu, REG_A3XX_RBBM_CLOCK_CTL, 0xaaaaaaaa);
	else if (adreno_is_a320(adreno_gpu))
		gpu_write(gpu, REG_A3XX_RBBM_CLOCK_CTL, 0xbfffffff);
	else if (adreno_is_a330v2(adreno_gpu))
		gpu_write(gpu, REG_A3XX_RBBM_CLOCK_CTL, 0xaaaaaaaa);
	else if (adreno_is_a330(adreno_gpu))
		gpu_write(gpu, REG_A3XX_RBBM_CLOCK_CTL, 0xbffcffff);

	if (adreno_is_a330v2(adreno_gpu))
		gpu_write(gpu, REG_A3XX_RBBM_GPR0_CTL, 0x05515455);
	else if (adreno_is_a330(adreno_gpu))
		gpu_write(gpu, REG_A3XX_RBBM_GPR0_CTL, 0x00000000);

	/* Set the OCMEM base address for A330, etc */
	if (a3xx_gpu->ocmem_hdl) {
		gpu_write(gpu, REG_A3XX_RB_GMEM_BASE_ADDR,
			(unsigned int)(a3xx_gpu->ocmem_base >> 14));
	}

	/* Turn on performance counters: */
	gpu_write(gpu, REG_A3XX_RBBM_PERFCTR_CTL, 0x01);

	/* Enable the perfcntrs that we use.. */
	for (i = 0; i < gpu->num_perfcntrs; i++) {
		const struct msm_gpu_perfcntr *perfcntr = &gpu->perfcntrs[i];
		gpu_write(gpu, perfcntr->select_reg, perfcntr->select_val);
	}

	gpu_write(gpu, REG_A3XX_RBBM_INT_0_MASK, A3XX_INT0_MASK);

	ret = adreno_hw_init(gpu);
	if (ret)
		return ret;

	/* setup access protection: */
	gpu_write(gpu, REG_A3XX_CP_PROTECT_CTRL, 0x00000007);

	/* RBBM registers */
	gpu_write(gpu, REG_A3XX_CP_PROTECT(0), 0x63000040);
	gpu_write(gpu, REG_A3XX_CP_PROTECT(1), 0x62000080);
	gpu_write(gpu, REG_A3XX_CP_PROTECT(2), 0x600000cc);
	gpu_write(gpu, REG_A3XX_CP_PROTECT(3), 0x60000108);
	gpu_write(gpu, REG_A3XX_CP_PROTECT(4), 0x64000140);
	gpu_write(gpu, REG_A3XX_CP_PROTECT(5), 0x66000400);

	/* CP registers */
	gpu_write(gpu, REG_A3XX_CP_PROTECT(6), 0x65000700);
	gpu_write(gpu, REG_A3XX_CP_PROTECT(7), 0x610007d8);
	gpu_write(gpu, REG_A3XX_CP_PROTECT(8), 0x620007e0);
	gpu_write(gpu, REG_A3XX_CP_PROTECT(9), 0x61001178);
	gpu_write(gpu, REG_A3XX_CP_PROTECT(10), 0x64001180);

	/* RB registers */
	gpu_write(gpu, REG_A3XX_CP_PROTECT(11), 0x60003300);

	/* VBIF registers */
	gpu_write(gpu, REG_A3XX_CP_PROTECT(12), 0x6b00c000);

	/* NOTE: PM4/micro-engine firmware registers look to be the same
	 * for a2xx and a3xx.. we could possibly push that part down to
	 * adreno_gpu base class.  Or push both PM4 and PFP but
	 * parameterize the pfp ucode addr/data registers..
	 */

	/* Load PM4: */
	ptr = (uint32_t *)(adreno_gpu->fw[ADRENO_FW_PM4]->data);
	len = adreno_gpu->fw[ADRENO_FW_PM4]->size / 4;
	DBG("loading PM4 ucode version: %x", ptr[1]);

	gpu_write(gpu, REG_AXXX_CP_DEBUG,
			AXXX_CP_DEBUG_DYNAMIC_CLK_DISABLE |
			AXXX_CP_DEBUG_MIU_128BIT_WRITE_ENABLE);
	gpu_write(gpu, REG_AXXX_CP_ME_RAM_WADDR, 0);
	for (i = 1; i < len; i++)
		gpu_write(gpu, REG_AXXX_CP_ME_RAM_DATA, ptr[i]);

	/* Load PFP: */
	ptr = (uint32_t *)(adreno_gpu->fw[ADRENO_FW_PFP]->data);
	len = adreno_gpu->fw[ADRENO_FW_PFP]->size / 4;
	DBG("loading PFP ucode version: %x", ptr[5]);

	gpu_write(gpu, REG_A3XX_CP_PFP_UCODE_ADDR, 0);
	for (i = 1; i < len; i++)
		gpu_write(gpu, REG_A3XX_CP_PFP_UCODE_DATA, ptr[i]);

	/* CP ROQ queue sizes (bytes) - RB:16, ST:16, IB1:32, IB2:64 */
	if (adreno_is_a305(adreno_gpu) || adreno_is_a306(adreno_gpu) ||
			adreno_is_a320(adreno_gpu)) {
		gpu_write(gpu, REG_AXXX_CP_QUEUE_THRESHOLDS,
				AXXX_CP_QUEUE_THRESHOLDS_CSQ_IB1_START(2) |
				AXXX_CP_QUEUE_THRESHOLDS_CSQ_IB2_START(6) |
				AXXX_CP_QUEUE_THRESHOLDS_CSQ_ST_START(14));
	} else if (adreno_is_a330(adreno_gpu)) {
		/* NOTE: this (value take from downstream android driver)
		 * includes some bits outside of the known bitfields.  But
		 * A330 has this "MERCIU queue" thing too, which might
		 * explain a new bitfield or reshuffling:
		 */
		gpu_write(gpu, REG_AXXX_CP_QUEUE_THRESHOLDS, 0x003e2008);
	}

	/* clear ME_HALT to start micro engine */
	gpu_write(gpu, REG_AXXX_CP_ME_CNTL, 0);

	return a3xx_me_init(gpu) ? 0 : -EINVAL;
}

static void a3xx_recover(struct msm_gpu *gpu)
{
	int i;

	adreno_dump_info(gpu);

	for (i = 0; i < 8; i++) {
		printk("CP_SCRATCH_REG%d: %u\n", i,
			gpu_read(gpu, REG_AXXX_CP_SCRATCH_REG0 + i));
	}

	/* dump registers before resetting gpu, if enabled: */
	if (hang_debug)
		a3xx_dump(gpu);

	gpu_write(gpu, REG_A3XX_RBBM_SW_RESET_CMD, 1);
	gpu_read(gpu, REG_A3XX_RBBM_SW_RESET_CMD);
	gpu_write(gpu, REG_A3XX_RBBM_SW_RESET_CMD, 0);
	adreno_recover(gpu);
}

static void a3xx_destroy(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a3xx_gpu *a3xx_gpu = to_a3xx_gpu(adreno_gpu);

	DBG("%s", gpu->name);

	adreno_gpu_cleanup(adreno_gpu);

#ifdef CONFIG_MSM_OCMEM
	if (a3xx_gpu->ocmem_base)
		ocmem_free(OCMEM_GRAPHICS, a3xx_gpu->ocmem_hdl);
#endif

	kfree(a3xx_gpu);
}

static bool a3xx_idle(struct msm_gpu *gpu)
{
	/* wait for ringbuffer to drain: */
	if (!adreno_idle(gpu, gpu->rb[0]))
		return false;

	/* then wait for GPU to finish: */
	if (spin_until(!(gpu_read(gpu, REG_A3XX_RBBM_STATUS) &
			A3XX_RBBM_STATUS_GPU_BUSY))) {
		DRM_ERROR("%s: timeout waiting for GPU to idle!\n", gpu->name);

		/* TODO maybe we need to reset GPU here to recover from hang? */
		return false;
	}

	return true;
}

static irqreturn_t a3xx_irq(struct msm_gpu *gpu)
{
	uint32_t status;

	status = gpu_read(gpu, REG_A3XX_RBBM_INT_0_STATUS);
	DBG("%s: %08x", gpu->name, status);

	// TODO

	gpu_write(gpu, REG_A3XX_RBBM_INT_CLEAR_CMD, status);

	msm_gpu_retire(gpu);

	return IRQ_HANDLED;
}

static const unsigned int a3xx_registers[] = {
	0x0000, 0x0002, 0x0010, 0x0012, 0x0018, 0x0018, 0x0020, 0x0027,
	0x0029, 0x002b, 0x002e, 0x0033, 0x0040, 0x0042, 0x0050, 0x005c,
	0x0060, 0x006c, 0x0080, 0x0082, 0x0084, 0x0088, 0x0090, 0x00e5,
	0x00ea, 0x00ed, 0x0100, 0x0100, 0x0110, 0x0123, 0x01c0, 0x01c1,
	0x01c3, 0x01c5, 0x01c7, 0x01c7, 0x01d5, 0x01d9, 0x01dc, 0x01dd,
	0x01ea, 0x01ea, 0x01ee, 0x01f1, 0x01f5, 0x01f5, 0x01fc, 0x01ff,
	0x0440, 0x0440, 0x0443, 0x0443, 0x0445, 0x0445, 0x044d, 0x044f,
	0x0452, 0x0452, 0x0454, 0x046f, 0x047c, 0x047c, 0x047f, 0x047f,
	0x0578, 0x057f, 0x0600, 0x0602, 0x0605, 0x0607, 0x060a, 0x060e,
	0x0612, 0x0614, 0x0c01, 0x0c02, 0x0c06, 0x0c1d, 0x0c3d, 0x0c3f,
	0x0c48, 0x0c4b, 0x0c80, 0x0c80, 0x0c88, 0x0c8b, 0x0ca0, 0x0cb7,
	0x0cc0, 0x0cc1, 0x0cc6, 0x0cc7, 0x0ce4, 0x0ce5, 0x0e00, 0x0e05,
	0x0e0c, 0x0e0c, 0x0e22, 0x0e23, 0x0e41, 0x0e45, 0x0e64, 0x0e65,
	0x0e80, 0x0e82, 0x0e84, 0x0e89, 0x0ea0, 0x0ea1, 0x0ea4, 0x0ea7,
	0x0ec4, 0x0ecb, 0x0ee0, 0x0ee0, 0x0f00, 0x0f01, 0x0f03, 0x0f09,
	0x2040, 0x2040, 0x2044, 0x2044, 0x2048, 0x204d, 0x2068, 0x2069,
	0x206c, 0x206d, 0x2070, 0x2070, 0x2072, 0x2072, 0x2074, 0x2075,
	0x2079, 0x207a, 0x20c0, 0x20d3, 0x20e4, 0x20ef, 0x2100, 0x2109,
	0x210c, 0x210c, 0x210e, 0x210e, 0x2110, 0x2111, 0x2114, 0x2115,
	0x21e4, 0x21e4, 0x21ea, 0x21ea, 0x21ec, 0x21ed, 0x21f0, 0x21f0,
	0x2200, 0x2212, 0x2214, 0x2217, 0x221a, 0x221a, 0x2240, 0x227e,
	0x2280, 0x228b, 0x22c0, 0x22c0, 0x22c4, 0x22ce, 0x22d0, 0x22d8,
	0x22df, 0x22e6, 0x22e8, 0x22e9, 0x22ec, 0x22ec, 0x22f0, 0x22f7,
	0x22ff, 0x22ff, 0x2340, 0x2343, 0x2440, 0x2440, 0x2444, 0x2444,
	0x2448, 0x244d, 0x2468, 0x2469, 0x246c, 0x246d, 0x2470, 0x2470,
	0x2472, 0x2472, 0x2474, 0x2475, 0x2479, 0x247a, 0x24c0, 0x24d3,
	0x24e4, 0x24ef, 0x2500, 0x2509, 0x250c, 0x250c, 0x250e, 0x250e,
	0x2510, 0x2511, 0x2514, 0x2515, 0x25e4, 0x25e4, 0x25ea, 0x25ea,
	0x25ec, 0x25ed, 0x25f0, 0x25f0, 0x2600, 0x2612, 0x2614, 0x2617,
	0x261a, 0x261a, 0x2640, 0x267e, 0x2680, 0x268b, 0x26c0, 0x26c0,
	0x26c4, 0x26ce, 0x26d0, 0x26d8, 0x26df, 0x26e6, 0x26e8, 0x26e9,
	0x26ec, 0x26ec, 0x26f0, 0x26f7, 0x26ff, 0x26ff, 0x2740, 0x2743,
	0x300c, 0x300e, 0x301c, 0x301d, 0x302a, 0x302a, 0x302c, 0x302d,
	0x3030, 0x3031, 0x3034, 0x3036, 0x303c, 0x303c, 0x305e, 0x305f,
	~0   /* sentinel */
};

/* would be nice to not have to duplicate the _show() stuff with printk(): */
static void a3xx_dump(struct msm_gpu *gpu)
{
	printk("status:   %08x\n",
			gpu_read(gpu, REG_A3XX_RBBM_STATUS));
	adreno_dump(gpu);
}

static struct msm_gpu_state *a3xx_gpu_state_get(struct msm_gpu *gpu)
{
	struct msm_gpu_state *state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (!state)
		return ERR_PTR(-ENOMEM);

	adreno_gpu_state_get(gpu, state);

	state->rbbm_status = gpu_read(gpu, REG_A3XX_RBBM_STATUS);

	return state;
}

/* Register offset defines for A3XX */
static const unsigned int a3xx_register_offsets[REG_ADRENO_REGISTER_MAX] = {
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_BASE, REG_AXXX_CP_RB_BASE),
	REG_ADRENO_SKIP(REG_ADRENO_CP_RB_BASE_HI),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_RPTR_ADDR, REG_AXXX_CP_RB_RPTR_ADDR),
	REG_ADRENO_SKIP(REG_ADRENO_CP_RB_RPTR_ADDR_HI),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_RPTR, REG_AXXX_CP_RB_RPTR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_WPTR, REG_AXXX_CP_RB_WPTR),
	REG_ADRENO_DEFINE(REG_ADRENO_CP_RB_CNTL, REG_AXXX_CP_RB_CNTL),
};

static const struct adreno_gpu_funcs funcs = {
	.base = {
		.get_param = adreno_get_param,
		.hw_init = a3xx_hw_init,
		.pm_suspend = msm_gpu_pm_suspend,
		.pm_resume = msm_gpu_pm_resume,
		.recover = a3xx_recover,
		.submit = adreno_submit,
		.flush = adreno_flush,
		.active_ring = adreno_active_ring,
		.irq = a3xx_irq,
		.destroy = a3xx_destroy,
#if defined(CONFIG_DEBUG_FS) || defined(CONFIG_DEV_COREDUMP)
		.show = adreno_show,
#endif
		.gpu_state_get = a3xx_gpu_state_get,
		.gpu_state_put = adreno_gpu_state_put,
	},
};

static const struct msm_gpu_perfcntr perfcntrs[] = {
	{ REG_A3XX_SP_PERFCOUNTER6_SELECT, REG_A3XX_RBBM_PERFCTR_SP_6_LO,
			SP_ALU_ACTIVE_CYCLES, "ALUACTIVE" },
	{ REG_A3XX_SP_PERFCOUNTER7_SELECT, REG_A3XX_RBBM_PERFCTR_SP_7_LO,
			SP_FS_FULL_ALU_INSTRUCTIONS, "ALUFULL" },
};

struct msm_gpu *a3xx_gpu_init(struct drm_device *dev)
{
	struct a3xx_gpu *a3xx_gpu = NULL;
	struct adreno_gpu *adreno_gpu;
	struct msm_gpu *gpu;
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = priv->gpu_pdev;
	int ret;

	if (!pdev) {
		DRM_DEV_ERROR(dev->dev, "no a3xx device\n");
		ret = -ENXIO;
		goto fail;
	}

	a3xx_gpu = kzalloc(sizeof(*a3xx_gpu), GFP_KERNEL);
	if (!a3xx_gpu) {
		ret = -ENOMEM;
		goto fail;
	}

	adreno_gpu = &a3xx_gpu->base;
	gpu = &adreno_gpu->base;

	gpu->perfcntrs = perfcntrs;
	gpu->num_perfcntrs = ARRAY_SIZE(perfcntrs);

	adreno_gpu->registers = a3xx_registers;
	adreno_gpu->reg_offsets = a3xx_register_offsets;

	ret = adreno_gpu_init(dev, pdev, adreno_gpu, &funcs, 1);
	if (ret)
		goto fail;

	/* if needed, allocate gmem: */
	if (adreno_is_a330(adreno_gpu)) {
#ifdef CONFIG_MSM_OCMEM
		/* TODO this is different/missing upstream: */
		struct ocmem_buf *ocmem_hdl =
				ocmem_allocate(OCMEM_GRAPHICS, adreno_gpu->gmem);

		a3xx_gpu->ocmem_hdl = ocmem_hdl;
		a3xx_gpu->ocmem_base = ocmem_hdl->addr;
		adreno_gpu->gmem = ocmem_hdl->len;
		DBG("using %dK of OCMEM at 0x%08x", adreno_gpu->gmem / 1024,
				a3xx_gpu->ocmem_base);
#endif
	}

	if (!gpu->aspace) {
		/* TODO we think it is possible to configure the GPU to
		 * restrict access to VRAM carveout.  But the required
		 * registers are unknown.  For now just bail out and
		 * limp along with just modesetting.  If it turns out
		 * to not be possible to restrict access, then we must
		 * implement a cmdstream validator.
		 */
		DRM_DEV_ERROR(dev->dev, "No memory protection without IOMMU\n");
		ret = -ENXIO;
		goto fail;
	}

	return gpu;

fail:
	if (a3xx_gpu)
		a3xx_destroy(&a3xx_gpu->base.base);

	return ERR_PTR(ret);
}
