/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/*
 * Register definitions based on mali_midg_regmap.h
 * (C) COPYRIGHT 2010-2018 ARM Limited. All rights reserved.
 */
#ifndef __PANFROST_REGS_H__
#define __PANFROST_REGS_H__

#define GPU_ID				0x00
#define GPU_L2_FEATURES			0x004	/* (RO) Level 2 cache features */
#define GPU_CORE_FEATURES		0x008	/* (RO) Shader Core Features */
#define GPU_TILER_FEATURES		0x00C	/* (RO) Tiler Features */
#define GPU_MEM_FEATURES		0x010	/* (RO) Memory system features */
#define   GROUPS_L2_COHERENT		BIT(0)	/* Cores groups are l2 coherent */

#define GPU_MMU_FEATURES		0x014	/* (RO) MMU features */
#define GPU_AS_PRESENT			0x018	/* (RO) Address space slots present */
#define GPU_JS_PRESENT			0x01C	/* (RO) Job slots present */

#define GPU_INT_RAWSTAT			0x20
#define GPU_INT_CLEAR			0x24
#define GPU_INT_MASK			0x28
#define GPU_INT_STAT			0x2c
#define   GPU_IRQ_FAULT			BIT(0)
#define   GPU_IRQ_MULTIPLE_FAULT	BIT(7)
#define   GPU_IRQ_RESET_COMPLETED	BIT(8)
#define   GPU_IRQ_POWER_CHANGED		BIT(9)
#define   GPU_IRQ_POWER_CHANGED_ALL	BIT(10)
#define   GPU_IRQ_PERFCNT_SAMPLE_COMPLETED BIT(16)
#define   GPU_IRQ_CLEAN_CACHES_COMPLETED BIT(17)
#define   GPU_IRQ_MASK_ALL			 \
	  (GPU_IRQ_FAULT			|\
	   GPU_IRQ_MULTIPLE_FAULT		|\
	   GPU_IRQ_RESET_COMPLETED		|\
	   GPU_IRQ_POWER_CHANGED		|\
	   GPU_IRQ_POWER_CHANGED_ALL		|\
	   GPU_IRQ_PERFCNT_SAMPLE_COMPLETED	|\
	   GPU_IRQ_CLEAN_CACHES_COMPLETED)
#define GPU_IRQ_MASK_ERROR	   		\
	(					\
	 GPU_IRQ_FAULT				|\
	 GPU_IRQ_MULTIPLE_FAULT)
#define GPU_CMD				0x30
#define   GPU_CMD_SOFT_RESET		0x01
#define GPU_STATUS			0x34
#define GPU_LATEST_FLUSH_ID		0x38
#define GPU_FAULT_STATUS		0x3C
#define GPU_FAULT_ADDRESS_LO		0x40
#define GPU_FAULT_ADDRESS_HI		0x44

#define GPU_THREAD_MAX_THREADS		0x0A0	/* (RO) Maximum number of threads per core */
#define GPU_THREAD_MAX_WORKGROUP_SIZE	0x0A4	/* (RO) Maximum workgroup size */
#define GPU_THREAD_MAX_BARRIER_SIZE	0x0A8	/* (RO) Maximum threads waiting at a barrier */
#define GPU_THREAD_FEATURES		0x0AC	/* (RO) Thread features */
#define GPU_THREAD_TLS_ALLOC		0x310   /* (RO) Number of threads per core that
						 * TLS must be allocated for */

#define GPU_TEXTURE_FEATURES(n)		(0x0B0 + ((n) * 4))
#define GPU_JS_FEATURES(n)		(0x0C0 + ((n) * 4))

#define GPU_SHADER_PRESENT_LO		0x100	/* (RO) Shader core present bitmap, low word */
#define GPU_SHADER_PRESENT_HI		0x104	/* (RO) Shader core present bitmap, high word */
#define GPU_TILER_PRESENT_LO		0x110	/* (RO) Tiler core present bitmap, low word */
#define GPU_TILER_PRESENT_HI		0x114	/* (RO) Tiler core present bitmap, high word */

#define GPU_L2_PRESENT_LO		0x120	/* (RO) Level 2 cache present bitmap, low word */
#define GPU_L2_PRESENT_HI		0x124	/* (RO) Level 2 cache present bitmap, high word */

#define GPU_COHERENCY_FEATURES		0x300	/* (RO) Coherency features present */
#define   COHERENCY_ACE_LITE		BIT(0)
#define   COHERENCY_ACE			BIT(1)

