/*
 *
 * (C) COPYRIGHT 2019-2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#ifndef _KBASE_GPU_REGMAP_JM_H_
#define _KBASE_GPU_REGMAP_JM_H_

#if MALI_USE_CSF
#error "Cannot be compiled with CSF"
#endif

/* Set to implementation defined, outer caching */
#define AS_MEMATTR_AARCH64_OUTER_IMPL_DEF 0x88ull
/* Set to write back memory, outer caching */
#define AS_MEMATTR_AARCH64_OUTER_WA       0x8Dull
/* Set to inner non-cacheable, outer-non-cacheable
 * Setting defined by the alloc bits is ignored, but set to a valid encoding:
 * - no-alloc on read
 * - no alloc on write
 */
#define AS_MEMATTR_AARCH64_NON_CACHEABLE  0x4Cull

/* Symbols for default MEMATTR to use
 * Default is - HW implementation defined caching
 */
#define AS_MEMATTR_INDEX_DEFAULT               0
#define AS_MEMATTR_INDEX_DEFAULT_ACE           3

/* HW implementation defined caching */
#define AS_MEMATTR_INDEX_IMPL_DEF_CACHE_POLICY 0
/* Force cache on */
#define AS_MEMATTR_INDEX_FORCE_TO_CACHE_ALL    1
/* Write-alloc */
#define AS_MEMATTR_INDEX_WRITE_ALLOC           2
/* Outer coherent, inner implementation defined policy */
#define AS_MEMATTR_INDEX_OUTER_IMPL_DEF        3
/* Outer coherent, write alloc inner */
#define AS_MEMATTR_INDEX_OUTER_WA              4
/* Normal memory, inner non-cacheable, outer non-cacheable (ARMv8 mode only) */
#define AS_MEMATTR_INDEX_NON_CACHEABLE         5

/* GPU control registers */

#define CORE_FEATURES           0x008   /* (RO) Shader Core Features */
#define JS_PRESENT              0x01C   /* (RO) Job slots present */
#define LATEST_FLUSH            0x038   /* (RO) Flush ID of latest
					 * clean-and-invalidate operation
					 */

#define PRFCNT_BASE_LO   0x060  /* (RW) Performance counter memory
				 * region base address, low word
				 */
#define PRFCNT_BASE_HI   0x064  /* (RW) Performance counter memory
				 * region base address, high word
				 */
#define PRFCNT_CONFIG    0x068  /* (RW) Performance counter
				 * configuration
				 */
#define PRFCNT_JM_EN     0x06C  /* (RW) Performance counter enable
				 * flags for Job Manager
				 */
#define PRFCNT_SHADER_EN 0x070  /* (RW) Performance counter enable
				 * flags for shader cores
				 */
#define PRFCNT_TILER_EN  0x074  /* (RW) Performance counter enable
				 * flags for tiler
				 */
#define PRFCNT_MMU_L2_EN 0x07C  /* (RW) Performance counter enable
				 * flags for MMU/L2 cache
				 */

#define JS0_FEATURES            0x0C0   /* (RO) Features of job slot 0 */
#define JS1_FEATURES            0x0C4   /* (RO) Features of job slot 1 */
#define JS2_FEATURES            0x0C8   /* (RO) Features of job slot 2 */
#define JS3_FEATURES            0x0CC   /* (RO) Features of job slot 3 */
#define JS4_FEATURES            0x0D0   /* (RO) Features of job slot 4 */
#define JS5_FEATURES            0x0D4   /* (RO) Features of job slot 5 */
#define JS6_FEATURES            0x0D8   /* (RO) Features of job slot 6 */
#define JS7_FEATURES            0x0DC   /* (RO) Features of job slot 7 */
#define JS8_FEATURES            0x0E0   /* (RO) Features of job slot 8 */
#define JS9_FEATURES            0x0E4   /* (RO) Features of job slot 9 */
#define JS10_FEATURES           0x0E8   /* (RO) Features of job slot 10 */
#define JS11_FEATURES           0x0EC   /* (RO) Features of job slot 11 */
#define JS12_FEATURES           0x0F0   /* (RO) Features of job slot 12 */
#define JS13_FEATURES           0x0F4   /* (RO) Features of job slot 13 */
#define JS14_FEATURES           0x0F8   /* (RO) Features of job slot 14 */
#define JS15_FEATURES           0x0FC   /* (RO) Features of job slot 15 */

#define JS_FEATURES_REG(n)      GPU_CONTROL_REG(JS0_FEATURES + ((n) << 2))

#define JM_CONFIG               0xF00   /* (RW) Job manager configuration (implementation-specific) */

/* Job control registers */

#define JOB_IRQ_JS_STATE        0x010   /* status==active and _next == busy snapshot from last JOB_IRQ_CLEAR */
#define JOB_IRQ_THROTTLE        0x014   /* cycles to delay delivering an interrupt externally. The JOB_IRQ_STATUS is NOT affected by this, just the delivery of the interrupt.  */

