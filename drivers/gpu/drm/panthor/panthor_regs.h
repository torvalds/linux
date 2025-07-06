/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/* Copyright 2023 Collabora ltd. */
/*
 * Register definitions based on mali_kbase_gpu_regmap.h and
 * mali_kbase_gpu_regmap_csf.h
 * (C) COPYRIGHT 2010-2022 ARM Limited. All rights reserved.
 */
#ifndef __PANTHOR_REGS_H__
#define __PANTHOR_REGS_H__

#define GPU_ID						0x0
#define   GPU_ARCH_MAJOR(x)				((x) >> 28)
#define   GPU_ARCH_MINOR(x)				(((x) & GENMASK(27, 24)) >> 24)
#define   GPU_ARCH_REV(x)				(((x) & GENMASK(23, 20)) >> 20)
#define   GPU_PROD_MAJOR(x)				(((x) & GENMASK(19, 16)) >> 16)
#define   GPU_VER_MAJOR(x)				(((x) & GENMASK(15, 12)) >> 12)
#define   GPU_VER_MINOR(x)				(((x) & GENMASK(11, 4)) >> 4)
#define   GPU_VER_STATUS(x)				((x) & GENMASK(3, 0))

#define GPU_L2_FEATURES					0x4
#define  GPU_L2_FEATURES_LINE_SIZE(x)			(1 << ((x) & GENMASK(7, 0)))

#define GPU_CORE_FEATURES				0x8

#define GPU_TILER_FEATURES				0xC
#define GPU_MEM_FEATURES				0x10
#define   GROUPS_L2_COHERENT				BIT(0)

#define GPU_MMU_FEATURES				0x14
#define  GPU_MMU_FEATURES_VA_BITS(x)			((x) & GENMASK(7, 0))
#define  GPU_MMU_FEATURES_PA_BITS(x)			(((x) >> 8) & GENMASK(7, 0))
#define GPU_AS_PRESENT					0x18
#define GPU_CSF_ID					0x1C

#define GPU_INT_RAWSTAT					0x20
#define GPU_INT_CLEAR					0x24
#define GPU_INT_MASK					0x28
#define GPU_INT_STAT					0x2c
#define   GPU_IRQ_FAULT					BIT(0)
#define   GPU_IRQ_PROTM_FAULT				BIT(1)
#define   GPU_IRQ_RESET_COMPLETED			BIT(8)
#define   GPU_IRQ_POWER_CHANGED				BIT(9)
#define   GPU_IRQ_POWER_CHANGED_ALL			BIT(10)
#define   GPU_IRQ_CLEAN_CACHES_COMPLETED		BIT(17)
#define   GPU_IRQ_DOORBELL_MIRROR			BIT(18)
#define   GPU_IRQ_MCU_STATUS_CHANGED			BIT(19)
#define GPU_CMD						0x30
#define   GPU_CMD_DEF(type, payload)			((type) | ((payload) << 8))
#define   GPU_SOFT_RESET				GPU_CMD_DEF(1, 1)
#define   GPU_HARD_RESET				GPU_CMD_DEF(1, 2)
#define   CACHE_CLEAN					BIT(0)
#define   CACHE_INV					BIT(1)
#define   GPU_FLUSH_CACHES(l2, lsc, oth)		\
	  GPU_CMD_DEF(4, ((l2) << 0) | ((lsc) << 4) | ((oth) << 8))

#define GPU_STATUS					0x34
#define   GPU_STATUS_ACTIVE				BIT(0)
#define   GPU_STATUS_PWR_ACTIVE				BIT(1)
#define   GPU_STATUS_PAGE_FAULT				BIT(4)
#define   GPU_STATUS_PROTM_ACTIVE			BIT(7)
#define   GPU_STATUS_DBG_ENABLED			BIT(8)

#define GPU_FAULT_STATUS				0x3C
#define GPU_FAULT_ADDR_LO				0x40
#define GPU_FAULT_ADDR_HI				0x44

#define GPU_PWR_KEY					0x50
#define  GPU_PWR_KEY_UNLOCK				0x2968A819
#define GPU_PWR_OVERRIDE0				0x54
#define GPU_PWR_OVERRIDE1				0x58

#define GPU_TIMESTAMP_OFFSET_LO				0x88
#define GPU_TIMESTAMP_OFFSET_HI				0x8C
#define GPU_CYCLE_COUNT_LO				0x90
#define GPU_CYCLE_COUNT_HI				0x94
#define GPU_TIMESTAMP_LO				0x98
#define GPU_TIMESTAMP_HI				0x9C

