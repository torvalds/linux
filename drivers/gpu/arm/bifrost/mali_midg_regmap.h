/*
 *
 * (C) COPYRIGHT 2010-2019 ARM Limited. All rights reserved.
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

#ifndef _MIDG_REGMAP_H_
#define _MIDG_REGMAP_H_

#include "mali_midg_coherency.h"
#include "mali_kbase_gpu_id.h"
#include "mali_midg_regmap_jm.h"

/* Begin Register Offsets */
/* GPU control registers */

#define GPU_CONTROL_BASE        0x0000
#define GPU_CONTROL_REG(r)      (GPU_CONTROL_BASE + (r))
#define GPU_ID                  0x000   /* (RO) GPU and revision identifier */
#define L2_FEATURES             0x004   /* (RO) Level 2 cache features */
#define TILER_FEATURES          0x00C   /* (RO) Tiler Features */
#define MEM_FEATURES            0x010   /* (RO) Memory system features */
#define MMU_FEATURES            0x014   /* (RO) MMU features */
#define AS_PRESENT              0x018   /* (RO) Address space slots present */
#define GPU_IRQ_RAWSTAT         0x020   /* (RW) */
#define GPU_IRQ_CLEAR           0x024   /* (WO) */
#define GPU_IRQ_MASK            0x028   /* (RW) */
#define GPU_IRQ_STATUS          0x02C   /* (RO) */

#define GPU_COMMAND             0x030   /* (WO) */
#define GPU_STATUS              0x034   /* (RO) */

#define GPU_DBGEN               (1 << 8)    /* DBGEN wire status */

#define GPU_FAULTSTATUS         0x03C   /* (RO) GPU exception type and fault status */
#define GPU_FAULTADDRESS_LO     0x040   /* (RO) GPU exception fault address, low word */
#define GPU_FAULTADDRESS_HI     0x044   /* (RO) GPU exception fault address, high word */

#define L2_CONFIG               0x048   /* (RW) Level 2 cache configuration */

#define PWR_KEY                 0x050   /* (WO) Power manager key register */
#define PWR_OVERRIDE0           0x054   /* (RW) Power manager override settings */
#define PWR_OVERRIDE1           0x058   /* (RW) Power manager override settings */

#define PRFCNT_BASE_LO          0x060   /* (RW) Performance counter memory region base address, low word */
#define PRFCNT_BASE_HI          0x064   /* (RW) Performance counter memory region base address, high word */
#define PRFCNT_CONFIG           0x068   /* (RW) Performance counter configuration */
#define PRFCNT_JM_EN            0x06C   /* (RW) Performance counter enable flags for Job Manager */
#define PRFCNT_SHADER_EN        0x070   /* (RW) Performance counter enable flags for shader cores */
#define PRFCNT_TILER_EN         0x074   /* (RW) Performance counter enable flags for tiler */
#define PRFCNT_MMU_L2_EN        0x07C   /* (RW) Performance counter enable flags for MMU/L2 cache */

#define CYCLE_COUNT_LO          0x090   /* (RO) Cycle counter, low word */
#define CYCLE_COUNT_HI          0x094   /* (RO) Cycle counter, high word */
#define TIMESTAMP_LO            0x098   /* (RO) Global time stamp counter, low word */
#define TIMESTAMP_HI            0x09C   /* (RO) Global time stamp counter, high word */

#define THREAD_MAX_THREADS      0x0A0   /* (RO) Maximum number of threads per core */
#define THREAD_MAX_WORKGROUP_SIZE 0x0A4 /* (RO) Maximum workgroup size */
#define THREAD_MAX_BARRIER_SIZE 0x0A8   /* (RO) Maximum threads waiting at a barrier */
#define THREAD_FEATURES         0x0AC   /* (RO) Thread features */
#define THREAD_TLS_ALLOC        0x310   /* (RO) Number of threads per core that TLS must be allocated for */

#define TEXTURE_FEATURES_0      0x0B0   /* (RO) Support flags for indexed texture formats 0..31 */
#define TEXTURE_FEATURES_1      0x0B4   /* (RO) Support flags for indexed texture formats 32..63 */
#define TEXTURE_FEATURES_2      0x0B8   /* (RO) Support flags for indexed texture formats 64..95 */
#define TEXTURE_FEATURES_3      0x0BC   /* (RO) Support flags for texture order */

