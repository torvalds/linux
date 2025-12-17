/* SPDX-License-Identifier: GPL-2.0-only or MIT */
/* Copyright 2025 Arm, Ltd. */

#ifndef __ETHOSU_DEVICE_H__
#define __ETHOSU_DEVICE_H__

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/types.h>

#include <drm/drm_device.h>
#include <drm/gpu_scheduler.h>

#include <drm/ethosu_accel.h>

struct clk;
struct gen_pool;

#define NPU_REG_ID		0x0000
#define NPU_REG_STATUS		0x0004
#define NPU_REG_CMD		0x0008
#define NPU_REG_RESET		0x000c
#define NPU_REG_QBASE		0x0010
#define NPU_REG_QBASE_HI	0x0014
#define NPU_REG_QREAD		0x0018
#define NPU_REG_QCONFIG		0x001c
#define NPU_REG_QSIZE		0x0020
#define NPU_REG_PROT		0x0024
#define NPU_REG_CONFIG		0x0028
#define NPU_REG_REGIONCFG	0x003c
#define NPU_REG_AXILIMIT0	0x0040		// U65
#define NPU_REG_AXILIMIT1	0x0044		// U65
#define NPU_REG_AXILIMIT2	0x0048		// U65
#define NPU_REG_AXILIMIT3	0x004c		// U65
#define NPU_REG_MEM_ATTR0	0x0040		// U85
#define NPU_REG_MEM_ATTR1	0x0044		// U85
#define NPU_REG_MEM_ATTR2	0x0048		// U85
#define NPU_REG_MEM_ATTR3	0x004c		// U85
#define NPU_REG_AXI_SRAM	0x0050		// U85
#define NPU_REG_AXI_EXT		0x0054		// U85

#define NPU_REG_BASEP(x)	(0x0080 + (x) * 8)
#define NPU_REG_BASEP_HI(x)	(0x0084 + (x) * 8)
#define NPU_BASEP_REGION_MAX	8

#define ID_ARCH_MAJOR_MASK	GENMASK(31, 28)
#define ID_ARCH_MINOR_MASK	GENMASK(27, 20)
#define ID_ARCH_PATCH_MASK	GENMASK(19, 16)
#define ID_VER_MAJOR_MASK	GENMASK(11, 8)
#define ID_VER_MINOR_MASK	GENMASK(7, 4)

#define CONFIG_MACS_PER_CC_MASK	GENMASK(3, 0)
#define CONFIG_CMD_STREAM_VER_MASK	GENMASK(7, 4)

#define STATUS_STATE_RUNNING	BIT(0)
#define STATUS_IRQ_RAISED	BIT(1)
#define STATUS_BUS_STATUS	BIT(2)
#define STATUS_RESET_STATUS	BIT(3)
#define STATUS_CMD_PARSE_ERR	BIT(4)
#define STATUS_CMD_END_REACHED	BIT(5)

#define CMD_CLEAR_IRQ		BIT(1)
#define CMD_TRANSITION_TO_RUN	BIT(0)

#define RESET_PENDING_CSL	BIT(1)
#define RESET_PENDING_CPL	BIT(0)

#define PROT_ACTIVE_CSL		BIT(1)