#define GPU_THREAD_MAX_THREADS				0xA0
#define GPU_THREAD_MAX_WORKGROUP_SIZE			0xA4
#define GPU_THREAD_MAX_BARRIER_SIZE			0xA8
#define GPU_THREAD_FEATURES				0xAC

#define GPU_TEXTURE_FEATURES(n)				(0xB0 + ((n) * 4))

#define GPU_SHADER_PRESENT_LO				0x100
#define GPU_SHADER_PRESENT_HI				0x104
#define GPU_TILER_PRESENT_LO				0x110
#define GPU_TILER_PRESENT_HI				0x114
#define GPU_L2_PRESENT_LO				0x120
#define GPU_L2_PRESENT_HI				0x124

#define SHADER_READY_LO					0x140
#define SHADER_READY_HI					0x144
#define TILER_READY_LO					0x150
#define TILER_READY_HI					0x154
#define L2_READY_LO					0x160
#define L2_READY_HI					0x164

#define SHADER_PWRON_LO					0x180
#define SHADER_PWRON_HI					0x184
#define TILER_PWRON_LO					0x190
#define TILER_PWRON_HI					0x194
#define L2_PWRON_LO					0x1A0
#define L2_PWRON_HI					0x1A4

#define SHADER_PWROFF_LO				0x1C0
#define SHADER_PWROFF_HI				0x1C4
#define TILER_PWROFF_LO					0x1D0
#define TILER_PWROFF_HI					0x1D4
#define L2_PWROFF_LO					0x1E0
#define L2_PWROFF_HI					0x1E4

#define SHADER_PWRTRANS_LO				0x200
#define SHADER_PWRTRANS_HI				0x204
#define TILER_PWRTRANS_LO				0x210
#define TILER_PWRTRANS_HI				0x214
#define L2_PWRTRANS_LO					0x220
#define L2_PWRTRANS_HI					0x224

#define SHADER_PWRACTIVE_LO				0x240
#define SHADER_PWRACTIVE_HI				0x244
#define TILER_PWRACTIVE_LO				0x250
#define TILER_PWRACTIVE_HI				0x254
#define L2_PWRACTIVE_LO					0x260
#define L2_PWRACTIVE_HI					0x264

#define GPU_REVID					0x280

#define GPU_COHERENCY_FEATURES				0x300
#define GPU_COHERENCY_PROT_BIT(name)			BIT(GPU_COHERENCY_  ## name)

#define GPU_COHERENCY_PROTOCOL				0x304
#define   GPU_COHERENCY_ACE_LITE			0
#define   GPU_COHERENCY_ACE				1
#define   GPU_COHERENCY_NONE				31

#define MCU_CONTROL					0x700
#define MCU_CONTROL_ENABLE				1
#define MCU_CONTROL_AUTO				2
#define MCU_CONTROL_DISABLE				0

#define MCU_STATUS					0x704
#define MCU_STATUS_DISABLED				0
#define MCU_STATUS_ENABLED				1
#define MCU_STATUS_HALT					2
#define MCU_STATUS_FATAL				3

/* Job Control regs */
#define JOB_INT_RAWSTAT					0x1000
#define JOB_INT_CLEAR					0x1004
#define JOB_INT_MASK					0x1008
#define JOB_INT_STAT					0x100c
#define   JOB_INT_GLOBAL_IF				BIT(31)
#define   JOB_INT_CSG_IF(x)				BIT(x)

/* MMU regs */
#define MMU_INT_RAWSTAT					0x2000
#define MMU_INT_CLEAR					0x2004
#define MMU_INT_MASK					0x2008
#define MMU_INT_STAT					0x200c

/* AS_COMMAND register commands */

#define MMU_BASE					0x2400
#define MMU_AS_SHIFT					6
#define MMU_AS(as)					(MMU_BASE + ((as) << MMU_AS_SHIFT))

#define AS_TRANSTAB_LO(as)				(MMU_AS(as) + 0x0)
#define AS_TRANSTAB_HI(as)				(MMU_AS(as) + 0x4)
#define AS_MEMATTR_LO(as)				(MMU_AS(as) + 0x8)
#define AS_MEMATTR_HI(as)				(MMU_AS(as) + 0xC)
#define   AS_MEMATTR_AARCH64_INNER_ALLOC_IMPL		(2 << 2)
#define   AS_MEMATTR_AARCH64_INNER_ALLOC_EXPL(w, r)	((3 << 2) | \
							 ((w) ? BIT(0) : 0) | \
							 ((r) ? BIT(1) : 0))
