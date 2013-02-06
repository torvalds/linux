/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Hypervisor Support
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

/**
 *  @file
 *
 *  @brief Constant definitions for ARM CP15 coprocessor registers.
 *
 *  Derived from tweety hypervisor/src/armv6/trango_macros.inc file
 */

#ifndef _COPROC_DEFS_H_
#define _COPROC_DEFS_H_

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

/**
 * @name CP10 registers.
 *
 * MCR/MRC format: @code #define <name> <opcode_1>, <CRn>, <CRm>, <opcode_2> @endcode
 * @{
 */
#define VFP_FPSID                           7,  c0,  c0, 0
#define VFP_MVFR0                           7,  c7,  c0, 0
#define VFP_MVFR1                           7,  c6,  c0, 0
#define VFP_FPEXC                           7,  c8,  c0, 0
#define VFP_FPSCR                           7,  c1,  c0, 0
#define VFP_FPINST                          7,  c9,  c0, 0
#define VFP_FPINST2                         7, c10,  c0, 0
/*@}*/


/**
 * @name CP15 registers.
 *
 * MCR/MRC format: @code #define <name> <opcode_1>, <CRn>, <CRm>, <opcode_2> @endcode
 * MCRR format: @code #define <name> <opcode>, <CRm>@endcode
 * @{
 */
#define ID_CODE                             0,  c0,  c0, 0
#define CACHE_TYPE                          0,  c0,  c0, 1
#define MPIDR                               0,  c0,  c0, 5
#define CACHE_SIZE_ID                       1,  c0,  c0, 0
#define CACHE_LEVEL_ID                      1,  c0,  c0, 1
#define CACHE_SIZE_SELECTION                2,  c0,  c0, 0
#define MEM_MODEL_FEATURE_0                 0,  c0,  c1, 4
#define CONTROL_REGISTER                    0,  c1,  c0, 0
#define TTBASE0_POINTER                     0,  c2,  c0, 0
#define TTBASE1_POINTER                     0,  c2,  c0, 1
#define TTCONTROL                           0,  c2,  c0, 2
#define DOMAIN_CONTROL                      0,  c3,  c0, 0
#define DATA_FAULT_STATUS                   0,  c5,  c0, 0
#define INST_FAULT_STATUS                   0,  c5,  c0, 1
#define AUX_DATA_FAULT_STATUS               0,  c5,  c1, 0
#define AUX_INST_FAULT_STATUS               0,  c5,  c1, 1
#define DATA_FAULT_ADDRESS                  0,  c6,  c0, 0
#define INST_FAULT_ADDRESS                  0,  c6,  c0, 2
#define WAIT_FOR_INTERRUPT                  0,  c7,  c0, 4
#define PHYSICAL_ADDRESS                    0,  c7,  c4, 0
#define ICACHE_INVALIDATE_POU               0,  c7,  c5, 0
#define ICACHE_INVALIDATE_MVA_POU           0,  c7,  c5, 1
#define ICACHE_INVALIDATE_INDEX             0,  c7,  c5, 2
#define BTAC_INVALIDATE                     0,  c7,  c5, 6
#define BTAC_INVALIDATE_MVA                 0,  c7,  c5, 7
#define DCACHE_INVALIDATE                   0,  c7,  c6, 0
#define DCACHE_INVALIDATE_MVA_POC           0,  c7,  c6, 1
#define DCACHE_INVALIDATE_INDEX             0,  c7,  c6, 2
#define UCACHE_INVALIDATE                   0,  c7,  c7, 0
#define V2P_CURRENT_PRIV_READ               0,  c7,  c8, 0
#define V2P_CURRENT_PRIV_WRITE              0,  c7,  c8, 1
#define V2P_CURRENT_USER_READ               0,  c7,  c8, 2
#define V2P_CURRENT_USER_WRITE              0,  c7,  c8, 3
#define V2P_OTHER_PRIV_READ                 0,  c7,  c8, 4
#define V2P_OTHER_PRIV_WRITE                0,  c7,  c8, 5
#define V2P_OTHER_USER_READ                 0,  c7,  c8, 6
#define V2P_OTHER_USER_WRITE                0,  c7,  c8, 7
#define DCACHE_CLEAN                        0,  c7, c10, 0
#define DCACHE_CLEAN_MVA_POC                0,  c7, c10, 1
#define DCACHE_CLEAN_INDEX                  0,  c7, c10, 2
#define DCACHE_CLEAN_MVA_POU                0,  c7, c11, 1
#define DCACHE_CLEAN_INVALIDATE             0,  c7, c14, 0
#define DCACHE_CLEAN_INVALIDATE_MVA_POC     0,  c7, c14, 1
#define DCACHE_CLEAN_INVALIDATE_INDEX       0,  c7, c14, 2
#define ITLB_INVALIDATE_ALL                 0,  c8,  c5, 0
#define ITLB_INVALIDATE_SINGLE              0,  c8,  c5, 1
#define ITLB_INVALIDATE_ASID                0,  c8,  c5, 2
#define DTLB_INVALIDATE_ALL                 0,  c8,  c6, 0
#define DTLB_INVALIDATE_SINGLE              0,  c8,  c6, 1
#define DTLB_INVALIDATE_ASID                0,  c8,  c6, 2
#define UTLB_INVALIDATE_ALL                 0,  c8,  c7, 0
#define UTLB_INVALIDATE_SINGLE              0,  c8,  c7, 1
#define UTLB_INVALIDATE_ASID                0,  c8,  c7, 2
#define TLB_LOCKDOWN                        0, c10,  c0, 0
#define PRIMARY_REGION_REMAP                0, c10,  c2, 0
#define MAIR0                               PRIMARY_REGION_REMAP
#define NORMAL_MEMORY_REMAP                 0, c10,  c2, 1
#define MAIR1                               NORMAL_MEMORY_REMAP
#define VECTOR_BASE                         0, c12,  c0, 0
#define INTERRUPT_STATUS                    0, c12,  c1, 0
#define CONTEXT_ID                          0, c13,  c0, 1
#define TID_USER_RW                         0, c13,  c0, 2
#define TID_USER_RO                         0, c13,  c0, 3
#define TID_PRIV_RW                         0, c13,  c0, 4
#define CLEAR_FAULT_IN_EFSR                 7, c15,  c0, 1
#define VBAR                                0, c12,  c0, 0