#define GPU_STACK_PRESENT_LO		0xE00   /* (RO) Core stack present bitmap, low word */
#define GPU_STACK_PRESENT_HI		0xE04   /* (RO) Core stack present bitmap, high word */

#define SHADER_READY_LO			0x140	/* (RO) Shader core ready bitmap, low word */
#define SHADER_READY_HI			0x144	/* (RO) Shader core ready bitmap, high word */

#define TILER_READY_LO			0x150	/* (RO) Tiler core ready bitmap, low word */
#define TILER_READY_HI			0x154	/* (RO) Tiler core ready bitmap, high word */

#define L2_READY_LO			0x160	/* (RO) Level 2 cache ready bitmap, low word */
#define L2_READY_HI			0x164	/* (RO) Level 2 cache ready bitmap, high word */

#define STACK_READY_LO			0xE10   /* (RO) Core stack ready bitmap, low word */
#define STACK_READY_HI			0xE14   /* (RO) Core stack ready bitmap, high word */


#define SHADER_PWRON_LO			0x180	/* (WO) Shader core power on bitmap, low word */
#define SHADER_PWRON_HI			0x184	/* (WO) Shader core power on bitmap, high word */

#define TILER_PWRON_LO			0x190	/* (WO) Tiler core power on bitmap, low word */
#define TILER_PWRON_HI			0x194	/* (WO) Tiler core power on bitmap, high word */

#define L2_PWRON_LO			0x1A0	/* (WO) Level 2 cache power on bitmap, low word */
#define L2_PWRON_HI			0x1A4	/* (WO) Level 2 cache power on bitmap, high word */

#define STACK_PWRON_LO			0xE20   /* (RO) Core stack power on bitmap, low word */
#define STACK_PWRON_HI			0xE24   /* (RO) Core stack power on bitmap, high word */


#define SHADER_PWROFF_LO		0x1C0	/* (WO) Shader core power off bitmap, low word */
#define SHADER_PWROFF_HI		0x1C4	/* (WO) Shader core power off bitmap, high word */

#define TILER_PWROFF_LO			0x1D0	/* (WO) Tiler core power off bitmap, low word */
#define TILER_PWROFF_HI			0x1D4	/* (WO) Tiler core power off bitmap, high word */

#define L2_PWROFF_LO			0x1E0	/* (WO) Level 2 cache power off bitmap, low word */
#define L2_PWROFF_HI			0x1E4	/* (WO) Level 2 cache power off bitmap, high word */

#define STACK_PWROFF_LO			0xE30   /* (RO) Core stack power off bitmap, low word */
#define STACK_PWROFF_HI			0xE34   /* (RO) Core stack power off bitmap, high word */


#define SHADER_PWRTRANS_LO		0x200	/* (RO) Shader core power transition bitmap, low word */
#define SHADER_PWRTRANS_HI		0x204	/* (RO) Shader core power transition bitmap, high word */

#define TILER_PWRTRANS_LO		0x210	/* (RO) Tiler core power transition bitmap, low word */
#define TILER_PWRTRANS_HI		0x214	/* (RO) Tiler core power transition bitmap, high word */

#define L2_PWRTRANS_LO			0x220	/* (RO) Level 2 cache power transition bitmap, low word */
#define L2_PWRTRANS_HI			0x224	/* (RO) Level 2 cache power transition bitmap, high word */

#define STACK_PWRTRANS_LO		0xE40   /* (RO) Core stack power transition bitmap, low word */
#define STACK_PWRTRANS_HI		0xE44   /* (RO) Core stack power transition bitmap, high word */


#define SHADER_PWRACTIVE_LO		0x240	/* (RO) Shader core active bitmap, low word */
#define SHADER_PWRACTIVE_HI		0x244	/* (RO) Shader core active bitmap, high word */

#define TILER_PWRACTIVE_LO		0x250	/* (RO) Tiler core active bitmap, low word */
#define TILER_PWRACTIVE_HI		0x254	/* (RO) Tiler core active bitmap, high word */

#define L2_PWRACTIVE_LO			0x260	/* (RO) Level 2 cache active bitmap, low word */
#define L2_PWRACTIVE_HI			0x264	/* (RO) Level 2 cache active bitmap, high word */