#define JOB_SLOT0               0x800   /* Configuration registers for job slot 0 */
#define JOB_SLOT1               0x880   /* Configuration registers for job slot 1 */
#define JOB_SLOT2               0x900   /* Configuration registers for job slot 2 */
#define JOB_SLOT3               0x980   /* Configuration registers for job slot 3 */
#define JOB_SLOT4               0xA00   /* Configuration registers for job slot 4 */
#define JOB_SLOT5               0xA80   /* Configuration registers for job slot 5 */
#define JOB_SLOT6               0xB00   /* Configuration registers for job slot 6 */
#define JOB_SLOT7               0xB80   /* Configuration registers for job slot 7 */
#define JOB_SLOT8               0xC00   /* Configuration registers for job slot 8 */
#define JOB_SLOT9               0xC80   /* Configuration registers for job slot 9 */
#define JOB_SLOT10              0xD00   /* Configuration registers for job slot 10 */
#define JOB_SLOT11              0xD80   /* Configuration registers for job slot 11 */
#define JOB_SLOT12              0xE00   /* Configuration registers for job slot 12 */
#define JOB_SLOT13              0xE80   /* Configuration registers for job slot 13 */
#define JOB_SLOT14              0xF00   /* Configuration registers for job slot 14 */
#define JOB_SLOT15              0xF80   /* Configuration registers for job slot 15 */

#define JOB_SLOT_REG(n, r)      (JOB_CONTROL_REG(JOB_SLOT0 + ((n) << 7)) + (r))

#define JS_HEAD_LO             0x00	/* (RO) Job queue head pointer for job slot n, low word */
#define JS_HEAD_HI             0x04	/* (RO) Job queue head pointer for job slot n, high word */
#define JS_TAIL_LO             0x08	/* (RO) Job queue tail pointer for job slot n, low word */
#define JS_TAIL_HI             0x0C	/* (RO) Job queue tail pointer for job slot n, high word */
#define JS_AFFINITY_LO         0x10	/* (RO) Core affinity mask for job slot n, low word */
#define JS_AFFINITY_HI         0x14	/* (RO) Core affinity mask for job slot n, high word */
#define JS_CONFIG              0x18	/* (RO) Configuration settings for job slot n */
#define JS_XAFFINITY           0x1C	/* (RO) Extended affinity mask for job
					   slot n */

#define JS_COMMAND             0x20	/* (WO) Command register for job slot n */
#define JS_STATUS              0x24	/* (RO) Status register for job slot n */

#define JS_HEAD_NEXT_LO        0x40	/* (RW) Next job queue head pointer for job slot n, low word */
#define JS_HEAD_NEXT_HI        0x44	/* (RW) Next job queue head pointer for job slot n, high word */

#define JS_AFFINITY_NEXT_LO    0x50	/* (RW) Next core affinity mask for job slot n, low word */
#define JS_AFFINITY_NEXT_HI    0x54	/* (RW) Next core affinity mask for job slot n, high word */
#define JS_CONFIG_NEXT         0x58	/* (RW) Next configuration settings for job slot n */
#define JS_XAFFINITY_NEXT      0x5C	/* (RW) Next extended affinity mask for
					   job slot n */

#define JS_COMMAND_NEXT        0x60	/* (RW) Next command register for job slot n */

#define JS_FLUSH_ID_NEXT       0x70	/* (RW) Next job slot n cache flush ID */

/* No JM-specific MMU control registers */
/* No JM-specific MMU address space control registers */

/* JS_COMMAND register commands */
#define JS_COMMAND_NOP         0x00	/* NOP Operation. Writing this value is ignored */
#define JS_COMMAND_START       0x01	/* Start processing a job chain. Writing this value is ignored */
#define JS_COMMAND_SOFT_STOP   0x02	/* Gently stop processing a job chain */
#define JS_COMMAND_HARD_STOP   0x03	/* Rudely stop processing a job chain */
#define JS_COMMAND_SOFT_STOP_0 0x04	/* Execute SOFT_STOP if JOB_CHAIN_FLAG is 0 */
#define JS_COMMAND_HARD_STOP_0 0x05	/* Execute HARD_STOP if JOB_CHAIN_FLAG is 0 */
#define JS_COMMAND_SOFT_STOP_1 0x06	/* Execute SOFT_STOP if JOB_CHAIN_FLAG is 1 */
#define JS_COMMAND_HARD_STOP_1 0x07	/* Execute HARD_STOP if JOB_CHAIN_FLAG is 1 */

#define JS_COMMAND_MASK        0x07    /* Mask of bits currently in use by the HW */

