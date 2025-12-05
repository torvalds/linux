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
#define GPU_FAULT_ADDR					0x40
#define GPU_L2_CONFIG					0x48
#define   GPU_L2_CONFIG_ASN_HASH_ENABLE			BIT(24)

#define GPU_PWR_KEY					0x50
#define  GPU_PWR_KEY_UNLOCK				0x2968A819
#define GPU_PWR_OVERRIDE0				0x54
#define GPU_PWR_OVERRIDE1				0x58

#define GPU_FEATURES					0x60
#define   GPU_FEATURES_RAY_INTERSECTION			BIT(2)
#define   GPU_FEATURES_RAY_TRAVERSAL			BIT(5)

#define GPU_TIMESTAMP_OFFSET				0x88
#define GPU_CYCLE_COUNT					0x90
#define GPU_TIMESTAMP					0x98

#define GPU_THREAD_MAX_THREADS				0xA0
#define GPU_THREAD_MAX_WORKGROUP_SIZE			0xA4
#define GPU_THREAD_MAX_BARRIER_SIZE			0xA8
#define GPU_THREAD_FEATURES				0xAC

#define GPU_TEXTURE_FEATURES(n)				(0xB0 + ((n) * 4))

#define GPU_SHADER_PRESENT				0x100
#define GPU_TILER_PRESENT				0x110
#define GPU_L2_PRESENT					0x120

#define SHADER_READY					0x140
#define TILER_READY					0x150
#define L2_READY					0x160

#define SHADER_PWRON					0x180
#define TILER_PWRON					0x190
#define L2_PWRON					0x1A0

#define SHADER_PWROFF					0x1C0
#define TILER_PWROFF					0x1D0
#define L2_PWROFF					0x1E0

#define SHADER_PWRTRANS					0x200
#define TILER_PWRTRANS					0x210
#define L2_PWRTRANS					0x220

#define SHADER_PWRACTIVE				0x240
#define TILER_PWRACTIVE					0x250
#define L2_PWRACTIVE					0x260

#define GPU_REVID					0x280

#define GPU_ASN_HASH(n)					(0x2C0 + ((n) * 4))

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

#define AS_TRANSTAB(as)					(MMU_AS(as) + 0x0)
#define AS_MEMATTR(as)					(MMU_AS(as) + 0x8)
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
#define AS_LOCKADDR(as)					(MMU_AS(as) + 0x10)
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
#define AS_FAULTADDRESS(as)				(MMU_AS(as) + 0x20)
#define AS_STATUS(as)					(MMU_AS(as) + 0x28)
#define   AS_STATUS_AS_ACTIVE				BIT(0)
#define AS_TRANSCFG(as)					(MMU_AS(as) + 0x30)
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
#define AS_FAULTEXTRA(as)				(MMU_AS(as) + 0x38)

#define CSF_GPU_LATEST_FLUSH_ID				0x10000

#define CSF_DOORBELL(i)					(0x80000 + ((i) * 0x10000))
#define CSF_GLB_DOORBELL_ID				0

/* PWR Control registers */

#define PWR_CONTROL_BASE				0x800
#define PWR_CTRL_REG(x)					(PWR_CONTROL_BASE + (x))

#define PWR_INT_RAWSTAT					PWR_CTRL_REG(0x0)
#define PWR_INT_CLEAR					PWR_CTRL_REG(0x4)
#define PWR_INT_MASK					PWR_CTRL_REG(0x8)
#define PWR_INT_STAT					PWR_CTRL_REG(0xc)
#define   PWR_IRQ_POWER_CHANGED_SINGLE			BIT(0)
#define   PWR_IRQ_POWER_CHANGED_ALL			BIT(1)
#define   PWR_IRQ_DELEGATION_CHANGED			BIT(2)
#define   PWR_IRQ_RESET_COMPLETED			BIT(3)
#define   PWR_IRQ_RETRACT_COMPLETED			BIT(4)
#define   PWR_IRQ_INSPECT_COMPLETED			BIT(5)
#define   PWR_IRQ_COMMAND_NOT_ALLOWED			BIT(30)
#define   PWR_IRQ_COMMAND_INVALID			BIT(31)