enum ethosu_cmds {
	NPU_OP_CONV = 0x2,
	NPU_OP_DEPTHWISE = 0x3,
	NPU_OP_POOL = 0x5,
	NPU_OP_ELEMENTWISE = 0x6,
	NPU_OP_RESIZE = 0x7,	// U85 only
	NPU_OP_DMA_START = 0x10,
	NPU_SET_IFM_PAD_TOP = 0x100,
	NPU_SET_IFM_PAD_LEFT = 0x101,
	NPU_SET_IFM_PAD_RIGHT = 0x102,
	NPU_SET_IFM_PAD_BOTTOM = 0x103,
	NPU_SET_IFM_DEPTH_M1 = 0x104,
	NPU_SET_IFM_PRECISION = 0x105,
	NPU_SET_IFM_BROADCAST = 0x108,
	NPU_SET_IFM_WIDTH0_M1 = 0x10a,
	NPU_SET_IFM_HEIGHT0_M1 = 0x10b,
	NPU_SET_IFM_HEIGHT1_M1 = 0x10c,
	NPU_SET_IFM_REGION = 0x10f,
	NPU_SET_OFM_WIDTH_M1 = 0x111,
	NPU_SET_OFM_HEIGHT_M1 = 0x112,
	NPU_SET_OFM_DEPTH_M1 = 0x113,
	NPU_SET_OFM_PRECISION = 0x114,
	NPU_SET_OFM_WIDTH0_M1 = 0x11a,
	NPU_SET_OFM_HEIGHT0_M1 = 0x11b,
	NPU_SET_OFM_HEIGHT1_M1 = 0x11c,
	NPU_SET_OFM_REGION = 0x11f,
	NPU_SET_KERNEL_WIDTH_M1 = 0x120,
	NPU_SET_KERNEL_HEIGHT_M1 = 0x121,
	NPU_SET_KERNEL_STRIDE = 0x122,
	NPU_SET_WEIGHT_REGION = 0x128,
	NPU_SET_SCALE_REGION = 0x129,
	NPU_SET_DMA0_SRC_REGION = 0x130,
	NPU_SET_DMA0_DST_REGION = 0x131,
	NPU_SET_DMA0_SIZE0 = 0x132,
	NPU_SET_DMA0_SIZE1 = 0x133,
	NPU_SET_IFM2_BROADCAST = 0x180,
	NPU_SET_IFM2_PRECISION = 0x185,
	NPU_SET_IFM2_WIDTH0_M1 = 0x18a,
	NPU_SET_IFM2_HEIGHT0_M1 = 0x18b,
	NPU_SET_IFM2_HEIGHT1_M1 = 0x18c,
	NPU_SET_IFM2_REGION = 0x18f,
	NPU_SET_IFM_BASE0 = 0x4000,
	NPU_SET_IFM_BASE1 = 0x4001,
	NPU_SET_IFM_BASE2 = 0x4002,
	NPU_SET_IFM_BASE3 = 0x4003,
	NPU_SET_IFM_STRIDE_X = 0x4004,
	NPU_SET_IFM_STRIDE_Y = 0x4005,
	NPU_SET_IFM_STRIDE_C = 0x4006,
	NPU_SET_OFM_BASE0 = 0x4010,
	NPU_SET_OFM_BASE1 = 0x4011,
	NPU_SET_OFM_BASE2 = 0x4012,
	NPU_SET_OFM_BASE3 = 0x4013,
	NPU_SET_OFM_STRIDE_X = 0x4014,
	NPU_SET_OFM_STRIDE_Y = 0x4015,
	NPU_SET_OFM_STRIDE_C = 0x4016,
	NPU_SET_WEIGHT_BASE = 0x4020,
	NPU_SET_WEIGHT_LENGTH = 0x4021,
	NPU_SET_SCALE_BASE = 0x4022,
	NPU_SET_SCALE_LENGTH = 0x4023,
	NPU_SET_DMA0_SRC = 0x4030,
	NPU_SET_DMA0_DST = 0x4031,
	NPU_SET_DMA0_LEN = 0x4032,
	NPU_SET_DMA0_SRC_STRIDE0 = 0x4033,
	NPU_SET_DMA0_SRC_STRIDE1 = 0x4034,
	NPU_SET_DMA0_DST_STRIDE0 = 0x4035,
	NPU_SET_DMA0_DST_STRIDE1 = 0x4036,
	NPU_SET_IFM2_BASE0 = 0x4080,
	NPU_SET_IFM2_BASE1 = 0x4081,
	NPU_SET_IFM2_BASE2 = 0x4082,
	NPU_SET_IFM2_BASE3 = 0x4083,
	NPU_SET_IFM2_STRIDE_X = 0x4084,
	NPU_SET_IFM2_STRIDE_Y = 0x4085,
	NPU_SET_IFM2_STRIDE_C = 0x4086,
	NPU_SET_WEIGHT1_BASE = 0x4090,
	NPU_SET_WEIGHT1_LENGTH = 0x4091,
	NPU_SET_SCALE1_BASE = 0x4092,
	NPU_SET_WEIGHT2_BASE = 0x4092,
	NPU_SET_SCALE1_LENGTH = 0x4093,
	NPU_SET_WEIGHT2_LENGTH = 0x4093,
	NPU_SET_WEIGHT3_BASE = 0x4094,
	NPU_SET_WEIGHT3_LENGTH = 0x4095,
};

#define ETHOSU_SRAM_REGION	2	/* Matching Vela compiler */

/**
 * struct ethosu_device - Ethosu device
 */
struct ethosu_device {
	/** @base: Base drm_device. */
	struct drm_device base;

	/** @iomem: CPU mapping of the registers. */
	void __iomem *regs;

	void __iomem *sram;
	struct gen_pool *srampool;
	dma_addr_t sramphys;

	struct clk_bulk_data *clks;
	int num_clks;
	int irq;

	struct drm_ethosu_npu_info npu_info;

	struct ethosu_job *in_flight_job;
	/* For in_flight_job and ethosu_job_hw_submit() */
	struct mutex job_lock;

	/* For dma_fence */
	spinlock_t fence_lock;

	struct drm_gpu_scheduler sched;
	/* For ethosu_job_do_push() */
	struct mutex sched_lock;
	u64 fence_context;
	u64 emit_seqno;
};

#define to_ethosu_device(drm_dev) \
	((struct ethosu_device *)container_of(drm_dev, struct ethosu_device, base))

static inline bool ethosu_is_u65(const struct ethosu_device *ethosudev)
{
	return FIELD_GET(ID_ARCH_MAJOR_MASK, ethosudev->npu_info.id) == 1;
}

#endif