/* Possible values of JS_CONFIG and JS_CONFIG_NEXT registers */
#define JS_CONFIG_START_FLUSH_NO_ACTION        (0u << 0)
#define JS_CONFIG_START_FLUSH_CLEAN            (1u << 8)
#define JS_CONFIG_START_FLUSH_CLEAN_INVALIDATE (3u << 8)
#define JS_CONFIG_START_MMU                    (1u << 10)
#define JS_CONFIG_JOB_CHAIN_FLAG               (1u << 11)
#define JS_CONFIG_END_FLUSH_NO_ACTION          JS_CONFIG_START_FLUSH_NO_ACTION
#define JS_CONFIG_END_FLUSH_CLEAN              (1u << 12)
#define JS_CONFIG_END_FLUSH_CLEAN_INVALIDATE   (3u << 12)
#define JS_CONFIG_ENABLE_FLUSH_REDUCTION       (1u << 14)
#define JS_CONFIG_DISABLE_DESCRIPTOR_WR_BK     (1u << 15)
#define JS_CONFIG_THREAD_PRI(n)                ((n) << 16)

/* JS_XAFFINITY register values */
#define JS_XAFFINITY_XAFFINITY_ENABLE (1u << 0)
#define JS_XAFFINITY_TILER_ENABLE     (1u << 8)
#define JS_XAFFINITY_CACHE_ENABLE     (1u << 16)

/* JS_STATUS register values */

/* NOTE: Please keep this values in sync with enum base_jd_event_code in mali_base_kernel.h.
 * The values are separated to avoid dependency of userspace and kernel code.
 */

/* Group of values representing the job status instead of a particular fault */
#define JS_STATUS_NO_EXCEPTION_BASE   0x00
#define JS_STATUS_INTERRUPTED         (JS_STATUS_NO_EXCEPTION_BASE + 0x02)	/* 0x02 means INTERRUPTED */
#define JS_STATUS_STOPPED             (JS_STATUS_NO_EXCEPTION_BASE + 0x03)	/* 0x03 means STOPPED */
#define JS_STATUS_TERMINATED          (JS_STATUS_NO_EXCEPTION_BASE + 0x04)	/* 0x04 means TERMINATED */

/* General fault values */
#define JS_STATUS_FAULT_BASE          0x40
#define JS_STATUS_CONFIG_FAULT        (JS_STATUS_FAULT_BASE)	/* 0x40 means CONFIG FAULT */
#define JS_STATUS_POWER_FAULT         (JS_STATUS_FAULT_BASE + 0x01)	/* 0x41 means POWER FAULT */
#define JS_STATUS_READ_FAULT          (JS_STATUS_FAULT_BASE + 0x02)	/* 0x42 means READ FAULT */
#define JS_STATUS_WRITE_FAULT         (JS_STATUS_FAULT_BASE + 0x03)	/* 0x43 means WRITE FAULT */
#define JS_STATUS_AFFINITY_FAULT      (JS_STATUS_FAULT_BASE + 0x04)	/* 0x44 means AFFINITY FAULT */
#define JS_STATUS_BUS_FAULT           (JS_STATUS_FAULT_BASE + 0x08)	/* 0x48 means BUS FAULT */

/* Instruction or data faults */
#define JS_STATUS_INSTRUCTION_FAULT_BASE  0x50
#define JS_STATUS_INSTR_INVALID_PC        (JS_STATUS_INSTRUCTION_FAULT_BASE)	/* 0x50 means INSTR INVALID PC */
#define JS_STATUS_INSTR_INVALID_ENC       (JS_STATUS_INSTRUCTION_FAULT_BASE + 0x01)	/* 0x51 means INSTR INVALID ENC */
#define JS_STATUS_INSTR_TYPE_MISMATCH     (JS_STATUS_INSTRUCTION_FAULT_BASE + 0x02)	/* 0x52 means INSTR TYPE MISMATCH */
#define JS_STATUS_INSTR_OPERAND_FAULT     (JS_STATUS_INSTRUCTION_FAULT_BASE + 0x03)	/* 0x53 means INSTR OPERAND FAULT */
#define JS_STATUS_INSTR_TLS_FAULT         (JS_STATUS_INSTRUCTION_FAULT_BASE + 0x04)	/* 0x54 means INSTR TLS FAULT */
#define JS_STATUS_INSTR_BARRIER_FAULT     (JS_STATUS_INSTRUCTION_FAULT_BASE + 0x05)	/* 0x55 means INSTR BARRIER FAULT */
#define JS_STATUS_INSTR_ALIGN_FAULT       (JS_STATUS_INSTRUCTION_FAULT_BASE + 0x06)	/* 0x56 means INSTR ALIGN FAULT */
/* NOTE: No fault with 0x57 code defined in spec. */
#define JS_STATUS_DATA_INVALID_FAULT      (JS_STATUS_INSTRUCTION_FAULT_BASE + 0x08)	/* 0x58 means DATA INVALID FAULT */
#define JS_STATUS_TILE_RANGE_FAULT        (JS_STATUS_INSTRUCTION_FAULT_BASE + 0x09)	/* 0x59 means TILE RANGE FAULT */
#define JS_STATUS_ADDRESS_RANGE_FAULT     (JS_STATUS_INSTRUCTION_FAULT_BASE + 0x0A)	/* 0x5A means ADDRESS RANGE FAULT */