#define GPU_JM_CONFIG			0xF00   /* (RW) Job Manager configuration register (Implementation specific register) */
#define GPU_SHADER_CONFIG		0xF04	/* (RW) Shader core configuration settings (Implementation specific register) */
#define GPU_TILER_CONFIG		0xF08   /* (RW) Tiler core configuration settings (Implementation specific register) */
#define GPU_L2_MMU_CONFIG		0xF0C	/* (RW) Configuration of the L2 cache and MMU (Implementation specific register) */

/* L2_MMU_CONFIG register */
#define L2_MMU_CONFIG_ALLOW_SNOOP_DISPARITY_SHIFT	23
#define L2_MMU_CONFIG_ALLOW_SNOOP_DISPARITY		(0x1 << L2_MMU_CONFIG_ALLOW_SNOOP_DISPARITY_SHIFT)
#define L2_MMU_CONFIG_LIMIT_EXTERNAL_READS_SHIFT	24
#define L2_MMU_CONFIG_LIMIT_EXTERNAL_READS		(0x3 << L2_MMU_CONFIG_LIMIT_EXTERNAL_READS_SHIFT)
#define L2_MMU_CONFIG_LIMIT_EXTERNAL_READS_OCTANT	(0x1 << L2_MMU_CONFIG_LIMIT_EXTERNAL_READS_SHIFT)
#define L2_MMU_CONFIG_LIMIT_EXTERNAL_READS_QUARTER	(0x2 << L2_MMU_CONFIG_LIMIT_EXTERNAL_READS_SHIFT)
#define L2_MMU_CONFIG_LIMIT_EXTERNAL_READS_HALF		(0x3 << L2_MMU_CONFIG_LIMIT_EXTERNAL_READS_SHIFT)

#define L2_MMU_CONFIG_LIMIT_EXTERNAL_WRITES_SHIFT	26
#define L2_MMU_CONFIG_LIMIT_EXTERNAL_WRITES		(0x3 << L2_MMU_CONFIG_LIMIT_EXTERNAL_WRITES_SHIFT)
#define L2_MMU_CONFIG_LIMIT_EXTERNAL_WRITES_OCTANT	(0x1 << L2_MMU_CONFIG_LIMIT_EXTERNAL_WRITES_SHIFT)
#define L2_MMU_CONFIG_LIMIT_EXTERNAL_WRITES_QUARTER	(0x2 << L2_MMU_CONFIG_LIMIT_EXTERNAL_WRITES_SHIFT)
#define L2_MMU_CONFIG_LIMIT_EXTERNAL_WRITES_HALF	(0x3 << L2_MMU_CONFIG_LIMIT_EXTERNAL_WRITES_SHIFT)

#define L2_MMU_CONFIG_3BIT_LIMIT_EXTERNAL_READS_SHIFT	12
#define L2_MMU_CONFIG_3BIT_LIMIT_EXTERNAL_READS		(0x7 << L2_MMU_CONFIG_LIMIT_EXTERNAL_READS_SHIFT)

#define L2_MMU_CONFIG_3BIT_LIMIT_EXTERNAL_WRITES_SHIFT	15
#define L2_MMU_CONFIG_3BIT_LIMIT_EXTERNAL_WRITES	(0x7 << L2_MMU_CONFIG_LIMIT_EXTERNAL_WRITES_SHIFT)

/* SHADER_CONFIG register */
#define SC_ALT_COUNTERS			BIT(3)
#define SC_OVERRIDE_FWD_PIXEL_KILL	BIT(4)
#define SC_SDC_DISABLE_OQ_DISCARD	BIT(6)
#define SC_LS_ALLOW_ATTR_TYPES		BIT(16)
#define SC_LS_PAUSEBUFFER_DISABLE	BIT(16)
#define SC_TLS_HASH_ENABLE		BIT(17)
#define SC_LS_ATTR_CHECK_DISABLE	BIT(18)
#define SC_ENABLE_TEXGRD_FLAGS		BIT(25)
/* End SHADER_CONFIG register */

/* TILER_CONFIG register */
#define TC_CLOCK_GATE_OVERRIDE		BIT(0)

/* JM_CONFIG register */
#define JM_TIMESTAMP_OVERRIDE		BIT(0)
#define JM_CLOCK_GATE_OVERRIDE		BIT(1)
#define JM_JOB_THROTTLE_ENABLE		BIT(2)
#define JM_JOB_THROTTLE_LIMIT_SHIFT	3
#define JM_MAX_JOB_THROTTLE_LIMIT	0x3F
#define JM_FORCE_COHERENCY_FEATURES_SHIFT 2
#define JM_IDVS_GROUP_SIZE_SHIFT	16
#define JM_MAX_IDVS_GROUP_SIZE		0x3F