#define TEXTURE_FEATURES_REG(n) GPU_CONTROL_REG(TEXTURE_FEATURES_0 + ((n) << 2))

#define SHADER_PRESENT_LO       0x100   /* (RO) Shader core present bitmap, low word */
#define SHADER_PRESENT_HI       0x104   /* (RO) Shader core present bitmap, high word */

#define TILER_PRESENT_LO        0x110   /* (RO) Tiler core present bitmap, low word */
#define TILER_PRESENT_HI        0x114   /* (RO) Tiler core present bitmap, high word */

#define L2_PRESENT_LO           0x120   /* (RO) Level 2 cache present bitmap, low word */
#define L2_PRESENT_HI           0x124   /* (RO) Level 2 cache present bitmap, high word */

#define STACK_PRESENT_LO        0xE00   /* (RO) Core stack present bitmap, low word */
#define STACK_PRESENT_HI        0xE04   /* (RO) Core stack present bitmap, high word */

#define SHADER_READY_LO         0x140   /* (RO) Shader core ready bitmap, low word */
#define SHADER_READY_HI         0x144   /* (RO) Shader core ready bitmap, high word */

#define TILER_READY_LO          0x150   /* (RO) Tiler core ready bitmap, low word */
#define TILER_READY_HI          0x154   /* (RO) Tiler core ready bitmap, high word */

#define L2_READY_LO             0x160   /* (RO) Level 2 cache ready bitmap, low word */
#define L2_READY_HI             0x164   /* (RO) Level 2 cache ready bitmap, high word */

#define STACK_READY_LO          0xE10   /* (RO) Core stack ready bitmap, low word */
#define STACK_READY_HI          0xE14   /* (RO) Core stack ready bitmap, high word */

#define SHADER_PWRON_LO         0x180   /* (WO) Shader core power on bitmap, low word */
#define SHADER_PWRON_HI         0x184   /* (WO) Shader core power on bitmap, high word */

#define TILER_PWRON_LO          0x190   /* (WO) Tiler core power on bitmap, low word */
#define TILER_PWRON_HI          0x194   /* (WO) Tiler core power on bitmap, high word */

#define L2_PWRON_LO             0x1A0   /* (WO) Level 2 cache power on bitmap, low word */
#define L2_PWRON_HI             0x1A4   /* (WO) Level 2 cache power on bitmap, high word */

#define STACK_PWRON_LO          0xE20   /* (RO) Core stack power on bitmap, low word */
#define STACK_PWRON_HI          0xE24   /* (RO) Core stack power on bitmap, high word */

#define SHADER_PWROFF_LO        0x1C0   /* (WO) Shader core power off bitmap, low word */
#define SHADER_PWROFF_HI        0x1C4   /* (WO) Shader core power off bitmap, high word */

#define TILER_PWROFF_LO         0x1D0   /* (WO) Tiler core power off bitmap, low word */
#define TILER_PWROFF_HI         0x1D4   /* (WO) Tiler core power off bitmap, high word */

#define L2_PWROFF_LO            0x1E0   /* (WO) Level 2 cache power off bitmap, low word */
#define L2_PWROFF_HI            0x1E4   /* (WO) Level 2 cache power off bitmap, high word */

#define STACK_PWROFF_LO         0xE30   /* (RO) Core stack power off bitmap, low word */
#define STACK_PWROFF_HI         0xE34   /* (RO) Core stack power off bitmap, high word */

#define SHADER_PWRTRANS_LO      0x200   /* (RO) Shader core power transition bitmap, low word */
#define SHADER_PWRTRANS_HI      0x204   /* (RO) Shader core power transition bitmap, high word */

#define TILER_PWRTRANS_LO       0x210   /* (RO) Tiler core power transition bitmap, low word */
#define TILER_PWRTRANS_HI       0x214   /* (RO) Tiler core power transition bitmap, high word */

#define L2_PWRTRANS_LO          0x220   /* (RO) Level 2 cache power transition bitmap, low word */
#define L2_PWRTRANS_HI          0x224   /* (RO) Level 2 cache power transition bitmap, high word */