/*
 * ARMv7 performance counters' registers (MVP related)
 * - ARM Architecture Reference Manual v7-A and v7-R: DDI0406B
 * - Cortex-A8 TRM, rev.r1p1: DDI0344B
 */
#define PERF_MON_CONTROL_REGISTER           0,  c9, c12, 0
#define CYCLE_COUNT                         0,  c9, c13, 0
#define PERF_MON_COUNT_SET                  0,  c9, c12, 1
#define PERF_MON_COUNT_CLR                  0,  c9, c12, 2
#define PERF_MON_FLAG_RDCLR                 0,  c9, c12, 3
#define PERF_MON_EVENT_SELECT               0,  c9, c12, 5
#define PERF_MON_EVENT_TYPE                 0,  c9, c13, 1
#define PERF_MON_EVENT_COUNT                0,  c9, c13, 2
#define PERF_MON_INTEN_SET                  0,  c9, c14, 1
#define PERF_MON_INTEN_CLR                  0,  c9, c14, 2

#define COPROC_ACCESS_CONTROL               0,  c1,  c0, 2
#define NON_SECURE_ACCESS_CONTROL           0,  c1,  c1, 2

#define HYP_CFG                             4,  c1,  c1, 0
#define HYP_DEBUG_CONTROL                   4,  c1,  c1, 1
#define HYP_COPROC_TRAP                     4,  c1,  c1, 2
#define HYP_SYS_TRAP                        4,  c1,  c1, 3
#define VIRT_TCR                            4,  c2,  c1, 2
#define HYP_SYNDROME                        4,  c5,  c2, 0
#define HYP_DATA_FAULT_ADDRESS              4,  c6,  c0, 0
#define HYP_INST_FAULT_ADDRESS              4,  c6,  c0, 2
#define HYP_IPA_FAULT_ADDRESS               4,  c6,  c0, 4
#define UTLB_INVALIDATE_ALL_HYP             4,  c8,  c7, 0
#define UTLB_INVALIDATE_SINGLE_HYP          4,  c8,  c7, 1
#define UTLB_INVALIDATE_ALL_NS_NON_HYP      4,  c8,  c7, 4

#define EXT_TTBR0                           0,  c2
#define EXT_TTBR1                           1,  c2
#define HYP_TTBR                            4,  c2
#define VIRT_TTBR                           6,  c2
#define EXT_PHYSICAL_ADDRESS                0,  c7
/*@}*/

/**
 * @name CP15 configuration control register bits.
 * @{
 */
#define ARM_CP15_CNTL_M         (1 << 0)
#define ARM_CP15_CNTL_A         (1 << 1)
#define ARM_CP15_CNTL_C         (1 << 2)
#define ARM_CP15_CNTL_B         (1 << 7)
#define ARM_CP15_CNTL_Z         (1 << 11)
#define ARM_CP15_CNTL_I         (1 << 12)
#define ARM_CP15_CNTL_V         (1 << 13)
#define ARM_CP15_CNTL_U         (1 << 22)
#define ARM_CP15_CNTL_VE        (1 << 24)
#define ARM_CP15_CNTL_EE        (1 << 25)
#define ARM_CP15_CNTL_TRE       (1 << 28)
#define ARM_CP15_CNTL_AFE       (1 << 29)
#define ARM_CP15_CNTL_TE        (1 << 30)

