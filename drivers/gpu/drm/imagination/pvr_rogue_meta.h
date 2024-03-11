/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_ROGUE_META_H
#define PVR_ROGUE_META_H

/***** The META HW register definitions in the file are updated manually *****/

#include <linux/bits.h>
#include <linux/types.h>

/*
 ******************************************************************************
 * META registers and MACROS
 *****************************************************************************
 */
#define META_CR_CTRLREG_BASE(t) (0x04800000U + (0x1000U * (t)))

#define META_CR_TXPRIVEXT (0x048000E8)
#define META_CR_TXPRIVEXT_MINIM_EN BIT(7)

#define META_CR_SYSC_JTAG_THREAD (0x04830030)
#define META_CR_SYSC_JTAG_THREAD_PRIV_EN (0x00000004)

#define META_CR_PERF_COUNT0 (0x0480FFE0)
#define META_CR_PERF_COUNT1 (0x0480FFE8)
#define META_CR_PERF_COUNT_CTRL_SHIFT (28)
#define META_CR_PERF_COUNT_CTRL_MASK (0xF0000000)
#define META_CR_PERF_COUNT_CTRL_DCACHEHITS (8 << META_CR_PERF_COUNT_CTRL_SHIFT)
#define META_CR_PERF_COUNT_CTRL_ICACHEHITS (9 << META_CR_PERF_COUNT_CTRL_SHIFT)
#define META_CR_PERF_COUNT_CTRL_ICACHEMISS \
	(0xA << META_CR_PERF_COUNT_CTRL_SHIFT)
#define META_CR_PERF_COUNT_CTRL_ICORE (0xD << META_CR_PERF_COUNT_CTRL_SHIFT)
#define META_CR_PERF_COUNT_THR_SHIFT (24)
#define META_CR_PERF_COUNT_THR_MASK (0x0F000000)
#define META_CR_PERF_COUNT_THR_0 (0x1 << META_CR_PERF_COUNT_THR_SHIFT)
#define META_CR_PERF_COUNT_THR_1 (0x2 << META_CR_PERF_COUNT_THR_1)

#define META_CR_TxVECINT_BHALT (0x04820500)
#define META_CR_PERF_ICORE0 (0x0480FFD0)
#define META_CR_PERF_ICORE1 (0x0480FFD8)
#define META_CR_PERF_ICORE_DCACHEMISS (0x8)

