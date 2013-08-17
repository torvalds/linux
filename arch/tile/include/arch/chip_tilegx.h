/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/*
 * @file
 * Global header file.
 * This header file specifies defines for TILE-Gx.
 */

#ifndef __ARCH_CHIP_H__
#define __ARCH_CHIP_H__

/** Specify chip version.
 * When possible, prefer the CHIP_xxx symbols below for future-proofing.
 * This is intended for cross-compiling; native compilation should
 * use the predefined __tile_chip__ symbol.
 */
#define TILE_CHIP 10

/** Specify chip revision.
 * This provides for the case of a respin of a particular chip type;
 * the normal value for this symbol is "0".
 * This is intended for cross-compiling; native compilation should
 * use the predefined __tile_chip_rev__ symbol.
 */
#define TILE_CHIP_REV 0

/** The name of this architecture. */
#define CHIP_ARCH_NAME "tilegx"

/** The ELF e_machine type for binaries for this chip. */
#define CHIP_ELF_TYPE() EM_TILEGX

/** The alternate ELF e_machine type for binaries for this chip. */
#define CHIP_COMPAT_ELF_TYPE() 0x2597

/** What is the native word size of the machine? */
#define CHIP_WORD_SIZE() 64

/** How many bits of a virtual address are used. Extra bits must be
 * the sign extension of the low bits.
 */
#define CHIP_VA_WIDTH() 42

/** How many bits are in a physical address? */
#define CHIP_PA_WIDTH() 40

/** Size of the L2 cache, in bytes. */
#define CHIP_L2_CACHE_SIZE() 262144

/** Log size of an L2 cache line in bytes. */
#define CHIP_L2_LOG_LINE_SIZE() 6

/** Size of an L2 cache line, in bytes. */
#define CHIP_L2_LINE_SIZE() (1 << CHIP_L2_LOG_LINE_SIZE())

/** Associativity of the L2 cache. */
#define CHIP_L2_ASSOC() 8

/** Size of the L1 data cache, in bytes. */
#define CHIP_L1D_CACHE_SIZE() 32768

/** Log size of an L1 data cache line in bytes. */
#define CHIP_L1D_LOG_LINE_SIZE() 6

/** Size of an L1 data cache line, in bytes. */
#define CHIP_L1D_LINE_SIZE() (1 << CHIP_L1D_LOG_LINE_SIZE())

/** Associativity of the L1 data cache. */
#define CHIP_L1D_ASSOC() 2

/** Size of the L1 instruction cache, in bytes. */
#define CHIP_L1I_CACHE_SIZE() 32768

/** Log size of an L1 instruction cache line in bytes. */
#define CHIP_L1I_LOG_LINE_SIZE() 6

/** Size of an L1 instruction cache line, in bytes. */
#define CHIP_L1I_LINE_SIZE() (1 << CHIP_L1I_LOG_LINE_SIZE())

/** Associativity of the L1 instruction cache. */
#define CHIP_L1I_ASSOC() 2

/** Stride with which flush instructions must be issued. */
#define CHIP_FLUSH_STRIDE() CHIP_L2_LINE_SIZE()

/** Stride with which inv instructions must be issued. */
#define CHIP_INV_STRIDE() CHIP_L2_LINE_SIZE()

/** Stride with which finv instructions must be issued. */
#define CHIP_FINV_STRIDE() CHIP_L2_LINE_SIZE()

/** Can the local cache coherently cache data that is homed elsewhere? */
#define CHIP_HAS_COHERENT_LOCAL_CACHE() 1

/** How many simultaneous outstanding victims can the L2 cache have? */
#define CHIP_MAX_OUTSTANDING_VICTIMS() 128

/** Does the TLB support the NC and NOALLOC bits? */
#define CHIP_HAS_NC_AND_NOALLOC_BITS() 1

/** Does the chip support hash-for-home caching? */
#define CHIP_HAS_CBOX_HOME_MAP() 1

/** Number of entries in the chip's home map tables. */
#define CHIP_CBOX_HOME_MAP_SIZE() 128

/** Do uncacheable requests miss in the cache regardless of whether
 * there is matching data? */
#define CHIP_HAS_ENFORCED_UNCACHEABLE_REQUESTS() 1

/** Does the mf instruction wait for victims? */
#define CHIP_HAS_MF_WAITS_FOR_VICTIMS() 0

/** Does the chip have an "inv" instruction that doesn't also flush? */
#define CHIP_HAS_INV() 1

/** Does the chip have a "wh64" instruction? */
#define CHIP_HAS_WH64() 1

/** Does this chip have a 'dword_align' instruction? */
#define CHIP_HAS_DWORD_ALIGN() 0

/** Number of performance counters. */
#define CHIP_PERFORMANCE_COUNTERS() 4

/** Does this chip have auxiliary performance counters? */
#define CHIP_HAS_AUX_PERF_COUNTERS() 1