#define STACK_PWRTRANS_LO       0xE40   /* (RO) Core stack power transition bitmap, low word */
#define STACK_PWRTRANS_HI       0xE44   /* (RO) Core stack power transition bitmap, high word */

#define SHADER_PWRACTIVE_LO     0x240   /* (RO) Shader core active bitmap, low word */
#define SHADER_PWRACTIVE_HI     0x244   /* (RO) Shader core active bitmap, high word */

#define TILER_PWRACTIVE_LO      0x250   /* (RO) Tiler core active bitmap, low word */
#define TILER_PWRACTIVE_HI      0x254   /* (RO) Tiler core active bitmap, high word */

#define L2_PWRACTIVE_LO         0x260   /* (RO) Level 2 cache active bitmap, low word */
#define L2_PWRACTIVE_HI         0x264   /* (RO) Level 2 cache active bitmap, high word */

#define COHERENCY_FEATURES      0x300   /* (RO) Coherency features present */
#define COHERENCY_ENABLE        0x304   /* (RW) Coherency enable */

#define SHADER_CONFIG           0xF04   /* (RW) Shader core configuration (implementation-specific) */
#define TILER_CONFIG            0xF08   /* (RW) Tiler core configuration (implementation-specific) */
#define L2_MMU_CONFIG           0xF0C   /* (RW) L2 cache and MMU configuration (implementation-specific) */

/* Job control registers */

#define JOB_CONTROL_BASE        0x1000

#define JOB_CONTROL_REG(r)      (JOB_CONTROL_BASE + (r))

#define JOB_IRQ_RAWSTAT         0x000   /* Raw interrupt status register */
#define JOB_IRQ_CLEAR           0x004   /* Interrupt clear register */
#define JOB_IRQ_MASK            0x008   /* Interrupt mask register */
#define JOB_IRQ_STATUS          0x00C   /* Interrupt status register */

/* MMU control registers */

#define MEMORY_MANAGEMENT_BASE  0x2000
#define MMU_REG(r)              (MEMORY_MANAGEMENT_BASE + (r))

#define MMU_IRQ_RAWSTAT         0x000   /* (RW) Raw interrupt status register */
#define MMU_IRQ_CLEAR           0x004   /* (WO) Interrupt clear register */
#define MMU_IRQ_MASK            0x008   /* (RW) Interrupt mask register */
#define MMU_IRQ_STATUS          0x00C   /* (RO) Interrupt status register */

#define MMU_AS0                 0x400   /* Configuration registers for address space 0 */
#define MMU_AS1                 0x440   /* Configuration registers for address space 1 */
#define MMU_AS2                 0x480   /* Configuration registers for address space 2 */
#define MMU_AS3                 0x4C0   /* Configuration registers for address space 3 */
#define MMU_AS4                 0x500   /* Configuration registers for address space 4 */
#define MMU_AS5                 0x540   /* Configuration registers for address space 5 */
#define MMU_AS6                 0x580   /* Configuration registers for address space 6 */
#define MMU_AS7                 0x5C0   /* Configuration registers for address space 7 */
#define MMU_AS8                 0x600   /* Configuration registers for address space 8 */
#define MMU_AS9                 0x640   /* Configuration registers for address space 9 */
#define MMU_AS10                0x680   /* Configuration registers for address space 10 */
#define MMU_AS11                0x6C0   /* Configuration registers for address space 11 */
#define MMU_AS12                0x700   /* Configuration registers for address space 12 */
#define MMU_AS13                0x740   /* Configuration registers for address space 13 */
#define MMU_AS14                0x780   /* Configuration registers for address space 14 */
#define MMU_AS15                0x7C0   /* Configuration registers for address space 15 */

/* MMU address space control registers */

#define MMU_AS_REG(n, r)        (MMU_REG(MMU_AS0 + ((n) << 6)) + (r))