/*@}*/

/**
 * @brief Initial System Control Register (SCTLR) value.
 *
 * Magic described on B3-97 ARM DDI 0406B, it's the power-on
 * value, e.g. caches/MMU/alignment checking/TEX remap etc. disabled.
 */
#define ARM_CP15_CNTL_INIT 0x00c50078

/**
 * @name System control coprocessor primary registers.
 * Each primary register is backed by potentially multiple
 * physical registers in the vCPU CP15 register file.
 * @{
 */
#define ARM_CP15_CRN_ID         0  ///< Processor ID, cache, TCM and TLB type
#define ARM_CP15_CRN_CNTL       1  ///< System configuration bits
#define ARM_CP15_CRN_PT         2  ///< Page table control
#define ARM_CP15_CRN_DACR       3  ///< Domain access control
#define ARM_CP15_CRN_F_STATUS   5  ///< Fault status
#define ARM_CP15_CRN_F_ADDR     6  ///< Fault address
#define ARM_CP15_CRN_CACHE      7  ///< Cache/write buffer control
#define ARM_CP15_CRN_TLB        8  ///< TLB control
#define ARM_CP15_CRN_REMAP      10 ///< Memory Remap registers
#define ARM_CP15_CRN_SER        12 ///< Security Extension registers
#define ARM_CP15_CRN_PID        13 ///< Process ID
#define ARM_CP15_CRN_TIMER      14 ///< Architecture timers

#define ARM_CP15_CRM_INVALIDATE_D_CACHE_RANGE            6
#define ARM_CP15_CRM_CLEAN_AND_INVALIDATE_D_CACHE_RANGE  14
/*@}*/

/**
 * @name ARMv7 performance counter control/status register bits (MVP related)
 * INTEN: counters overflow interrupt enable
 * CNTEN: counters enable
 * @{
 */
#define ARMV7_PMNC_E        (1 << 0)
#define ARMV7_PMNC_INTEN_P0 (1 << 0)
#define ARMV7_PMNC_INTEN_P1 (1 << 1)
#define ARMV7_PMNC_INTEN_P2 (1 << 2)
#define ARMV7_PMNC_INTEN_P3 (1 << 3)
#define ARMV7_PMNC_INTEN_C (1 << 31)
#define ARMV7_PMNC_INTEN_MASK 0x8000000f
#define ARMV7_PMNC_CNTEN_P0 (1 << 0)
#define ARMV7_PMNC_CNTEN_P1 (1 << 1)
#define ARMV7_PMNC_CNTEN_P2 (1 << 2)
#define ARMV7_PMNC_CNTEN_P3 (1 << 3)
#define ARMV7_PMNC_CNTEN_C (1 << 31)
#define ARMV7_PMNC_FLAG_P0 (1 << 0)
#define ARMV7_PMNC_FLAG_P1 (1 << 1)
#define ARMV7_PMNC_FLAG_P2 (1 << 2)
#define ARMV7_PMNC_FLAG_P3 (1 << 3)
#define ARMV7_PMNC_FLAG_C (1 << 31)
/*@}*/

/**
 * @name TTBR masks.
 * See B4.9.2 ARM DDI 0100I and B3.12.24 ARM DDI 0406A.
 * @{
 */
#define ARM_CP15_TTBASE_MASK    MVP_MASK(14, 18)
#define ARM_CP15_TTBASE_SPLIT_MASK(ttbcrn) MVP_MASK(14-ttbcrn, 18+ttbcrn)
#define ARM_CP15_TTATTRIB_MASK  MVP_MASK(0, 6)
/*@}*/

/**
 * @name ARM fault status register encoding/decoding.
 * See B4.6 and B4.9.6 in ARM DDI 0100I.
 * @{
 */
#define ARM_CP15_FSR_STATUS_POS         0
#define ARM_CP15_FSR_STATUS_POS2        10
#define ARM_CP15_FSR_DOMAIN_POS         4
#define ARM_CP15_FSR_WR_POS             11

#define ARM_CP15_FSR_STATUS_LEN         4
#define ARM_CP15_FSR_DOMAIN_LEN         4