/** Is the CBOX_MSR1 SPR supported? */
#define CHIP_HAS_CBOX_MSR1() 0

/** Is the TILE_RTF_HWM SPR supported? */
#define CHIP_HAS_TILE_RTF_HWM() 1

/** Is the TILE_WRITE_PENDING SPR supported? */
#define CHIP_HAS_TILE_WRITE_PENDING() 0

/** Is the PROC_STATUS SPR supported? */
#define CHIP_HAS_PROC_STATUS_SPR() 1

/** Is the DSTREAM_PF SPR supported? */
#define CHIP_HAS_DSTREAM_PF() 1

/** Log of the number of mshims we have. */
#define CHIP_LOG_NUM_MSHIMS() 2

/** Are the bases of the interrupt vector areas fixed? */
#define CHIP_HAS_FIXED_INTVEC_BASE() 0

/** Are the interrupt masks split up into 2 SPRs? */
#define CHIP_HAS_SPLIT_INTR_MASK() 0

/** Is the cycle count split up into 2 SPRs? */
#define CHIP_HAS_SPLIT_CYCLE() 0

/** Does the chip have a static network? */
#define CHIP_HAS_SN() 0

/** Does the chip have a static network processor? */
#define CHIP_HAS_SN_PROC() 0

/** Size of the L1 static network processor instruction cache, in bytes. */
/* #define CHIP_L1SNI_CACHE_SIZE() -- does not apply to chip 10 */

/** Does the chip have DMA support in each tile? */
#define CHIP_HAS_TILE_DMA() 0

/** Does the chip have the second revision of the directly accessible
 *  dynamic networks?  This encapsulates a number of characteristics,
 *  including the absence of the catch-all, the absence of inline message
 *  tags, the absence of support for network context-switching, and so on.
 */
#define CHIP_HAS_REV1_XDN() 1

/** Does the chip have cmpexch and similar (fetchadd, exch, etc.)? */
#define CHIP_HAS_CMPEXCH() 1

/** Does the chip have memory-mapped I/O support? */
#define CHIP_HAS_MMIO() 1

/** Does the chip have post-completion interrupts? */
#define CHIP_HAS_POST_COMPLETION_INTERRUPTS() 1

/** Does the chip have native single step support? */
#define CHIP_HAS_SINGLE_STEP() 1

#ifndef __OPEN_SOURCE__  /* features only relevant to hypervisor-level code */

/** How many entries are present in the instruction TLB? */
#define CHIP_ITLB_ENTRIES() 16

/** How many entries are present in the data TLB? */
#define CHIP_DTLB_ENTRIES() 32

/** How many MAF entries does the XAUI shim have? */
#define CHIP_XAUI_MAF_ENTRIES() 32

/** Does the memory shim have a source-id table? */
#define CHIP_HAS_MSHIM_SRCID_TABLE() 0

/** Does the L1 instruction cache clear on reset? */
#define CHIP_HAS_L1I_CLEAR_ON_RESET() 1

/** Does the chip come out of reset with valid coordinates on all tiles?
 * Note that if defined, this also implies that the upper left is 1,1.
 */
#define CHIP_HAS_VALID_TILE_COORD_RESET() 1

/** Does the chip have unified packet formats? */
#define CHIP_HAS_UNIFIED_PACKET_FORMATS() 1

/** Does the chip support write reordering? */
#define CHIP_HAS_WRITE_REORDERING() 1

/** Does the chip support Y-X routing as well as X-Y? */
#define CHIP_HAS_Y_X_ROUTING() 1

/** Is INTCTRL_3 managed with the correct MPL? */
#define CHIP_HAS_INTCTRL_3_STATUS_FIX() 1

/** Is it possible to configure the chip to be big-endian? */
#define CHIP_HAS_BIG_ENDIAN_CONFIG() 1

/** Is the CACHE_RED_WAY_OVERRIDDEN SPR supported? */
#define CHIP_HAS_CACHE_RED_WAY_OVERRIDDEN() 0

/** Is the DIAG_TRACE_WAY SPR supported? */
#define CHIP_HAS_DIAG_TRACE_WAY() 0

/** Is the MEM_STRIPE_CONFIG SPR supported? */
#define CHIP_HAS_MEM_STRIPE_CONFIG() 1

/** Are the TLB_PERF SPRs supported? */
#define CHIP_HAS_TLB_PERF() 1

/** Is the VDN_SNOOP_SHIM_CTL SPR supported? */
#define CHIP_HAS_VDN_SNOOP_SHIM_CTL() 0

/** Does the chip support rev1 DMA packets? */
#define CHIP_HAS_REV1_DMA_PACKETS() 1

/** Does the chip have an IPI shim? */
#define CHIP_HAS_IPI() 1

#endif /* !__OPEN_SOURCE__ */
#endif /* __ARCH_CHIP_H__ */