#define AS_TRANSTAB_LO         0x00	/* (RW) Translation Table Base Address for address space n, low word */
#define AS_TRANSTAB_HI         0x04	/* (RW) Translation Table Base Address for address space n, high word */
#define AS_MEMATTR_LO          0x08	/* (RW) Memory attributes for address space n, low word. */
#define AS_MEMATTR_HI          0x0C	/* (RW) Memory attributes for address space n, high word. */
#define AS_LOCKADDR_LO         0x10	/* (RW) Lock region address for address space n, low word */
#define AS_LOCKADDR_HI         0x14	/* (RW) Lock region address for address space n, high word */
#define AS_COMMAND             0x18	/* (WO) MMU command register for address space n */
#define AS_FAULTSTATUS         0x1C	/* (RO) MMU fault status register for address space n */
#define AS_FAULTADDRESS_LO     0x20	/* (RO) Fault Address for address space n, low word */
#define AS_FAULTADDRESS_HI     0x24	/* (RO) Fault Address for address space n, high word */
#define AS_STATUS              0x28	/* (RO) Status flags for address space n */

/* (RW) Translation table configuration for address space n, low word */
#define AS_TRANSCFG_LO         0x30
/* (RW) Translation table configuration for address space n, high word */
#define AS_TRANSCFG_HI         0x34
/* (RO) Secondary fault address for address space n, low word */
#define AS_FAULTEXTRA_LO       0x38
/* (RO) Secondary fault address for address space n, high word */
#define AS_FAULTEXTRA_HI       0x3C

/* End Register Offsets */

/* IRQ flags */
#define GPU_FAULT               (1 << 0)    /* A GPU Fault has occurred */
#define MULTIPLE_GPU_FAULTS     (1 << 7)    /* More than one GPU Fault occurred. */
#define RESET_COMPLETED         (1 << 8)    /* Set when a reset has completed. */
#define POWER_CHANGED_SINGLE    (1 << 9)    /* Set when a single core has finished powering up or down. */
#define POWER_CHANGED_ALL       (1 << 10)   /* Set when all cores have finished powering up or down. */

#define PRFCNT_SAMPLE_COMPLETED (1 << 16)   /* Set when a performance count sample has completed. */
#define CLEAN_CACHES_COMPLETED  (1 << 17)   /* Set when a cache clean operation has completed. */

#define GPU_IRQ_REG_ALL (GPU_FAULT | MULTIPLE_GPU_FAULTS | RESET_COMPLETED \
		| POWER_CHANGED_ALL | PRFCNT_SAMPLE_COMPLETED)

/*
 * MMU_IRQ_RAWSTAT register values. Values are valid also for
 * MMU_IRQ_CLEAR, MMU_IRQ_MASK, MMU_IRQ_STATUS registers.
 */

#define MMU_PAGE_FAULT_FLAGS    16

/* Macros returning a bitmask to retrieve page fault or bus error flags from
 * MMU registers */
#define MMU_PAGE_FAULT(n)       (1UL << (n))
#define MMU_BUS_ERROR(n)        (1UL << ((n) + MMU_PAGE_FAULT_FLAGS))

/*
 * Begin LPAE MMU TRANSTAB register values
 */
#define AS_TRANSTAB_LPAE_ADDR_SPACE_MASK   0xfffff000
#define AS_TRANSTAB_LPAE_ADRMODE_UNMAPPED  (0u << 0)
#define AS_TRANSTAB_LPAE_ADRMODE_IDENTITY  (1u << 1)
#define AS_TRANSTAB_LPAE_ADRMODE_TABLE     (3u << 0)
#define AS_TRANSTAB_LPAE_READ_INNER        (1u << 2)
#define AS_TRANSTAB_LPAE_SHARE_OUTER       (1u << 4)

#define AS_TRANSTAB_LPAE_ADRMODE_MASK      0x00000003

/*
 * Begin AARCH64 MMU TRANSTAB register values
 */
#define MMU_HW_OUTA_BITS 40
#define AS_TRANSTAB_BASE_MASK ((1ULL << MMU_HW_OUTA_BITS) - (1ULL << 4))

/*
 * Begin MMU STATUS register values
 */
#define AS_STATUS_AS_ACTIVE 0x01