#define PWR_STATUS					PWR_CTRL_REG(0x20)
#define   PWR_STATUS_ALLOW_L2				BIT_U64(0)
#define   PWR_STATUS_ALLOW_TILER			BIT_U64(1)
#define   PWR_STATUS_ALLOW_SHADER			BIT_U64(8)
#define   PWR_STATUS_ALLOW_BASE				BIT_U64(14)
#define   PWR_STATUS_ALLOW_STACK			BIT_U64(15)
#define   PWR_STATUS_DOMAIN_ALLOWED(x)			BIT_U64(x)
#define   PWR_STATUS_DELEGATED_L2			BIT_U64(16)
#define   PWR_STATUS_DELEGATED_TILER			BIT_U64(17)
#define   PWR_STATUS_DELEGATED_SHADER			BIT_U64(24)
#define   PWR_STATUS_DELEGATED_BASE			BIT_U64(30)
#define   PWR_STATUS_DELEGATED_STACK			BIT_U64(31)
#define   PWR_STATUS_DELEGATED_SHIFT			16
#define   PWR_STATUS_DOMAIN_DELEGATED(x)		BIT_U64((x) + PWR_STATUS_DELEGATED_SHIFT)
#define   PWR_STATUS_ALLOW_SOFT_RESET			BIT_U64(33)
#define   PWR_STATUS_ALLOW_FAST_RESET			BIT_U64(34)
#define   PWR_STATUS_POWER_PENDING			BIT_U64(41)
#define   PWR_STATUS_RESET_PENDING			BIT_U64(42)
#define   PWR_STATUS_RETRACT_PENDING			BIT_U64(43)
#define   PWR_STATUS_INSPECT_PENDING			BIT_U64(44)

#define PWR_COMMAND					PWR_CTRL_REG(0x28)
#define   PWR_COMMAND_POWER_UP				0x10
#define   PWR_COMMAND_POWER_DOWN			0x11
#define   PWR_COMMAND_DELEGATE				0x20
#define   PWR_COMMAND_RETRACT				0x21
#define   PWR_COMMAND_RESET_SOFT			0x31
#define   PWR_COMMAND_RESET_FAST			0x32
#define   PWR_COMMAND_INSPECT				0xF0
#define   PWR_COMMAND_DOMAIN_L2				0
#define   PWR_COMMAND_DOMAIN_TILER			1
#define   PWR_COMMAND_DOMAIN_SHADER			8
#define   PWR_COMMAND_DOMAIN_BASE			14
#define   PWR_COMMAND_DOMAIN_STACK			15
#define   PWR_COMMAND_SUBDOMAIN_RTU			BIT(0)
#define   PWR_COMMAND_DEF(cmd, domain, subdomain)	\
	(((subdomain) << 16) | ((domain) << 8) | (cmd))

#define PWR_CMDARG					PWR_CTRL_REG(0x30)

#define PWR_L2_PRESENT					PWR_CTRL_REG(0x100)
#define PWR_L2_READY					PWR_CTRL_REG(0x108)
#define PWR_L2_PWRTRANS					PWR_CTRL_REG(0x110)
#define PWR_L2_PWRACTIVE				PWR_CTRL_REG(0x118)
#define PWR_TILER_PRESENT				PWR_CTRL_REG(0x140)
#define PWR_TILER_READY					PWR_CTRL_REG(0x148)
#define PWR_TILER_PWRTRANS				PWR_CTRL_REG(0x150)
#define PWR_TILER_PWRACTIVE				PWR_CTRL_REG(0x158)
#define PWR_SHADER_PRESENT				PWR_CTRL_REG(0x200)
#define PWR_SHADER_READY				PWR_CTRL_REG(0x208)
#define PWR_SHADER_PWRTRANS				PWR_CTRL_REG(0x210)
#define PWR_SHADER_PWRACTIVE				PWR_CTRL_REG(0x218)
#define PWR_BASE_PRESENT				PWR_CTRL_REG(0x380)
#define PWR_BASE_READY					PWR_CTRL_REG(0x388)
#define PWR_BASE_PWRTRANS				PWR_CTRL_REG(0x390)
#define PWR_BASE_PWRACTIVE				PWR_CTRL_REG(0x398)
#define PWR_STACK_PRESENT				PWR_CTRL_REG(0x3c0)
#define PWR_STACK_READY					PWR_CTRL_REG(0x3c8)
#define PWR_STACK_PWRTRANS				PWR_CTRL_REG(0x3d0)

#endif