#define   AS_MEMATTR_AARCH64_SH_MIDGARD_INNER		(0 << 4)
#define   AS_MEMATTR_AARCH64_SH_CPU_INNER		(1 << 4)
#define   AS_MEMATTR_AARCH64_SH_CPU_INNER_SHADER_COH	(2 << 4)
#define   AS_MEMATTR_AARCH64_SHARED			(0 << 6)
#define   AS_MEMATTR_AARCH64_INNER_OUTER_NC		(1 << 6)
#define   AS_MEMATTR_AARCH64_INNER_OUTER_WB		(2 << 6)
#define   AS_MEMATTR_AARCH64_FAULT			(3 << 6)
#define AS_LOCKADDR_LO(as)				(MMU_AS(as) + 0x10)
#define AS_LOCKADDR_HI(as)				(MMU_AS(as) + 0x14)
#define AS_COMMAND(as)					(MMU_AS(as) + 0x18)
#define   AS_COMMAND_NOP				0
#define   AS_COMMAND_UPDATE				1
#define   AS_COMMAND_LOCK				2
#define   AS_COMMAND_UNLOCK				3
#define   AS_COMMAND_FLUSH_PT				4
#define   AS_COMMAND_FLUSH_MEM				5
#define   AS_LOCK_REGION_MIN_SIZE			(1ULL << 15)
#define AS_FAULTSTATUS(as)				(MMU_AS(as) + 0x1C)
#define  AS_FAULTSTATUS_ACCESS_TYPE_MASK		(0x3 << 8)
#define  AS_FAULTSTATUS_ACCESS_TYPE_ATOMIC		(0x0 << 8)
#define  AS_FAULTSTATUS_ACCESS_TYPE_EX			(0x1 << 8)
#define  AS_FAULTSTATUS_ACCESS_TYPE_READ		(0x2 << 8)
#define  AS_FAULTSTATUS_ACCESS_TYPE_WRITE		(0x3 << 8)
#define AS_FAULTADDRESS_LO(as)				(MMU_AS(as) + 0x20)
#define AS_FAULTADDRESS_HI(as)				(MMU_AS(as) + 0x24)
#define AS_STATUS(as)					(MMU_AS(as) + 0x28)
#define   AS_STATUS_AS_ACTIVE				BIT(0)
#define AS_TRANSCFG_LO(as)				(MMU_AS(as) + 0x30)
#define AS_TRANSCFG_HI(as)				(MMU_AS(as) + 0x34)
#define   AS_TRANSCFG_ADRMODE_UNMAPPED			(1 << 0)
#define   AS_TRANSCFG_ADRMODE_IDENTITY			(2 << 0)
#define   AS_TRANSCFG_ADRMODE_AARCH64_4K		(6 << 0)
#define   AS_TRANSCFG_ADRMODE_AARCH64_64K		(8 << 0)
#define   AS_TRANSCFG_INA_BITS(x)			((x) << 6)
#define   AS_TRANSCFG_OUTA_BITS(x)			((x) << 14)
#define   AS_TRANSCFG_SL_CONCAT				BIT(22)
#define   AS_TRANSCFG_PTW_MEMATTR_NC			(1 << 24)
#define   AS_TRANSCFG_PTW_MEMATTR_WB			(2 << 24)
#define   AS_TRANSCFG_PTW_SH_NS				(0 << 28)
#define   AS_TRANSCFG_PTW_SH_OS				(2 << 28)
#define   AS_TRANSCFG_PTW_SH_IS				(3 << 28)
#define   AS_TRANSCFG_PTW_RA				BIT(30)
#define   AS_TRANSCFG_DISABLE_HIER_AP			BIT(33)
#define   AS_TRANSCFG_DISABLE_AF_FAULT			BIT(34)
#define   AS_TRANSCFG_WXN				BIT(35)
#define   AS_TRANSCFG_XREADABLE				BIT(36)
#define AS_FAULTEXTRA_LO(as)				(MMU_AS(as) + 0x38)
#define AS_FAULTEXTRA_HI(as)				(MMU_AS(as) + 0x3C)

#define CSF_GPU_LATEST_FLUSH_ID				0x10000

#define CSF_DOORBELL(i)					(0x80000 + ((i) * 0x10000))
#define CSF_GLB_DOORBELL_ID				0

#define gpu_write(dev, reg, data) \
	writel(data, (dev)->iomem + (reg))

#define gpu_read(dev, reg) \
	readl((dev)->iomem + (reg))

#endif