#define AS_FAULTSTATUS_EXCEPTION_CODE_MASK                      (0x7<<3)
#define AS_FAULTSTATUS_EXCEPTION_CODE_TRANSLATION_FAULT         (0x0<<3)
#define AS_FAULTSTATUS_EXCEPTION_CODE_PERMISSION_FAULT          (0x1<<3)
#define AS_FAULTSTATUS_EXCEPTION_CODE_TRANSTAB_BUS_FAULT        (0x2<<3)
#define AS_FAULTSTATUS_EXCEPTION_CODE_ACCESS_FLAG               (0x3<<3)
#define AS_FAULTSTATUS_EXCEPTION_CODE_ADDRESS_SIZE_FAULT        (0x4<<3)
#define AS_FAULTSTATUS_EXCEPTION_CODE_MEMORY_ATTRIBUTES_FAULT   (0x5<<3)

#define AS_FAULTSTATUS_ACCESS_TYPE_MASK         (0x3<<8)
#define AS_FAULTSTATUS_ACCESS_TYPE_ATOMIC       (0x0<<8)
#define AS_FAULTSTATUS_ACCESS_TYPE_EX           (0x1<<8)
#define AS_FAULTSTATUS_ACCESS_TYPE_READ         (0x2<<8)
#define AS_FAULTSTATUS_ACCESS_TYPE_WRITE        (0x3<<8)

/*
 * Begin MMU TRANSCFG register values
 */
#define AS_TRANSCFG_ADRMODE_LEGACY      0
#define AS_TRANSCFG_ADRMODE_UNMAPPED    1
#define AS_TRANSCFG_ADRMODE_IDENTITY    2
#define AS_TRANSCFG_ADRMODE_AARCH64_4K  6
#define AS_TRANSCFG_ADRMODE_AARCH64_64K 8

#define AS_TRANSCFG_ADRMODE_MASK        0xF

/*
 * Begin TRANSCFG register values
 */
#define AS_TRANSCFG_PTW_MEMATTR_MASK (3ull << 24)
#define AS_TRANSCFG_PTW_MEMATTR_NON_CACHEABLE (1ull << 24)
#define AS_TRANSCFG_PTW_MEMATTR_WRITE_BACK (2ull << 24)

#define AS_TRANSCFG_PTW_SH_MASK ((3ull << 28))
#define AS_TRANSCFG_PTW_SH_OS (2ull << 28)
#define AS_TRANSCFG_PTW_SH_IS (3ull << 28)
#define AS_TRANSCFG_R_ALLOCATE (1ull << 30)

/*
 * Begin Command Values
 */

/* AS_COMMAND register commands */
#define AS_COMMAND_NOP         0x00	/* NOP Operation */
#define AS_COMMAND_UPDATE      0x01	/* Broadcasts the values in AS_TRANSTAB and ASn_MEMATTR to all MMUs */
#define AS_COMMAND_LOCK        0x02	/* Issue a lock region command to all MMUs */
#define AS_COMMAND_UNLOCK      0x03	/* Issue a flush region command to all MMUs */
#define AS_COMMAND_FLUSH       0x04	/* Flush all L2 caches then issue a flush region command to all MMUs
					   (deprecated - only for use with T60x) */
#define AS_COMMAND_FLUSH_PT    0x04	/* Flush all L2 caches then issue a flush region command to all MMUs */
#define AS_COMMAND_FLUSH_MEM   0x05	/* Wait for memory accesses to complete, flush all the L1s cache then
					   flush all L2 caches then issue a flush region command to all MMUs */

/* GPU_STATUS values */
#define GPU_STATUS_PRFCNT_ACTIVE            (1 << 2)    /* Set if the performance counters are active. */
#define GPU_STATUS_PROTECTED_MODE_ACTIVE    (1 << 7)    /* Set if protected mode is active */

/* PRFCNT_CONFIG register values */
#define PRFCNT_CONFIG_MODE_SHIFT        0 /* Counter mode position. */
#define PRFCNT_CONFIG_AS_SHIFT          4 /* Address space bitmap position. */
#define PRFCNT_CONFIG_SETSELECT_SHIFT   8 /* Set select position. */

/* The performance counters are disabled. */
#define PRFCNT_CONFIG_MODE_OFF          0
/* The performance counters are enabled, but are only written out when a
 * PRFCNT_SAMPLE command is issued using the GPU_COMMAND register.
 */
#define PRFCNT_CONFIG_MODE_MANUAL       1
/* The performance counters are enabled, and are written out each time a tile
 * finishes rendering.
 */