#define META_CR_PERF_COUNT(ctrl, thr)                                        \
	((META_CR_PERF_COUNT_CTRL_##ctrl << META_CR_PERF_COUNT_CTRL_SHIFT) | \
	 ((thr) << META_CR_PERF_COUNT_THR_SHIFT))

#define META_CR_TXUXXRXDT_OFFSET (META_CR_CTRLREG_BASE(0U) + 0x0000FFF0U)
#define META_CR_TXUXXRXRQ_OFFSET (META_CR_CTRLREG_BASE(0U) + 0x0000FFF8U)

/* Poll for done. */
#define META_CR_TXUXXRXRQ_DREADY_BIT (0x80000000U)
/* Set for read. */
#define META_CR_TXUXXRXRQ_RDnWR_BIT (0x00010000U)
#define META_CR_TXUXXRXRQ_TX_S (12)
#define META_CR_TXUXXRXRQ_RX_S (4)
#define META_CR_TXUXXRXRQ_UXX_S (0)

/* Internal ctrl regs. */
#define META_CR_TXUIN_ID (0x0)
/* Data unit regs. */
#define META_CR_TXUD0_ID (0x1)
/* Data unit regs. */
#define META_CR_TXUD1_ID (0x2)
/* Address unit regs. */
#define META_CR_TXUA0_ID (0x3)
/* Address unit regs. */
#define META_CR_TXUA1_ID (0x4)
/* PC registers. */
#define META_CR_TXUPC_ID (0x5)

/* Macros to calculate register access values. */
#define META_CR_CORE_REG(thr, reg_num, unit)          \
	(((u32)(thr) << META_CR_TXUXXRXRQ_TX_S) |     \
	 ((u32)(reg_num) << META_CR_TXUXXRXRQ_RX_S) | \
	 ((u32)(unit) << META_CR_TXUXXRXRQ_UXX_S))

#define META_CR_THR0_PC META_CR_CORE_REG(0, 0, META_CR_TXUPC_ID)
#define META_CR_THR0_PCX META_CR_CORE_REG(0, 1, META_CR_TXUPC_ID)
#define META_CR_THR0_SP META_CR_CORE_REG(0, 0, META_CR_TXUA0_ID)

#define META_CR_THR1_PC META_CR_CORE_REG(1, 0, META_CR_TXUPC_ID)
#define META_CR_THR1_PCX META_CR_CORE_REG(1, 1, META_CR_TXUPC_ID)
#define META_CR_THR1_SP META_CR_CORE_REG(1, 0, META_CR_TXUA0_ID)

#define SP_ACCESS(thread) META_CR_CORE_REG(thread, 0, META_CR_TXUA0_ID)
#define PC_ACCESS(thread) META_CR_CORE_REG(thread, 0, META_CR_TXUPC_ID)

#define META_CR_COREREG_ENABLE (0x0000000U)
#define META_CR_COREREG_STATUS (0x0000010U)
#define META_CR_COREREG_DEFR (0x00000A0U)
#define META_CR_COREREG_PRIVEXT (0x00000E8U)

#define META_CR_T0ENABLE_OFFSET \
	(META_CR_CTRLREG_BASE(0U) + META_CR_COREREG_ENABLE)
#define META_CR_T0STATUS_OFFSET \
	(META_CR_CTRLREG_BASE(0U) + META_CR_COREREG_STATUS)
#define META_CR_T0DEFR_OFFSET (META_CR_CTRLREG_BASE(0U) + META_CR_COREREG_DEFR)
#define META_CR_T0PRIVEXT_OFFSET \
	(META_CR_CTRLREG_BASE(0U) + META_CR_COREREG_PRIVEXT)

#define META_CR_T1ENABLE_OFFSET \
	(META_CR_CTRLREG_BASE(1U) + META_CR_COREREG_ENABLE)
#define META_CR_T1STATUS_OFFSET \
	(META_CR_CTRLREG_BASE(1U) + META_CR_COREREG_STATUS)
#define META_CR_T1DEFR_OFFSET (META_CR_CTRLREG_BASE(1U) + META_CR_COREREG_DEFR)
#define META_CR_T1PRIVEXT_OFFSET \
	(META_CR_CTRLREG_BASE(1U) + META_CR_COREREG_PRIVEXT)

#define META_CR_TXENABLE_ENABLE_BIT (0x00000001U) /* Set if running */
#define META_CR_TXSTATUS_PRIV (0x00020000U)
#define META_CR_TXPRIVEXT_MINIM (0x00000080U)

#define META_MEM_GLOBAL_RANGE_BIT (0x80000000U)

#define META_CR_TXCLKCTRL (0x048000B0)
#define META_CR_TXCLKCTRL_ALL_ON (0x55111111)
#define META_CR_TXCLKCTRL_ALL_AUTO (0xAA222222)

#define META_CR_MMCU_LOCAL_EBCTRL (0x04830600)
#define META_CR_MMCU_LOCAL_EBCTRL_ICWIN (0x3 << 14)
#define META_CR_MMCU_LOCAL_EBCTRL_DCWIN (0x3 << 6)
#define META_CR_SYSC_DCPART(n) (0x04830200 + (n) * 0x8)
#define META_CR_SYSC_DCPARTX_CACHED_WRITE_ENABLE (0x1 << 31)
#define META_CR_SYSC_ICPART(n) (0x04830220 + (n) * 0x8)
#define META_CR_SYSC_XCPARTX_LOCAL_ADDR_OFFSET_TOP_HALF (0x8 << 16)
#define META_CR_SYSC_XCPARTX_LOCAL_ADDR_FULL_CACHE (0xF)
#define META_CR_SYSC_XCPARTX_LOCAL_ADDR_HALF_CACHE (0x7)
#define META_CR_MMCU_DCACHE_CTRL (0x04830018)
#define META_CR_MMCU_ICACHE_CTRL (0x04830020)
#define META_CR_MMCU_XCACHE_CTRL_CACHE_HITS_EN (0x1)

/*
 ******************************************************************************
 * META LDR Format
 ******************************************************************************
 */
/* Block header structure. */
struct rogue_meta_ldr_block_hdr {
	u32 dev_id;
	u32 sl_code;
	u32 sl_data;
	u16 pc_ctrl;
	u16 crc;
};

/* High level data stream block structure. */
struct rogue_meta_ldr_l1_data_blk {
	u16 cmd;
	u16 length;
	u32 next;
	u32 cmd_data[4];
};

/* High level data stream block structure. */
struct rogue_meta_ldr_l2_data_blk {
	u16 tag;
	u16 length;
	u32 block_data[4];
};

/* Config command structure. */
struct rogue_meta_ldr_cfg_blk {
	u32 type;
	u32 block_data[4];
};

/* Block type definitions */
#define ROGUE_META_LDR_COMMENT_TYPE_MASK (0x0010U)
#define ROGUE_META_LDR_BLK_IS_COMMENT(x) (((x) & ROGUE_META_LDR_COMMENT_TYPE_MASK) != 0U)

/*
 * Command definitions
 *  Value   Name            Description
 *  0       LoadMem         Load memory with binary data.
 *  1       LoadCore        Load a set of core registers.
 *  2       LoadMMReg       Load a set of memory mapped registers.
 *  3       StartThreads    Set each thread PC and SP, then enable threads.
 *  4       ZeroMem         Zeros a memory region.
 *  5       Config          Perform a configuration command.
 */
#define ROGUE_META_LDR_CMD_MASK (0x000FU)

#define ROGUE_META_LDR_CMD_LOADMEM (0x0000U)
#define ROGUE_META_LDR_CMD_LOADCORE (0x0001U)
#define ROGUE_META_LDR_CMD_LOADMMREG (0x0002U)
#define ROGUE_META_LDR_CMD_START_THREADS (0x0003U)
#define ROGUE_META_LDR_CMD_ZEROMEM (0x0004U)
#define ROGUE_META_LDR_CMD_CONFIG (0x0005U)

/*
 * Config Command definitions
 *  Value   Name        Description
 *  0       Pause       Pause for x times 100 instructions
 *  1       Read        Read a value from register - No value return needed.
 *                      Utilises effects of issuing reads to certain registers
 *  2       Write       Write to mem location
 *  3       MemSet      Set mem to value
 *  4       MemCheck    check mem for specific value.
 */
#define ROGUE_META_LDR_CFG_PAUSE (0x0000)
#define ROGUE_META_LDR_CFG_READ (0x0001)
#define ROGUE_META_LDR_CFG_WRITE (0x0002)
#define ROGUE_META_LDR_CFG_MEMSET (0x0003)
#define ROGUE_META_LDR_CFG_MEMCHECK (0x0004)

/*
 ******************************************************************************
 * ROGUE FW segmented MMU definitions
 ******************************************************************************
 */
/* All threads can access the segment. */
#define ROGUE_FW_SEGMMU_ALLTHRS (0xf << 8U)
/* Writable. */
#define ROGUE_FW_SEGMMU_WRITEABLE (0x1U << 1U)
/* All threads can access and writable. */
#define ROGUE_FW_SEGMMU_ALLTHRS_WRITEABLE \
	(ROGUE_FW_SEGMMU_ALLTHRS | ROGUE_FW_SEGMMU_WRITEABLE)

/* Direct map region 10 used for mapping GPU memory - max 8MB. */
#define ROGUE_FW_SEGMMU_DMAP_GPU_ID (10U)
#define ROGUE_FW_SEGMMU_DMAP_GPU_ADDR_START (0x07000000U)
#define ROGUE_FW_SEGMMU_DMAP_GPU_MAX_SIZE (0x00800000U)

/* Segment IDs. */
#define ROGUE_FW_SEGMMU_DATA_ID (1U)
#define ROGUE_FW_SEGMMU_BOOTLDR_ID (2U)
#define ROGUE_FW_SEGMMU_TEXT_ID (ROGUE_FW_SEGMMU_BOOTLDR_ID)

/*
 * SLC caching strategy in S7 and volcanic is emitted through the segment MMU.
 * All the segments configured through the macro ROGUE_FW_SEGMMU_OUTADDR_TOP are
 * CACHED in the SLC.
 * The interface has been kept the same to simplify the code changes.
 * The bifdm argument is ignored (no longer relevant) in S7 and volcanic.
 */
#define ROGUE_FW_SEGMMU_OUTADDR_TOP_VIVT_SLC(pers, slc_policy, mmu_ctx)  \
	((((u64)((pers) & 0x3)) << 52) | (((u64)((mmu_ctx) & 0xFF)) << 44) | \
	 (((u64)((slc_policy) & 0x1)) << 40))
#define ROGUE_FW_SEGMMU_OUTADDR_TOP_VIVT_SLC_CACHED(mmu_ctx) \
	ROGUE_FW_SEGMMU_OUTADDR_TOP_VIVT_SLC(0x3, 0x0, mmu_ctx)
#define ROGUE_FW_SEGMMU_OUTADDR_TOP_VIVT_SLC_UNCACHED(mmu_ctx) \
	ROGUE_FW_SEGMMU_OUTADDR_TOP_VIVT_SLC(0x0, 0x1, mmu_ctx)

/*
 * To configure the Page Catalog and BIF-DM fed into the BIF for Garten
 * accesses through this segment.
 */
#define ROGUE_FW_SEGMMU_OUTADDR_TOP_SLC(pc, bifdm) \
	(((u64)((u64)(pc) & 0xFU) << 44U) | ((u64)((u64)(bifdm) & 0xFU) << 40U))

#define ROGUE_FW_SEGMMU_META_BIFDM_ID (0x7U)

/* META segments have 4kB minimum size. */
#define ROGUE_FW_SEGMMU_ALIGN (0x1000U)

/* Segmented MMU registers (n = segment id). */
#define META_CR_MMCU_SEGMENT_N_BASE(n) (0x04850000U + ((n) * 0x10U))
#define META_CR_MMCU_SEGMENT_N_LIMIT(n) (0x04850004U + ((n) * 0x10U))
#define META_CR_MMCU_SEGMENT_N_OUTA0(n) (0x04850008U + ((n) * 0x10U))
#define META_CR_MMCU_SEGMENT_N_OUTA1(n) (0x0485000CU + ((n) * 0x10U))

/*
 * The following defines must be recalculated if the Meta MMU segments used
 * to access Host-FW data are changed
 * Current combinations are:
 * - SLC uncached, META cached,   FW base address 0x70000000
 * - SLC uncached, META uncached, FW base address 0xF0000000
 * - SLC cached,   META cached,   FW base address 0x10000000
 * - SLC cached,   META uncached, FW base address 0x90000000
 */
#define ROGUE_FW_SEGMMU_DATA_BASE_ADDRESS (0x10000000U)
#define ROGUE_FW_SEGMMU_DATA_META_CACHED (0x0U)
#define ROGUE_FW_SEGMMU_DATA_META_UNCACHED (META_MEM_GLOBAL_RANGE_BIT)
#define ROGUE_FW_SEGMMU_DATA_META_CACHE_MASK (META_MEM_GLOBAL_RANGE_BIT)
/*
 * For non-VIVT SLCs the cacheability of the FW data in the SLC is selected in
 * the PTEs for the FW data, not in the Meta Segment MMU, which means these
 * defines have no real effect in those cases.
 */
#define ROGUE_FW_SEGMMU_DATA_VIVT_SLC_CACHED (0x0U)
#define ROGUE_FW_SEGMMU_DATA_VIVT_SLC_UNCACHED (0x60000000U)
#define ROGUE_FW_SEGMMU_DATA_VIVT_SLC_CACHE_MASK (0x60000000U)

/*
 ******************************************************************************
 * ROGUE FW Bootloader defaults
 ******************************************************************************
 */
#define ROGUE_FW_BOOTLDR_META_ADDR (0x40000000U)
#define ROGUE_FW_BOOTLDR_DEVV_ADDR_0 (0xC0000000U)
#define ROGUE_FW_BOOTLDR_DEVV_ADDR_1 (0x000000E1)
#define ROGUE_FW_BOOTLDR_DEVV_ADDR                     \
	((((u64)ROGUE_FW_BOOTLDR_DEVV_ADDR_1) << 32) | \
	 ROGUE_FW_BOOTLDR_DEVV_ADDR_0)
#define ROGUE_FW_BOOTLDR_LIMIT (0x1FFFF000)
#define ROGUE_FW_MAX_BOOTLDR_OFFSET (0x1000)

/* Bootloader configuration offset is in dwords (512 bytes) */
#define ROGUE_FW_BOOTLDR_CONF_OFFSET (0x80)

/*
 ******************************************************************************
 * ROGUE META Stack
 ******************************************************************************
 */
#define ROGUE_META_STACK_SIZE (0x1000U)

/*
 ******************************************************************************
 * ROGUE META Core memory
 ******************************************************************************
 */
/* Code and data both map to the same physical memory. */
#define ROGUE_META_COREMEM_CODE_ADDR (0x80000000U)
#define ROGUE_META_COREMEM_DATA_ADDR (0x82000000U)
#define ROGUE_META_COREMEM_OFFSET_MASK (0x01ffffffU)

#define ROGUE_META_IS_COREMEM_CODE(a, b)                                \
	({                                                              \
		u32 _a = (a), _b = (b);                                 \
		((_a) >= ROGUE_META_COREMEM_CODE_ADDR) &&               \
			((_a) < (ROGUE_META_COREMEM_CODE_ADDR + (_b))); \
	})
#define ROGUE_META_IS_COREMEM_DATA(a, b)                                \
	({                                                              \
		u32 _a = (a), _b = (b);                                 \
		((_a) >= ROGUE_META_COREMEM_DATA_ADDR) &&               \
			((_a) < (ROGUE_META_COREMEM_DATA_ADDR + (_b))); \
	})
/*
 ******************************************************************************
 * 2nd thread
 ******************************************************************************
 */
#define ROGUE_FW_THR1_PC (0x18930000)
#define ROGUE_FW_THR1_SP (0x78890000)

/*
 ******************************************************************************
 * META compatibility
 ******************************************************************************
 */

#define META_CR_CORE_ID (0x04831000)
#define META_CR_CORE_ID_VER_SHIFT (16U)
#define META_CR_CORE_ID_VER_CLRMSK (0XFF00FFFFU)

#define ROGUE_CR_META_MTP218_CORE_ID_VALUE 0x19
#define ROGUE_CR_META_MTP219_CORE_ID_VALUE 0x1E
#define ROGUE_CR_META_LTP218_CORE_ID_VALUE 0x1C
#define ROGUE_CR_META_LTP217_CORE_ID_VALUE 0x1F

#define ROGUE_FW_PROCESSOR_META "META"

#endif /* PVR_ROGUE_META_H */