/* Job Control regs */
#define JOB_INT_RAWSTAT			0x1000
#define JOB_INT_CLEAR			0x1004
#define JOB_INT_MASK			0x1008
#define JOB_INT_STAT			0x100c
#define JOB_INT_JS_STATE		0x1010
#define JOB_INT_THROTTLE		0x1014

#define MK_JS_MASK(j)			(0x10001 << (j))
#define JOB_INT_MASK_ERR(j)		BIT((j) + 16)
#define JOB_INT_MASK_DONE(j)		BIT(j)

#define JS_BASE				0x1800
#define JS_HEAD_LO(n)			(JS_BASE + ((n) * 0x80) + 0x00)
#define JS_HEAD_HI(n)			(JS_BASE + ((n) * 0x80) + 0x04)
#define JS_TAIL_LO(n)			(JS_BASE + ((n) * 0x80) + 0x08)
#define JS_TAIL_HI(n)			(JS_BASE + ((n) * 0x80) + 0x0c)
#define JS_AFFINITY_LO(n)		(JS_BASE + ((n) * 0x80) + 0x10)
#define JS_AFFINITY_HI(n)		(JS_BASE + ((n) * 0x80) + 0x14)
#define JS_CONFIG(n)			(JS_BASE + ((n) * 0x80) + 0x18)
#define JS_XAFFINITY(n)			(JS_BASE + ((n) * 0x80) + 0x1c)
#define JS_COMMAND(n)			(JS_BASE + ((n) * 0x80) + 0x20)
#define JS_STATUS(n)			(JS_BASE + ((n) * 0x80) + 0x24)
#define JS_HEAD_NEXT_LO(n)		(JS_BASE + ((n) * 0x80) + 0x40)
#define JS_HEAD_NEXT_HI(n)		(JS_BASE + ((n) * 0x80) + 0x44)
#define JS_AFFINITY_NEXT_LO(n)		(JS_BASE + ((n) * 0x80) + 0x50)
#define JS_AFFINITY_NEXT_HI(n)		(JS_BASE + ((n) * 0x80) + 0x54)
#define JS_CONFIG_NEXT(n)		(JS_BASE + ((n) * 0x80) + 0x58)
#define JS_COMMAND_NEXT(n)		(JS_BASE + ((n) * 0x80) + 0x60)
#define JS_FLUSH_ID_NEXT(n)		(JS_BASE + ((n) * 0x80) + 0x70)

/* Possible values of JS_CONFIG and JS_CONFIG_NEXT registers */
#define JS_CONFIG_START_FLUSH_CLEAN		BIT(8)
#define JS_CONFIG_START_FLUSH_CLEAN_INVALIDATE	(3u << 8)
#define JS_CONFIG_START_MMU			BIT(10)
#define JS_CONFIG_JOB_CHAIN_FLAG		BIT(11)
#define JS_CONFIG_END_FLUSH_CLEAN		BIT(12)
#define JS_CONFIG_END_FLUSH_CLEAN_INVALIDATE	(3u << 12)
#define JS_CONFIG_ENABLE_FLUSH_REDUCTION	BIT(14)
#define JS_CONFIG_DISABLE_DESCRIPTOR_WR_BK	BIT(15)
#define JS_CONFIG_THREAD_PRI(n)			((n) << 16)

#define JS_COMMAND_NOP			0x00
#define JS_COMMAND_START		0x01
#define JS_COMMAND_SOFT_STOP		0x02	/* Gently stop processing a job chain */
#define JS_COMMAND_HARD_STOP		0x03	/* Rudely stop processing a job chain */
#define JS_COMMAND_SOFT_STOP_0		0x04	/* Execute SOFT_STOP if JOB_CHAIN_FLAG is 0 */
#define JS_COMMAND_HARD_STOP_0		0x05	/* Execute HARD_STOP if JOB_CHAIN_FLAG is 0 */
#define JS_COMMAND_SOFT_STOP_1		0x06	/* Execute SOFT_STOP if JOB_CHAIN_FLAG is 1 */
#define JS_COMMAND_HARD_STOP_1		0x07	/* Execute HARD_STOP if JOB_CHAIN_FLAG is 1 */

#define JS_STATUS_EVENT_ACTIVE		0x08


/* MMU regs */
#define MMU_INT_RAWSTAT			0x2000
#define MMU_INT_CLEAR			0x2004
#define MMU_INT_MASK			0x2008
#define MMU_INT_STAT			0x200c