#define PRFCNT_CONFIG_MODE_TILE         2

/* AS<n>_MEMATTR values from MMU_MEMATTR_STAGE1: */
/* Use GPU implementation-defined caching policy. */
#define AS_MEMATTR_IMPL_DEF_CACHE_POLICY 0x88ull
/* The attribute set to force all resources to be cached. */
#define AS_MEMATTR_FORCE_TO_CACHE_ALL    0x8Full
/* Inner write-alloc cache setup, no outer caching */
#define AS_MEMATTR_WRITE_ALLOC           0x8Dull

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

/* Use GPU implementation-defined  caching policy. */
#define AS_MEMATTR_LPAE_IMPL_DEF_CACHE_POLICY 0x48ull
/* The attribute set to force all resources to be cached. */
#define AS_MEMATTR_LPAE_FORCE_TO_CACHE_ALL    0x4Full
/* Inner write-alloc cache setup, no outer caching */
#define AS_MEMATTR_LPAE_WRITE_ALLOC           0x4Dull
/* Set to implementation defined, outer caching */
#define AS_MEMATTR_LPAE_OUTER_IMPL_DEF        0x88ull
/* Set to write back memory, outer caching */
#define AS_MEMATTR_LPAE_OUTER_WA              0x8Dull
/* There is no LPAE support for non-cacheable, since the memory type is always
 * write-back.
 * Marking this setting as reserved for LPAE
 */
#define AS_MEMATTR_LPAE_NON_CACHEABLE_RESERVED

/* Symbols for default MEMATTR to use
 * Default is - HW implementation defined caching */
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

/* L2_MMU_CONFIG register */
#define L2_MMU_CONFIG_ALLOW_SNOOP_DISPARITY_SHIFT       (23)
#define L2_MMU_CONFIG_ALLOW_SNOOP_DISPARITY             (0x1 << L2_MMU_CONFIG_ALLOW_SNOOP_DISPARITY_SHIFT)

/* End L2_MMU_CONFIG register */

/* THREAD_* registers */

/* THREAD_FEATURES IMPLEMENTATION_TECHNOLOGY values */
#define IMPLEMENTATION_UNSPECIFIED  0
#define IMPLEMENTATION_SILICON      1
#define IMPLEMENTATION_FPGA         2
#define IMPLEMENTATION_MODEL        3

/* Default values when registers are not supported by the implemented hardware */
#define THREAD_MT_DEFAULT     256
#define THREAD_MWS_DEFAULT    256
#define THREAD_MBS_DEFAULT    256
#define THREAD_MR_DEFAULT     1024
#define THREAD_MTQ_DEFAULT    4
#define THREAD_MTGS_DEFAULT   10

/* End THREAD_* registers */

/* SHADER_CONFIG register */
#define SC_ALT_COUNTERS             (1ul << 3)
#define SC_OVERRIDE_FWD_PIXEL_KILL  (1ul << 4)
#define SC_SDC_DISABLE_OQ_DISCARD   (1ul << 6)
#define SC_LS_ALLOW_ATTR_TYPES      (1ul << 16)
#define SC_LS_PAUSEBUFFER_DISABLE   (1ul << 16)
#define SC_TLS_HASH_ENABLE          (1ul << 17)
#define SC_LS_ATTR_CHECK_DISABLE    (1ul << 18)
#define SC_ENABLE_TEXGRD_FLAGS      (1ul << 25)
#define SC_VAR_ALGORITHM            (1ul << 29)
/* End SHADER_CONFIG register */

/* TILER_CONFIG register */
#define TC_CLOCK_GATE_OVERRIDE      (1ul << 0)
/* End TILER_CONFIG register */

/* L2_CONFIG register */
#define L2_CONFIG_SIZE_SHIFT        16
#define L2_CONFIG_SIZE_MASK         (0xFFul << L2_CONFIG_SIZE_SHIFT)
#define L2_CONFIG_HASH_SHIFT        24
#define L2_CONFIG_HASH_MASK         (0xFFul << L2_CONFIG_HASH_SHIFT)
/* End L2_CONFIG register */


#endif /* _MIDG_REGMAP_H_ */