#define ARM_CP15_FSR_STATUS_DEBUG_EVENT           0x2
#define ARM_CP15_FSR_STATUS_ALIGNMENT             0x1
#define ARM_CP15_FSR_STATUS_ICACHE_MAINT          0x4
#define ARM_CP15_FSR_STATUS_TRANSLATION_SECT      0x5
#define ARM_CP15_FSR_STATUS_TRANSLATION_PAGE      0x7
#define ARM_CP15_FSR_STATUS_DOMAIN_SECT           0x9
#define ARM_CP15_FSR_STATUS_DOMAIN_PAGE           0xb
#define ARM_CP15_FSR_STATUS_PERMISSION_SECT       0xd
#define ARM_CP15_FSR_STATUS_PERMISSION_PAGE       0xf
#define ARM_CP15_FSR_STATUS_ACCESS_FLAG_SECT      0x3
#define ARM_CP15_FSR_STATUS_ACCESS_FLAG_PAGE      0x6
#define ARM_CP15_FSR_STATUS_SYNC_EXT_ABORT        0x8
#define ARM_CP15_FSR_STATUS_ASYNC_EXT_ABORT      0x16
/*@}*/

/**
 * @brief Generate ARM fault status register value.
 *
 * @param fs status from Table B4-1. Only implemented for fs <= 0xf.
 * @param domain domain accessed when abort occurred.
 * @param write write access caused abort.
 */
#define ARM_CP15_FSR(fs,domain,write) \
   (((fs) << ARM_CP15_FSR_STATUS_POS) | \
    ((domain) << ARM_CP15_FSR_DOMAIN_POS) |  \
    ((write) ? (1 << ARM_CP15_FSR_WR_POS) : 0))

#define ARM_CP15_FSR_STATUS(r) \
   (MVP_EXTRACT_FIELD((r), ARM_CP15_FSR_STATUS_POS, ARM_CP15_FSR_STATUS_LEN) | \
    (MVP_BIT((r), ARM_CP15_FSR_STATUS_POS2) << ARM_CP15_FSR_STATUS_LEN))
#define ARM_CP15_FSR_DOMAIN(r) \
   MVP_EXTRACT_FIELD((r), ARM_CP15_FSR_DOMAIN_POS, ARM_CP15_FSR_DOMAIN_LEN)
#define ARM_CP15_FSR_WR(r) \
   MVP_BIT((r), ARM_CP15_FSR_WR_POS)
/*@}*/

/*
 * This should mask out the major and minor revision numbers.
 * As per http://infocenter.arm.com/help/topic/com.arm.doc.ddi0211k/I65012.html
 */
#define ARM_CP15_MAIN_ID_NOREVISION_MASK 0xFF0FFFF0

// 2-8 ARM DDI 0151C
#define ARM_CP15_MAIN_ID_920_T   0x41129200
// 3-18 ARM DDI 0211H
#define ARM_CP15_MAIN_ID_1136J_S 0x4107B362

/* Coprocessor Access Control Register */
#define CPACR_ASEDIS              (1 << 31)
#define CPACR_D32DIS              (1 << 30)
#define CPACR_CP10_MASK           (0x3 << (10*2))
#define CPACR_CP10_CP11_MASK      ( (0x3 << (10*2)) | (0x3 << (11*2)) )
#define CPACR_CP10_CP11_PRIV_ONLY ( (0x1 << (10*2)) | (0x1 << (11*2)) )
                                     /* 2-bit access permission per Co-Proc */

/**
 * @name ARM VFP/Adv. SIMD Extension System Registers
 * @{
 */
#define ARM_VFP_SYSTEM_REG_FPSID    0x0
#define ARM_VFP_SYSTEM_REG_FPSCR    0x1
#define ARM_VFP_SYSTEM_REG_MVFR1    0x6
#define ARM_VFP_SYSTEM_REG_MVFR0    0x7
#define ARM_VFP_SYSTEM_REG_FPEXC    0x8
#define ARM_VFP_SYSTEM_REG_FPINST   0x9
#define ARM_VFP_SYSTEM_REG_FPINST2  0xa

#define ARM_VFP_SYSTEM_REG_FPEXC_EX   (1 << 31)
#define ARM_VFP_SYSTEM_REG_FPEXC_EN   (1 << 30)
#define ARM_VFP_SYSTEM_REG_FPEXC_FP2V (1 << 28)

#define ARM_VFP_SYSTEM_REG_MVFR0_A_SIMD_BIT  (0)
#define ARM_VFP_SYSTEM_REG_MVFR0_A_SIMD_MASK (0xf << ARM_VFP_SYSTEM_REG_MVFR0_A_SIMD_BIT)
/*@}*/

/**
 * @name ARM Multi Processor ID Register (MPIDR) decoding
 * @{
 */
#define ARM_CP15_MPIDR_MP          (0x1 << 31)
#define ARM_CP15_MPIDR_U           (0x1 << 30)
/*@}*/

#endif /// ifndef _COPROC_DEFS_H_