/* AS_COMMAND register commands */
#define AS_COMMAND_NOP			0x00	/* NOP Operation */
#define AS_COMMAND_UPDATE		0x01	/* Broadcasts the values in AS_TRANSTAB and ASn_MEMATTR to all MMUs */
#define AS_COMMAND_LOCK			0x02	/* Issue a lock region command to all MMUs */
#define AS_COMMAND_UNLOCK		0x03	/* Issue a flush region command to all MMUs */
#define AS_COMMAND_FLUSH		0x04	/* Flush all L2 caches then issue a flush region command to all MMUs
						   (deprecated - only for use with T60x) */
#define AS_COMMAND_FLUSH_PT		0x04	/* Flush all L2 caches then issue a flush region command to all MMUs */
#define AS_COMMAND_FLUSH_MEM		0x05	/* Wait for memory accesses to complete, flush all the L1s cache then
						   flush all L2 caches then issue a flush region command to all MMUs */

#define MMU_AS(as)			(0x2400 + ((as) << 6))

#define AS_TRANSTAB_LO(as)		(MMU_AS(as) + 0x00) /* (RW) Translation Table Base Address for address space n, low word */
#define AS_TRANSTAB_HI(as)		(MMU_AS(as) + 0x04) /* (RW) Translation Table Base Address for address space n, high word */
#define AS_MEMATTR_LO(as)		(MMU_AS(as) + 0x08) /* (RW) Memory attributes for address space n, low word. */
#define AS_MEMATTR_HI(as)		(MMU_AS(as) + 0x0C) /* (RW) Memory attributes for address space n, high word. */
#define AS_LOCKADDR_LO(as)		(MMU_AS(as) + 0x10) /* (RW) Lock region address for address space n, low word */
#define AS_LOCKADDR_HI(as)		(MMU_AS(as) + 0x14) /* (RW) Lock region address for address space n, high word */
#define AS_COMMAND(as)			(MMU_AS(as) + 0x18) /* (WO) MMU command register for address space n */
#define AS_FAULTSTATUS(as)		(MMU_AS(as) + 0x1C) /* (RO) MMU fault status register for address space n */
#define AS_FAULTADDRESS_LO(as)		(MMU_AS(as) + 0x20) /* (RO) Fault Address for address space n, low word */
#define AS_FAULTADDRESS_HI(as)		(MMU_AS(as) + 0x24) /* (RO) Fault Address for address space n, high word */
#define AS_STATUS(as)			(MMU_AS(as) + 0x28) /* (RO) Status flags for address space n */
/* Additional Bifrost AS regsiters */
#define AS_TRANSCFG_LO(as)		(MMU_AS(as) + 0x30) /* (RW) Translation table configuration for address space n, low word */
#define AS_TRANSCFG_HI(as)		(MMU_AS(as) + 0x34) /* (RW) Translation table configuration for address space n, high word */
#define AS_FAULTEXTRA_LO(as)		(MMU_AS(as) + 0x38) /* (RO) Secondary fault address for address space n, low word */
#define AS_FAULTEXTRA_HI(as)		(MMU_AS(as) + 0x3C) /* (RO) Secondary fault address for address space n, high word */

/*
 * Begin LPAE MMU TRANSTAB register values
 */
#define AS_TRANSTAB_LPAE_ADDR_SPACE_MASK	0xfffffffffffff000
#define AS_TRANSTAB_LPAE_ADRMODE_IDENTITY	0x2
#define AS_TRANSTAB_LPAE_ADRMODE_TABLE		0x3
#define AS_TRANSTAB_LPAE_ADRMODE_MASK		0x3
#define AS_TRANSTAB_LPAE_READ_INNER		BIT(2)
#define AS_TRANSTAB_LPAE_SHARE_OUTER		BIT(4)

#define AS_STATUS_AS_ACTIVE			0x01

#define AS_FAULTSTATUS_ACCESS_TYPE_MASK		(0x3 << 8)
#define AS_FAULTSTATUS_ACCESS_TYPE_ATOMIC	(0x0 << 8)
#define AS_FAULTSTATUS_ACCESS_TYPE_EX		(0x1 << 8)
#define AS_FAULTSTATUS_ACCESS_TYPE_READ		(0x2 << 8)
#define AS_FAULTSTATUS_ACCESS_TYPE_WRITE	(0x3 << 8)

#endif