/* Other faults */
#define JS_STATUS_MEMORY_FAULT_BASE   0x60
#define JS_STATUS_OUT_OF_MEMORY       (JS_STATUS_MEMORY_FAULT_BASE)	/* 0x60 means OUT OF MEMORY */
#define JS_STATUS_UNKNOWN             0x7F	/* 0x7F means UNKNOWN */

/* JS<n>_FEATURES register */
#define JS_FEATURE_NULL_JOB              (1u << 1)
#define JS_FEATURE_SET_VALUE_JOB         (1u << 2)
#define JS_FEATURE_CACHE_FLUSH_JOB       (1u << 3)
#define JS_FEATURE_COMPUTE_JOB           (1u << 4)
#define JS_FEATURE_VERTEX_JOB            (1u << 5)
#define JS_FEATURE_GEOMETRY_JOB          (1u << 6)
#define JS_FEATURE_TILER_JOB             (1u << 7)
#define JS_FEATURE_FUSED_JOB             (1u << 8)
#define JS_FEATURE_FRAGMENT_JOB          (1u << 9)

/* JM_CONFIG register */
#define JM_TIMESTAMP_OVERRIDE  (1ul << 0)
#define JM_CLOCK_GATE_OVERRIDE (1ul << 1)
#define JM_JOB_THROTTLE_ENABLE (1ul << 2)
#define JM_JOB_THROTTLE_LIMIT_SHIFT (3)
#define JM_MAX_JOB_THROTTLE_LIMIT (0x3F)
#define JM_FORCE_COHERENCY_FEATURES_SHIFT (2)

/* GPU_COMMAND values */
#define GPU_COMMAND_NOP                0x00 /* No operation, nothing happens */
#define GPU_COMMAND_SOFT_RESET         0x01 /* Stop all external bus interfaces, and then reset the entire GPU. */
#define GPU_COMMAND_HARD_RESET         0x02 /* Immediately reset the entire GPU. */
#define GPU_COMMAND_PRFCNT_CLEAR       0x03 /* Clear all performance counters, setting them all to zero. */
#define GPU_COMMAND_PRFCNT_SAMPLE      0x04 /* Sample all performance counters, writing them out to memory */
#define GPU_COMMAND_CYCLE_COUNT_START  0x05 /* Starts the cycle counter, and system timestamp propagation */
#define GPU_COMMAND_CYCLE_COUNT_STOP   0x06 /* Stops the cycle counter, and system timestamp propagation */
#define GPU_COMMAND_CLEAN_CACHES       0x07 /* Clean all caches */
#define GPU_COMMAND_CLEAN_INV_CACHES   0x08 /* Clean and invalidate all caches */
#define GPU_COMMAND_SET_PROTECTED_MODE 0x09 /* Places the GPU in protected mode */

/* IRQ flags */
#define GPU_FAULT               (1 << 0)    /* A GPU Fault has occurred */
#define MULTIPLE_GPU_FAULTS     (1 << 7)    /* More than one GPU Fault occurred.  */
#define RESET_COMPLETED         (1 << 8)    /* Set when a reset has completed.  */
#define POWER_CHANGED_SINGLE    (1 << 9)    /* Set when a single core has finished powering up or down. */
#define POWER_CHANGED_ALL       (1 << 10)   /* Set when all cores have finished powering up or down. */
#define PRFCNT_SAMPLE_COMPLETED (1 << 16)   /* Set when a performance count sample has completed. */
#define CLEAN_CACHES_COMPLETED  (1 << 17)   /* Set when a cache clean operation has completed. */

/*
 * In Debug build,
 * GPU_IRQ_REG_COMMON | POWER_CHANGED_SINGLE is used to clear and enable interupts sources of GPU_IRQ
 * by writing it onto GPU_IRQ_CLEAR/MASK registers.
 *
 * In Release build,
 * GPU_IRQ_REG_COMMON is used.
 *
 * Note:
 * CLEAN_CACHES_COMPLETED - Used separately for cache operation.
 */
#define GPU_IRQ_REG_COMMON (GPU_FAULT | MULTIPLE_GPU_FAULTS | RESET_COMPLETED \
		| POWER_CHANGED_ALL | PRFCNT_SAMPLE_COMPLETED)

#endif /* _KBASE_GPU_REGMAP_JM_H_ */
