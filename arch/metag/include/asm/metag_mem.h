/*
 * asm/metag_mem.h
 *
 * Copyright (C) 2000-2007, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Various defines for Meta (memory-mapped) registers.
 */

#ifndef _ASM_METAG_MEM_H_
#define _ASM_METAG_MEM_H_

/*****************************************************************************
 *                   META MEMORY MAP LINEAR ADDRESS VALUES
 ****************************************************************************/
/*
 * COMMON MEMORY MAP
 * -----------------
 */

#define LINSYSTEM_BASE  0x00200000
#define LINSYSTEM_LIMIT 0x07FFFFFF

/* Linear cache flush now implemented via DCACHE instruction. These defines
   related to a special region that used to exist for achieving cache flushes.
 */
#define         LINSYSLFLUSH_S 0

#define     LINSYSRES0_BASE     0x00200000
#define     LINSYSRES0_LIMIT    0x01FFFFFF

#define     LINSYSCUSTOM_BASE 0x02000000
#define     LINSYSCUSTOM_LIMIT   0x02FFFFFF

#define     LINSYSEXPAND_BASE 0x03000000
#define     LINSYSEXPAND_LIMIT   0x03FFFFFF

#define     LINSYSEVENT_BASE  0x04000000
#define         LINSYSEVENT_WR_ATOMIC_UNLOCK    0x04000000
#define         LINSYSEVENT_WR_ATOMIC_LOCK      0x04000040
#define         LINSYSEVENT_WR_CACHE_DISABLE    0x04000080
#define         LINSYSEVENT_WR_CACHE_ENABLE     0x040000C0
#define         LINSYSEVENT_WR_COMBINE_FLUSH    0x04000100
#define         LINSYSEVENT_WR_FENCE            0x04000140
#define     LINSYSEVENT_LIMIT   0x04000FFF

#define     LINSYSCFLUSH_BASE   0x04400000
#define         LINSYSCFLUSH_DCACHE_LINE    0x04400000
#define         LINSYSCFLUSH_ICACHE_LINE    0x04500000
#define         LINSYSCFLUSH_MMCU           0x04700000
#ifndef METAC_1_2
#define         LINSYSCFLUSH_TxMMCU_BASE    0x04700020
#define         LINSYSCFLUSH_TxMMCU_STRIDE  0x00000008
#endif
#define         LINSYSCFLUSH_ADDR_BITS      0x000FFFFF
#define         LINSYSCFLUSH_ADDR_S         0
#define     LINSYSCFLUSH_LIMIT  0x047FFFFF

#define     LINSYSCTRL_BASE     0x04800000
#define     LINSYSCTRL_LIMIT    0x04FFFFFF

#define     LINSYSMTABLE_BASE   0x05000000
#define     LINSYSMTABLE_LIMIT  0x05FFFFFF

#define     LINSYSDIRECT_BASE   0x06000000
#define     LINSYSDIRECT_LIMIT  0x07FFFFFF

#define LINLOCAL_BASE   0x08000000
#define LINLOCAL_LIMIT  0x7FFFFFFF

#define LINCORE_BASE    0x80000000
#define LINCORE_LIMIT   0x87FFFFFF

#define LINCORE_CODE_BASE  0x80000000
#define LINCORE_CODE_LIMIT 0x81FFFFFF

#define LINCORE_DATA_BASE  0x82000000
#define LINCORE_DATA_LIMIT 0x83FFFFFF


/* The core can support locked icache lines in this region */
#define LINCORE_ICACHE_BASE  0x84000000
#define LINCORE_ICACHE_LIMIT 0x85FFFFFF

/* The core can support locked dcache lines in this region */
#define LINCORE_DCACHE_BASE  0x86000000
#define LINCORE_DCACHE_LIMIT 0x87FFFFFF

#define LINGLOBAL_BASE  0x88000000
#define LINGLOBAL_LIMIT 0xFFFDFFFF

/*
 * CHIP Core Register Map
 * ----------------------
 */
#define CORE_HWBASE     0x04800000
#define PRIV_HWBASE     0x04810000
#define TRIG_HWBASE     0x04820000
#define SYSC_HWBASE     0x04830000

/*****************************************************************************
 *         INTER-THREAD KICK REGISTERS FOR SOFTWARE EVENT GENERATION
 ****************************************************************************/
/*
 * These values define memory mapped registers that can be used to supply
 * kicks to threads that service arbitrary software events.
 */

#define T0KICK     0x04800800   /* Background kick 0     */
#define     TXXKICK_MAX 0xFFFF  /* Maximum kicks */
#define     TnXKICK_STRIDE      0x00001000  /* Thread scale value    */
#define     TnXKICK_STRIDE_S    12
#define T0KICKI    0x04800808   /* Interrupt kick 0      */
#define     TXIKICK_OFFSET  0x00000008  /* Int level offset value */
#define T1KICK     0x04801800   /* Background kick 1     */
#define T1KICKI    0x04801808   /* Interrupt kick 1      */
#define T2KICK     0x04802800   /* Background kick 2     */
#define T2KICKI    0x04802808   /* Interrupt kick 2      */
#define T3KICK     0x04803800   /* Background kick 3     */
#define T3KICKI    0x04803808   /* Interrupt kick 3      */

/*****************************************************************************
 *                GLOBAL REGISTER ACCESS RESOURCES
 ****************************************************************************/
/*
 * These values define memory mapped registers that allow access to the
 * internal state of all threads in order to allow global set-up of thread
 * state and external handling of thread events, errors, or debugging.
 *
 * The actual unit and register index values needed to access individul
 * registers are chip specific see - METAC_TXUXX_VALUES in metac_x_y.h.
 * However two C array initialisers TXUXX_MASKS and TGUXX_MASKS will always be
 * defined to allow arbitrary loading, display, and saving of all valid
 * register states without detailed knowledge of their purpose - TXUXX sets
 * bits for all valid registers and TGUXX sets bits for the sub-set which are
 * global.
 */

#define T0UCTREG0   0x04800000  /* Access to all CT regs */
#define TnUCTRX_STRIDE      0x00001000  /* Thread scale value    */
#define TXUCTREGn_STRIDE    0x00000008  /* Register scale value  */

#define TXUXXRXDT  0x0480FFF0   /* Data to/from any threads reg */
#define TXUXXRXRQ  0x0480FFF8
#define     TXUXXRXRQ_DREADY_BIT 0x80000000  /* Poll for done */
#define     TXUXXRXRQ_DSPEXT_BIT 0x00020000  /* Addr DSP Regs */
#define     TXUXXRXRQ_RDnWR_BIT  0x00010000  /* Set for read  */
#define     TXUXXRXRQ_TX_BITS    0x00003000  /* Thread number */
#define     TXUXXRXRQ_TX_S       12
#define     TXUXXRXRQ_RX_BITS    0x000001F0  /* Register num  */
#define     TXUXXRXRQ_RX_S       4
#define         TXUXXRXRQ_DSPRARD0    0      /* DSP RAM A Read Pointer 0 */
#define         TXUXXRXRQ_DSPRARD1    1      /* DSP RAM A Read Pointer 1 */
#define         TXUXXRXRQ_DSPRAWR0    2      /* DSP RAM A Write Pointer 0 */
#define         TXUXXRXRQ_DSPRAWR2    3      /* DSP RAM A Write Pointer 1 */
#define         TXUXXRXRQ_DSPRBRD0    4      /* DSP RAM B Read Pointer 0 */
#define         TXUXXRXRQ_DSPRBRD1    5      /* DSP RAM B Read Pointer 1 */
#define         TXUXXRXRQ_DSPRBWR0    6      /* DSP RAM B Write Pointer 0 */
#define         TXUXXRXRQ_DSPRBWR1    7      /* DSP RAM B Write Pointer 1 */
#define         TXUXXRXRQ_DSPRARINC0  8      /* DSP RAM A Read Increment 0 */
#define         TXUXXRXRQ_DSPRARINC1  9      /* DSP RAM A Read Increment 1 */
#define         TXUXXRXRQ_DSPRAWINC0 10      /* DSP RAM A Write Increment 0 */
#define         TXUXXRXRQ_DSPRAWINC1 11      /* DSP RAM A Write Increment 1 */
#define         TXUXXRXRQ_DSPRBRINC0 12      /* DSP RAM B Read Increment 0 */
#define         TXUXXRXRQ_DSPRBRINC1 13      /* DSP RAM B Read Increment 1 */
#define         TXUXXRXRQ_DSPRBWINC0 14      /* DSP RAM B Write Increment 0 */
#define         TXUXXRXRQ_DSPRBWINC1 15      /* DSP RAM B Write Increment 1 */

#define         TXUXXRXRQ_ACC0L0     16      /* Accumulator 0 bottom 32-bits */
#define         TXUXXRXRQ_ACC1L0     17      /* Accumulator 1 bottom 32-bits */
#define         TXUXXRXRQ_ACC2L0     18      /* Accumulator 2 bottom 32-bits */
#define         TXUXXRXRQ_ACC3L0     19      /* Accumulator 3 bottom 32-bits */
#define         TXUXXRXRQ_ACC0HI     20      /* Accumulator 0 top 8-bits */
#define         TXUXXRXRQ_ACC1HI     21      /* Accumulator 1 top 8-bits */
#define         TXUXXRXRQ_ACC2HI     22      /* Accumulator 2 top 8-bits */
#define         TXUXXRXRQ_ACC3HI     23      /* Accumulator 3 top 8-bits */
#define     TXUXXRXRQ_UXX_BITS   0x0000000F  /* Unit number   */
#define     TXUXXRXRQ_UXX_S      0

/*****************************************************************************
 *          PRIVILEGE CONTROL VALUES FOR MEMORY MAPPED RESOURCES
 ****************************************************************************/
/*
 * These values define memory mapped registers that give control over and
 * the privilege required to access other memory mapped resources. These
 * registers themselves always require privilege to update them.
 */

#define TXPRIVREG_STRIDE    0x8 /* Delta between per-thread regs */
#define TXPRIVREG_STRIDE_S  3

/*
 * Each bit 0 to 15 defines privilege required to access internal register
 * regions 0x04800000 to 0x048FFFFF in 64k chunks
 */
#define T0PIOREG    0x04810100
#define T1PIOREG    0x04810108
#define T2PIOREG    0x04810110
#define T3PIOREG    0x04810118

/*
 * Each bit 0 to 31 defines privilege required to use the pair of
 * system events implemented as writee in the regions 0x04000000 to
 * 0x04000FFF in 2*64 byte chunks.
 */
#define T0PSYREG    0x04810180
#define T1PSYREG    0x04810188
#define T2PSYREG    0x04810190
#define T3PSYREG    0x04810198

/*
 * CHIP PRIV CONTROLS
 * ------------------
 */

/* The TXPIOREG register holds a bit mask directly mappable to
   corresponding addresses in the range 0x04800000 to 049FFFFF */
#define     TXPIOREG_ADDR_BITS  0x1F0000 /* Up to 32x64K bytes */
#define     TXPIOREG_ADDR_S     16

/* Hence based on the _HWBASE values ... */
#define     TXPIOREG_CORE_BIT       (1<<((0x04800000>>16)&0x1F))
#define     TXPIOREG_PRIV_BIT       (1<<((0x04810000>>16)&0x1F))
#define     TXPIOREG_TRIG_BIT       (1<<((0x04820000>>16)&0x1F))
#define     TXPIOREG_SYSC_BIT       (1<<((0x04830000>>16)&0x1F))

#define     TXPIOREG_WRC_BIT          0x00080000  /* Wr combiner reg priv */
#define     TXPIOREG_LOCALBUS_RW_BIT  0x00040000  /* Local bus rd/wr priv */
#define     TXPIOREG_SYSREGBUS_RD_BIT 0x00020000  /* Sys reg bus write priv */
#define     TXPIOREG_SYSREGBUS_WR_BIT 0x00010000  /* Sys reg bus read priv */

/* CORE region privilege controls */
#define T0PRIVCORE 0x04800828
#define         TXPRIVCORE_TXBKICK_BIT   0x001  /* Background kick priv */
#define         TXPRIVCORE_TXIKICK_BIT   0x002  /* Interrupt kick priv  */
#define         TXPRIVCORE_TXAMAREGX_BIT 0x004  /* TXAMAREG4|5|6 priv   */
#define TnPRIVCORE_STRIDE 0x00001000

#define T0PRIVSYSR 0x04810000
#define     TnPRIVSYSR_STRIDE   0x00000008
#define     TnPRIVSYSR_STRIDE_S 3
#define     TXPRIVSYSR_CFLUSH_BIT     0x01
#define     TXPRIVSYSR_MTABLE_BIT     0x02
#define     TXPRIVSYSR_DIRECT_BIT     0x04
#ifdef METAC_1_2
#define     TXPRIVSYSR_ALL_BITS       0x07
#else
#define     TXPRIVSYSR_CORE_BIT       0x08
#define     TXPRIVSYSR_CORECODE_BIT   0x10
#define     TXPRIVSYSR_ALL_BITS       0x1F
#endif
#define T1PRIVSYSR 0x04810008
#define T2PRIVSYSR 0x04810010
#define T3PRIVSYSR 0x04810018

/*****************************************************************************
 *          H/W TRIGGER STATE/LEVEL REGISTERS AND H/W TRIGGER VECTORS
 ****************************************************************************/
/*
 * These values define memory mapped registers that give control over and
 * the state of hardware trigger sources both external to the META processor
 * and internal to it.
 */

#define HWSTATMETA  0x04820000  /* Hardware status/clear META trig */
#define         HWSTATMETA_T0HALT_BITS 0xF
#define         HWSTATMETA_T0HALT_S    0
#define     HWSTATMETA_T0BHALT_BIT 0x1  /* Background HALT */
#define     HWSTATMETA_T0IHALT_BIT 0x2  /* Interrupt HALT  */
#define     HWSTATMETA_T0PHALT_BIT 0x4  /* PF/RO Memory HALT */
#define     HWSTATMETA_T0AMATR_BIT 0x8  /* AMA trigger */
#define     HWSTATMETA_TnINT_S     4    /* Shift by (thread*4) */
#define HWSTATEXT   0x04820010  /* H/W status/clear external trigs  0-31 */
#define HWSTATEXT2  0x04820018  /* H/W status/clear external trigs 32-63 */
#define HWSTATEXT4  0x04820020  /* H/W status/clear external trigs 64-95 */
#define HWSTATEXT6  0x04820028  /* H/W status/clear external trigs 96-128 */
#define HWLEVELEXT  0x04820030  /* Edge/Level type of external trigs  0-31 */
#define HWLEVELEXT2 0x04820038  /* Edge/Level type of external trigs 32-63 */
#define HWLEVELEXT4 0x04820040  /* Edge/Level type of external trigs 64-95 */
#define HWLEVELEXT6 0x04820048  /* Edge/Level type of external trigs 96-128 */
#define     HWLEVELEXT_XXX_LEVEL 1  /* Level sense logic in HWSTATEXTn */
#define     HWLEVELEXT_XXX_EDGE  0
#define HWMASKEXT   0x04820050  /* Enable/disable of external trigs  0-31 */
#define HWMASKEXT2  0x04820058  /* Enable/disable of external trigs 32-63 */
#define HWMASKEXT4  0x04820060  /* Enable/disable of external trigs 64-95 */
#define HWMASKEXT6  0x04820068  /* Enable/disable of external trigs 96-128 */
#define T0VECINT_BHALT  0x04820500  /* Background HALT trigger vector */
#define     TXVECXXX_BITS   0xF       /* Per-trigger vector vals 0,1,4-15 */
#define     TXVECXXX_S  0
#define T0VECINT_IHALT  0x04820508  /* Interrupt HALT */
#define T0VECINT_PHALT  0x04820510  /* PF/RO memory fault */
#define T0VECINT_AMATR  0x04820518  /* AMA trigger */
#define     TnVECINT_STRIDE 0x00000020  /* Per thread stride */
#define HWVEC0EXT   0x04820700  /* Vectors for external triggers  0-31 */
#define HWVEC20EXT  0x04821700  /* Vectors for external triggers 32-63 */
#define HWVEC40EXT  0x04822700  /* Vectors for external triggers 64-95 */
#define HWVEC60EXT  0x04823700  /* Vectors for external triggers 96-127 */
#define     HWVECnEXT_STRIDE 0x00000008 /* Per trigger stride */
#define HWVECnEXT_DEBUG 0x1         /* Redirect trigger to debug i/f */

/*
 * CORE HWCODE-BREAKPOINT REGISTERS/VALUES
 * ---------------------------------------
 */
#define CODEB0ADDR         0x0480FF00  /* Address specifier */
#define     CODEBXADDR_MATCHX_BITS 0xFFFFFFFC
#define     CODEBXADDR_MATCHX_S    2
#define CODEB0CTRL         0x0480FF08  /* Control */
#define     CODEBXCTRL_MATEN_BIT   0x80000000   /* Match 'Enable'  */
#define     CODEBXCTRL_MATTXEN_BIT 0x10000000   /* Match threadn enable */
#define     CODEBXCTRL_HITC_BITS   0x00FF0000   /* Hit counter   */
#define     CODEBXCTRL_HITC_S      16
#define           CODEBXHITC_NEXT  0xFF     /* Next 'hit' will trigger */
#define           CODEBXHITC_HIT1  0x00     /* No 'hits' after trigger */
#define     CODEBXCTRL_MMASK_BITS  0x0000FFFC   /* Mask ADDR_MATCH bits */
#define     CODEBXCTRL_MMASK_S     2
#define     CODEBXCTRL_MATLTX_BITS 0x00000003   /* Match threadn LOCAL addr */
#define     CODEBXCTRL_MATLTX_S    0            /* Match threadn LOCAL addr */
#define CODEBnXXXX_STRIDE      0x00000010  /* Stride between CODEB reg sets */
#define CODEBnXXXX_STRIDE_S    4
#define CODEBnXXXX_LIMIT       3           /* Sets 0-3 */

/*
 * CORE DATA-WATCHPOINT REGISTERS/VALUES
 * -------------------------------------
 */
#define DATAW0ADDR         0x0480FF40  /* Address specifier */
#define     DATAWXADDR_MATCHR_BITS 0xFFFFFFF8
#define     DATAWXADDR_MATCHR_S    3
#define     DATAWXADDR_MATCHW_BITS 0xFFFFFFFF
#define     DATAWXADDR_MATCHW_S    0
#define DATAW0CTRL         0x0480FF48  /* Control */
#define     DATAWXCTRL_MATRD_BIT   0x80000000   /* Match 'Read'  */
#ifndef METAC_1_2
#define     DATAWXCTRL_MATNOTTX_BIT 0x20000000  /* Invert threadn enable */
#endif
#define     DATAWXCTRL_MATWR_BIT   0x40000000   /* Match 'Write' */
#define     DATAWXCTRL_MATTXEN_BIT 0x10000000   /* Match threadn enable */
#define     DATAWXCTRL_WRSIZE_BITS 0x0F000000   /* Write Match Size */
#define     DATAWXCTRL_WRSIZE_S    24
#define         DATAWWRSIZE_ANY   0         /* Any size transaction matches */
#define         DATAWWRSIZE_8BIT  1     /* Specific sizes ... */
#define         DATAWWRSIZE_16BIT 2
#define         DATAWWRSIZE_32BIT 3
#define         DATAWWRSIZE_64BIT 4
#define     DATAWXCTRL_HITC_BITS   0x00FF0000   /* Hit counter   */
#define     DATAWXCTRL_HITC_S      16
#define           DATAWXHITC_NEXT  0xFF     /* Next 'hit' will trigger */
#define           DATAWXHITC_HIT1  0x00     /* No 'hits' after trigger */
#define     DATAWXCTRL_MMASK_BITS 0x0000FFF8    /* Mask ADDR_MATCH bits */
#define     DATAWXCTRL_MMASK_S    3
#define     DATAWXCTRL_MATLTX_BITS 0x00000003   /* Match threadn LOCAL addr */
#define     DATAWXCTRL_MATLTX_S    0            /* Match threadn LOCAL addr */
#define DATAW0DMATCH0       0x0480FF50 /* Write match data */
#define DATAW0DMATCH1       0x0480FF58
#define DATAW0DMASK0        0x0480FF60 /* Write match data mask */
#define DATAW0DMASK1        0x0480FF68
#define DATAWnXXXX_STRIDE      0x00000040  /* Stride between DATAW reg sets */
#define DATAWnXXXX_STRIDE_S    6
#define DATAWnXXXX_LIMIT       1           /* Sets 0,1 */

/*
 * CHIP Automatic Mips Allocation control registers
 * ------------------------------------------------
 */

/* CORE memory mapped AMA registers */
#define T0AMAREG4   0x04800810
#define     TXAMAREG4_POOLSIZE_BITS 0x3FFFFF00
#define     TXAMAREG4_POOLSIZE_S    8
#define     TXAMAREG4_AVALUE_BITS   0x000000FF
#define     TXAMAREG4_AVALUE_S  0
#define T0AMAREG5   0x04800818
#define     TXAMAREG5_POOLC_BITS    0x07FFFFFF
#define         TXAMAREG5_POOLC_S       0
#define T0AMAREG6   0x04800820
#define     TXAMAREG6_DLINEDEF_BITS 0x00FFFFF0
#define         TXAMAREG6_DLINEDEF_S    0
#define TnAMAREGX_STRIDE    0x00001000

/*
 * Memory Management Control Unit Table Entries
 * --------------------------------------------
 */
#define MMCU_ENTRY_S         4            /* -> Entry size                */
#define MMCU_ENTRY_ADDR_BITS 0xFFFFF000   /* Physical address             */
#define MMCU_ENTRY_ADDR_S    12           /* -> Page size                 */
#define MMCU_ENTRY_CWIN_BITS 0x000000C0   /* Caching 'window' selection   */
#define MMCU_ENTRY_CWIN_S    6
#define     MMCU_CWIN_UNCACHED  0 /* May not be memory etc.  */
#define     MMCU_CWIN_BURST     1 /* Cached but LRU unset */
#define     MMCU_CWIN_C1SET     2 /* Cached in 1 set only */
#define     MMCU_CWIN_CACHED    3 /* Fully cached            */
#define MMCU_ENTRY_CACHE_BIT 0x00000080   /* Set for cached region         */
#define     MMCU_ECACHE1_FULL_BIT  0x00000040 /* Use all the sets */
#define     MMCU_ECACHE0_BURST_BIT 0x00000040 /* Match bursts     */
#define MMCU_ENTRY_SYS_BIT   0x00000010   /* Sys-coherent access required  */
#define MMCU_ENTRY_WRC_BIT   0x00000008   /* Write combining allowed       */
#define MMCU_ENTRY_PRIV_BIT  0x00000004   /* Privilege required            */
#define MMCU_ENTRY_WR_BIT    0x00000002   /* Writes allowed                */
#define MMCU_ENTRY_VAL_BIT   0x00000001   /* Entry is valid                */

#ifdef METAC_2_1
/*
 * Extended first-level/top table entries have extra/larger fields in later
 * cores as bits 11:0 previously had no effect in such table entries.
 */
#define MMCU_E1ENT_ADDR_BITS 0xFFFFFFC0   /* Physical address             */
#define MMCU_E1ENT_ADDR_S    6            /*   -> resolution < page size  */
#define MMCU_E1ENT_PGSZ_BITS 0x0000001E   /* Page size for 2nd level      */
#define MMCU_E1ENT_PGSZ_S    1
#define     MMCU_E1ENT_PGSZ0_POWER   12   /* PgSz  0 -> 4K */
#define     MMCU_E1ENT_PGSZ_MAX      10   /* PgSz 10 -> 4M maximum */
#define MMCU_E1ENT_MINIM_BIT 0x00000020
#endif /* METAC_2_1 */

/* MMCU control register in SYSC region */
#define MMCU_TABLE_PHYS_ADDR        0x04830010
#define     MMCU_TABLE_PHYS_ADDR_BITS   0xFFFFFFFC
#ifdef METAC_2_1
#define     MMCU_TABLE_PHYS_EXTEND      0x00000001     /* See below */
#endif
#define MMCU_DCACHE_CTRL_ADDR       0x04830018
#define     MMCU_xCACHE_CTRL_ENABLE_BIT     0x00000001
#define     MMCU_xCACHE_CTRL_PARTITION_BIT  0x00000000 /* See xCPART below */
#define MMCU_ICACHE_CTRL_ADDR       0x04830020

#ifdef METAC_2_1

/*
 * Allow direct access to physical memory used to implement MMU table.
 *
 * Each is based on a corresponding MMCU_TnLOCAL_TABLE_PHYSn or similar
 *    MMCU_TnGLOBAL_TABLE_PHYSn register pair (see next).
 */
#define LINSYSMEMT0L_BASE   0x05000000
#define LINSYSMEMT0L_LIMIT  0x051FFFFF
#define     LINSYSMEMTnX_STRIDE     0x00200000  /*  2MB Local per thread */
#define     LINSYSMEMTnX_STRIDE_S   21
#define     LINSYSMEMTXG_OFFSET     0x00800000  /* +2MB Global per thread */
#define     LINSYSMEMTXG_OFFSET_S   23
#define LINSYSMEMT1L_BASE   0x05200000
#define LINSYSMEMT1L_LIMIT  0x053FFFFF
#define LINSYSMEMT2L_BASE   0x05400000
#define LINSYSMEMT2L_LIMIT  0x055FFFFF
#define LINSYSMEMT3L_BASE   0x05600000
#define LINSYSMEMT3L_LIMIT  0x057FFFFF
#define LINSYSMEMT0G_BASE   0x05800000
#define LINSYSMEMT0G_LIMIT  0x059FFFFF
#define LINSYSMEMT1G_BASE   0x05A00000
#define LINSYSMEMT1G_LIMIT  0x05BFFFFF
#define LINSYSMEMT2G_BASE   0x05C00000
#define LINSYSMEMT2G_LIMIT  0x05DFFFFF
#define LINSYSMEMT3G_BASE   0x05E00000
#define LINSYSMEMT3G_LIMIT  0x05FFFFFF

/*
 * Extended MMU table functionality allows a sparse or flat table to be
 * described much more efficiently than before.
 */
#define MMCU_T0LOCAL_TABLE_PHYS0    0x04830700
#define   MMCU_TnX_TABLE_PHYSX_STRIDE    0x20   /* Offset per thread */
#define   MMCU_TnX_TABLE_PHYSX_STRIDE_S  5
#define   MMCU_TXG_TABLE_PHYSX_OFFSET    0x10   /* Global versus local */
#define   MMCU_TXG_TABLE_PHYSX_OFFSET_S  4
#define     MMCU_TBLPHYS0_DCCTRL_BITS       0x000000DF  /* DC controls  */
#define     MMCU_TBLPHYS0_ENTLB_BIT         0x00000020  /* Cache in TLB */
#define     MMCU_TBLPHYS0_TBLSZ_BITS        0x00000F00  /* Area supported */
#define     MMCU_TBLPHYS0_TBLSZ_S           8
#define         MMCU_TBLPHYS0_TBLSZ0_POWER      22  /* 0 -> 4M */
#define         MMCU_TBLPHYS0_TBLSZ_MAX         9   /* 9 -> 2G */
#define     MMCU_TBLPHYS0_LINBASE_BITS      0xFFC00000  /* Linear base */
#define     MMCU_TBLPHYS0_LINBASE_S         22

#define MMCU_T0LOCAL_TABLE_PHYS1    0x04830708
#define     MMCU_TBLPHYS1_ADDR_BITS         0xFFFFFFFC  /* Physical base */
#define     MMCU_TBLPHYS1_ADDR_S            2

#define MMCU_T0GLOBAL_TABLE_PHYS0   0x04830710
#define MMCU_T0GLOBAL_TABLE_PHYS1   0x04830718
#define MMCU_T1LOCAL_TABLE_PHYS0    0x04830720
#define MMCU_T1LOCAL_TABLE_PHYS1    0x04830728
#define MMCU_T1GLOBAL_TABLE_PHYS0   0x04830730
#define MMCU_T1GLOBAL_TABLE_PHYS1   0x04830738
#define MMCU_T2LOCAL_TABLE_PHYS0    0x04830740
#define MMCU_T2LOCAL_TABLE_PHYS1    0x04830748
#define MMCU_T2GLOBAL_TABLE_PHYS0   0x04830750
#define MMCU_T2GLOBAL_TABLE_PHYS1   0x04830758
#define MMCU_T3LOCAL_TABLE_PHYS0    0x04830760
#define MMCU_T3LOCAL_TABLE_PHYS1    0x04830768
#define MMCU_T3GLOBAL_TABLE_PHYS0   0x04830770
#define MMCU_T3GLOBAL_TABLE_PHYS1   0x04830778

#define MMCU_T0EBWCCTRL             0x04830640
#define     MMCU_TnEBWCCTRL_BITS    0x00000007
#define     MMCU_TnEBWCCTRL_S       0
#define         MMCU_TnEBWCCCTRL_DISABLE_ALL 0
#define         MMCU_TnEBWCCCTRL_ABIT25      1
#define         MMCU_TnEBWCCCTRL_ABIT26      2
#define         MMCU_TnEBWCCCTRL_ABIT27      3
#define         MMCU_TnEBWCCCTRL_ABIT28      4
#define         MMCU_TnEBWCCCTRL_ABIT29      5
#define         MMCU_TnEBWCCCTRL_ABIT30      6
#define         MMCU_TnEBWCCCTRL_ENABLE_ALL  7
#define MMCU_TnEBWCCTRL_STRIDE      8

#endif /* METAC_2_1 */


/* Registers within the SYSC register region */
#define METAC_ID                0x04830000
#define     METAC_ID_MAJOR_BITS     0xFF000000
#define     METAC_ID_MAJOR_S        24
#define     METAC_ID_MINOR_BITS     0x00FF0000
#define     METAC_ID_MINOR_S        16
#define     METAC_ID_REV_BITS       0x0000FF00
#define     METAC_ID_REV_S          8
#define     METAC_ID_MAINT_BITS     0x000000FF
#define     METAC_ID_MAINT_S        0

#ifdef METAC_2_1
/* Use of this section is strongly deprecated */
#define METAC_ID2               0x04830008
#define     METAC_ID2_DESIGNER_BITS 0xFFFF0000  /* Modified by customer */
#define     METAC_ID2_DESIGNER_S    16
#define     METAC_ID2_MINOR2_BITS   0x00000F00  /* 3rd digit of prod rev */
#define     METAC_ID2_MINOR2_S      8
#define     METAC_ID2_CONFIG_BITS   0x000000FF  /* Wrapper configuration */
#define     METAC_ID2_CONFIG_S      0

/* Primary core identification and configuration information */
#define METAC_CORE_ID           0x04831000
#define     METAC_COREID_GROUP_BITS   0xFF000000
#define     METAC_COREID_GROUP_S      24
#define         METAC_COREID_GROUP_METAG  0x14
#define     METAC_COREID_ID_BITS      0x00FF0000
#define     METAC_COREID_ID_S         16
#define         METAC_COREID_ID_W32       0x10   /* >= for 32-bit pipeline */
#define     METAC_COREID_CONFIG_BITS  0x0000FFFF
#define     METAC_COREID_CONFIG_S     0
#define       METAC_COREID_CFGCACHE_BITS    0x0007
#define       METAC_COREID_CFGCACHE_S       0
#define           METAC_COREID_CFGCACHE_NOM       0
#define           METAC_COREID_CFGCACHE_TYPE0     1
#define           METAC_COREID_CFGCACHE_NOMMU     1 /* Alias for TYPE0 */
#define           METAC_COREID_CFGCACHE_NOCACHE   2
#define           METAC_COREID_CFGCACHE_PRIVNOMMU 3
#define       METAC_COREID_CFGDSP_BITS      0x0038
#define       METAC_COREID_CFGDSP_S         3
#define           METAC_COREID_CFGDSP_NOM       0
#define           METAC_COREID_CFGDSP_MIN       1
#define       METAC_COREID_NOFPACC_BIT      0x0040 /* Set if no FPU accum */
#define       METAC_COREID_CFGFPU_BITS      0x0180
#define       METAC_COREID_CFGFPU_S         7
#define           METAC_COREID_CFGFPU_NOM       0
#define           METAC_COREID_CFGFPU_SNGL      1
#define           METAC_COREID_CFGFPU_DBL       2
#define       METAC_COREID_NOAMA_BIT        0x0200 /* Set if no AMA present */
#define       METAC_COREID_NOCOH_BIT        0x0400 /* Set if no Gbl coherency */

/* Core revision information */
#define METAC_CORE_REV          0x04831008
#define     METAC_COREREV_DESIGN_BITS   0xFF000000
#define     METAC_COREREV_DESIGN_S      24
#define     METAC_COREREV_MAJOR_BITS    0x00FF0000
#define     METAC_COREREV_MAJOR_S       16
#define     METAC_COREREV_MINOR_BITS    0x0000FF00
#define     METAC_COREREV_MINOR_S       8
#define     METAC_COREREV_MAINT_BITS    0x000000FF
#define     METAC_COREREV_MAINT_S       0

/* Configuration information control outside the core */
#define METAC_CORE_DESIGNER1    0x04831010      /* Arbitrary value */
#define METAC_CORE_DESIGNER2    0x04831018      /* Arbitrary value */

/* Configuration information covering presence/number of various features */
#define METAC_CORE_CONFIG2      0x04831020
#define     METAC_CORECFG2_COREDBGTYPE_BITS 0x60000000   /* Core debug type */
#define     METAC_CORECFG2_COREDBGTYPE_S    29
#define     METAC_CORECFG2_DCSMALL_BIT      0x04000000   /* Data cache small */
#define     METAC_CORECFG2_ICSMALL_BIT      0x02000000   /* Inst cache small */
#define     METAC_CORECFG2_DCSZNP_BITS      0x01C00000   /* Data cache size np */
#define     METAC_CORECFG2_DCSZNP_S         22
#define     METAC_CORECFG2_ICSZNP_BITS      0x00380000  /* Inst cache size np */
#define     METAC_CORECFG2_ICSZNP_S         19
#define     METAC_CORECFG2_DCSZ_BITS        0x00070000   /* Data cache size */
#define     METAC_CORECFG2_DCSZ_S           16
#define         METAC_CORECFG2_xCSZ_4K          0        /* Allocated values */
#define         METAC_CORECFG2_xCSZ_8K          1
#define         METAC_CORECFG2_xCSZ_16K         2
#define         METAC_CORECFG2_xCSZ_32K         3
#define         METAC_CORECFG2_xCSZ_64K         4
#define     METAC_CORE_C2ICSZ_BITS          0x0000E000   /* Inst cache size */
#define     METAC_CORE_C2ICSZ_S             13
#define     METAC_CORE_GBLACC_BITS          0x00001800   /* Number of Global Acc */
#define     METAC_CORE_GBLACC_S             11
#define     METAC_CORE_GBLDXR_BITS          0x00000700   /* 0 -> 0, R -> 2^(R-1) */
#define     METAC_CORE_GBLDXR_S             8
#define     METAC_CORE_GBLAXR_BITS          0x000000E0   /* 0 -> 0, R -> 2^(R-1) */
#define     METAC_CORE_GBLAXR_S             5
#define     METAC_CORE_RTTRACE_BIT          0x00000010
#define     METAC_CORE_WATCHN_BITS          0x0000000C   /* 0 -> 0, N -> 2^N */
#define     METAC_CORE_WATCHN_S             2
#define     METAC_CORE_BREAKN_BITS          0x00000003   /* 0 -> 0, N -> 2^N */
#define     METAC_CORE_BREAKN_S             0

/* Configuration information covering presence/number of various features */
#define METAC_CORE_CONFIG3      0x04831028
#define     METAC_CORECFG3_L2C_REV_ID_BITS          0x000F0000   /* Revision of L2 cache */
#define     METAC_CORECFG3_L2C_REV_ID_S             16
#define     METAC_CORECFG3_L2C_LINE_SIZE_BITS       0x00003000   /* L2 line size */
#define     METAC_CORECFG3_L2C_LINE_SIZE_S          12
#define         METAC_CORECFG3_L2C_LINE_SIZE_64B    0x0          /* 64 bytes */
#define     METAC_CORECFG3_L2C_NUM_WAYS_BITS        0x00000F00   /* L2 number of ways (2^n) */
#define     METAC_CORECFG3_L2C_NUM_WAYS_S           8
#define     METAC_CORECFG3_L2C_SIZE_BITS            0x000000F0   /* L2 size (2^n) */
#define     METAC_CORECFG3_L2C_SIZE_S               4
#define     METAC_CORECFG3_L2C_UNIFIED_BIT          0x00000004   /* Unified cache: */
#define     METAC_CORECFG3_L2C_UNIFIED_S            2
#define       METAC_CORECFG3_L2C_UNIFIED_UNIFIED    1            /* - Unified D/I cache */
#define       METAC_CORECFG3_L2C_UNIFIED_SEPARATE   0            /* - Separate D/I cache */
#define     METAC_CORECFG3_L2C_MODE_BIT             0x00000002   /* Cache Mode: */
#define     METAC_CORECFG3_L2C_MODE_S               1
#define       METAC_CORECFG3_L2C_MODE_WRITE_BACK    1            /* - Write back */
#define       METAC_CORECFG3_L2C_MODE_WRITE_THROUGH 0            /* - Write through */
#define     METAC_CORECFG3_L2C_HAVE_L2C_BIT         0x00000001   /* Have L2C */
#define     METAC_CORECFG3_L2C_HAVE_L2C_S           0

#endif /* METAC_2_1 */

#define SYSC_CACHE_MMU_CONFIG       0x04830028
#ifdef METAC_2_1
#define     SYSC_CMMUCFG_DCSKEWABLE_BIT 0x00000040
#define     SYSC_CMMUCFG_ICSKEWABLE_BIT 0x00000020
#define     SYSC_CMMUCFG_DCSKEWOFF_BIT  0x00000010  /* Skew association override  */
#define     SYSC_CMMUCFG_ICSKEWOFF_BIT  0x00000008  /* -> default 0 on if present */
#define     SYSC_CMMUCFG_MODE_BITS      0x00000007  /* Access to old state */
#define     SYSC_CMMUCFG_MODE_S         0
#define         SYSC_CMMUCFG_ON             0x7
#define         SYSC_CMMUCFG_EBYPASS        0x6   /* Enhanced by-pass mode */
#define         SYSC_CMMUCFG_EBYPASSIC      0x4   /* EB just inst cache */
#define         SYSC_CMMUCFG_EBYPASSDC      0x2   /* EB just data cache */
#endif /* METAC_2_1 */
/* Old definitions, Keep them for now */
#define         SYSC_CMMUCFG_MMU_ON_BIT     0x1
#define         SYSC_CMMUCFG_DC_ON_BIT      0x2
#define         SYSC_CMMUCFG_IC_ON_BIT      0x4

#define SYSC_JTAG_THREAD            0x04830030
#define     SYSC_JTAG_TX_BITS           0x00000003 /* Read only bits! */
#define     SYSC_JTAG_TX_S              0
#define     SYSC_JTAG_PRIV_BIT          0x00000004
#ifdef METAC_2_1
#define     SYSC_JTAG_SLAVETX_BITS      0x00000018
#define     SYSC_JTAG_SLAVETX_S         3
#endif /* METAC_2_1 */

#define SYSC_DCACHE_FLUSH           0x04830038
#define SYSC_ICACHE_FLUSH           0x04830040
#define  SYSC_xCACHE_FLUSH_INIT     0x1
#define MMCU_DIRECTMAP0_ADDR        0x04830080 /* LINSYSDIRECT_BASE -> */
#define     MMCU_DIRECTMAPn_STRIDE      0x00000010 /* 4 Region settings */
#define     MMCU_DIRECTMAPn_S           4
#define         MMCU_DIRECTMAPn_ADDR_BITS       0xFF800000
#define         MMCU_DIRECTMAPn_ADDR_S          23
#define         MMCU_DIRECTMAPn_ADDR_SCALE      0x00800000 /* 8M Regions */
#ifdef METAC_2_1
/*
 * These fields in the above registers provide MMCU_ENTRY_* values
 *   for each direct mapped region to enable optimisation of these areas.
 *       (LSB similar to VALID must be set for enhancments to be active)
 */
#define         MMCU_DIRECTMAPn_ENHANCE_BIT     0x00000001 /* 0 = no optim */
#define         MMCU_DIRECTMAPn_DCCTRL_BITS     0x000000DF /* Get DC Ctrl */
#define         MMCU_DIRECTMAPn_DCCTRL_S        0
#define         MMCU_DIRECTMAPn_ICCTRL_BITS     0x0000C000 /* Get IC Ctrl */
#define         MMCU_DIRECTMAPn_ICCTRL_S        8
#define         MMCU_DIRECTMAPn_ENTLB_BIT       0x00000020 /* Cache in TLB */
#define         MMCU_DIRECTMAPn_ICCWIN_BITS     0x0000C000 /* Get IC Win Bits */
#define         MMCU_DIRECTMAPn_ICCWIN_S        14
#endif /* METAC_2_1 */

#define MMCU_DIRECTMAP1_ADDR        0x04830090
#define MMCU_DIRECTMAP2_ADDR        0x048300a0
#define MMCU_DIRECTMAP3_ADDR        0x048300b0

/*
 * These bits partion each threads use of data cache or instruction cache
 * resource by modifying the top 4 bits of the address within the cache
 * storage area.
 */
#define SYSC_DCPART0 0x04830200
#define     SYSC_xCPARTn_STRIDE   0x00000008
#define     SYSC_xCPARTL_AND_BITS 0x0000000F /* Masks top 4 bits */
#define     SYSC_xCPARTL_AND_S    0
#define     SYSC_xCPARTG_AND_BITS 0x00000F00 /* Masks top 4 bits */
#define     SYSC_xCPARTG_AND_S    8
#define     SYSC_xCPARTL_OR_BITS  0x000F0000 /* Ors into top 4 bits */
#define     SYSC_xCPARTL_OR_S     16
#define     SYSC_xCPARTG_OR_BITS  0x0F000000 /* Ors into top 4 bits */
#define     SYSC_xCPARTG_OR_S     24
#define     SYSC_CWRMODE_BIT      0x80000000 /* Write cache mode bit */

#define SYSC_DCPART1 0x04830208
#define SYSC_DCPART2 0x04830210
#define SYSC_DCPART3 0x04830218
#define SYSC_ICPART0 0x04830220
#define SYSC_ICPART1 0x04830228
#define SYSC_ICPART2 0x04830230
#define SYSC_ICPART3 0x04830238

/*
 * META Core Memory and Cache Update registers
 */
#define SYSC_MCMDATAX  0x04830300   /* 32-bit read/write data register */
#define SYSC_MCMDATAT  0x04830308   /* Read or write data triggers oper */
#define SYSC_MCMGCTRL  0x04830310   /* Control register */
#define     SYSC_MCMGCTRL_READ_BIT  0x00000001 /* Set to issue 1st read */
#define     SYSC_MCMGCTRL_AINC_BIT  0x00000002 /* Set for auto-increment */
#define     SYSC_MCMGCTRL_ADDR_BITS 0x000FFFFC /* Address or index */
#define     SYSC_MCMGCTRL_ADDR_S    2
#define     SYSC_MCMGCTRL_ID_BITS   0x0FF00000 /* Internal memory block Id */
#define     SYSC_MCMGCTRL_ID_S      20
#define         SYSC_MCMGID_NODEV       0xFF /* No Device Selected */
#define         SYSC_MCMGID_DSPRAM0A    0x04 /* DSP RAM D0 block A access */
#define         SYSC_MCMGID_DSPRAM0B    0x05 /* DSP RAM D0 block B access */
#define         SYSC_MCMGID_DSPRAM1A    0x06 /* DSP RAM D1 block A access */
#define         SYSC_MCMGID_DSPRAM1B    0x07 /* DSP RAM D1 block B access */
#define         SYSC_MCMGID_DCACHEL     0x08 /* DCACHE lines (64-bytes/line) */
#ifdef METAC_2_1
#define         SYSC_MCMGID_DCACHETLB   0x09 /* DCACHE TLB ( Read Only )     */
#endif /* METAC_2_1 */
#define         SYSC_MCMGID_DCACHET     0x0A /* DCACHE tags (32-bits/line)   */
#define         SYSC_MCMGID_DCACHELRU   0x0B /* DCACHE LRU (8-bits/line)     */
#define         SYSC_MCMGID_ICACHEL     0x0C /* ICACHE lines (64-bytes/line  */
#ifdef METAC_2_1
#define         SYSC_MCMGID_ICACHETLB   0x0D /* ICACHE TLB (Read Only )     */
#endif /* METAC_2_1 */
#define         SYSC_MCMGID_ICACHET     0x0E /* ICACHE Tags (32-bits/line)   */
#define         SYSC_MCMGID_ICACHELRU   0x0F /* ICACHE LRU (8-bits/line )    */
#define         SYSC_MCMGID_COREIRAM0   0x10 /* Core code mem id 0 */
#define         SYSC_MCMGID_COREIRAMn   0x17
#define         SYSC_MCMGID_COREDRAM0   0x18 /* Core data mem id 0 */
#define         SYSC_MCMGID_COREDRAMn   0x1F
#ifdef METAC_2_1
#define         SYSC_MCMGID_DCACHEST    0x20 /* DCACHE ST ( Read Only )      */
#define         SYSC_MCMGID_ICACHEST    0x21 /* ICACHE ST ( Read Only )      */
#define         SYSC_MCMGID_DCACHETLBLRU 0x22 /* DCACHE TLB LRU ( Read Only )*/
#define         SYSC_MCMGID_ICACHETLBLRU 0x23 /* ICACHE TLB LRU( Read Only ) */
#define         SYSC_MCMGID_DCACHESTLRU 0x24 /* DCACHE ST LRU ( Read Only )  */
#define         SYSC_MCMGID_ICACHESTLRU 0x25 /* ICACHE ST LRU ( Read Only )  */
#define         SYSC_MCMGID_DEBUGTLB    0x26 /* DEBUG TLB ( Read Only )      */
#define         SYSC_MCMGID_DEBUGST     0x27 /* DEBUG ST ( Read Only )       */
#define         SYSC_MCMGID_L2CACHEL    0x30 /* L2 Cache Lines (64-bytes/line) */
#define         SYSC_MCMGID_L2CACHET    0x31 /* L2 Cache Tags (32-bits/line) */
#define         SYSC_MCMGID_COPROX0     0x70 /* Coprocessor port id 0 */
#define         SYSC_MCMGID_COPROXn     0x77
#endif /* METAC_2_1 */
#define     SYSC_MCMGCTRL_TR31_BIT  0x80000000 /* Trigger 31 on completion */
#define SYSC_MCMSTATUS 0x04830318   /* Status read only */
#define     SYSC_MCMSTATUS_IDLE_BIT 0x00000001

/* META System Events */
#define SYSC_SYS_EVENT            0x04830400
#define     SYSC_SYSEVT_ATOMIC_BIT      0x00000001
#define     SYSC_SYSEVT_CACHEX_BIT      0x00000002
#define SYSC_ATOMIC_LOCK          0x04830408
#define     SYSC_ATOMIC_STATE_TX_BITS 0x0000000F
#define     SYSC_ATOMIC_STATE_TX_S    0
#ifdef METAC_1_2
#define     SYSC_ATOMIC_STATE_DX_BITS 0x000000F0
#define     SYSC_ATOMIC_STATE_DX_S    4
#else /* METAC_1_2 */
#define     SYSC_ATOMIC_SOURCE_BIT    0x00000010
#endif /* !METAC_1_2 */


#ifdef METAC_2_1

/* These definitions replace the EXPAND_TIMER_DIV register defines which are to
 * be deprecated.
 */
#define SYSC_TIMER_DIV            0x04830140
#define     SYSC_TIMDIV_BITS      0x000000FF
#define     SYSC_TIMDIV_S         0

/* META Enhanced by-pass control for local and global region */
#define MMCU_LOCAL_EBCTRL   0x04830600
#define MMCU_GLOBAL_EBCTRL  0x04830608
#define     MMCU_EBCTRL_SINGLE_BIT      0x00000020 /* TLB Uncached */
/*
 * These fields in the above registers provide MMCU_ENTRY_* values
 *   for each direct mapped region to enable optimisation of these areas.
 */
#define     MMCU_EBCTRL_DCCTRL_BITS     0x000000C0 /* Get DC Ctrl */
#define     MMCU_EBCTRL_DCCTRL_S        0
#define     MMCU_EBCTRL_ICCTRL_BITS     0x0000C000 /* Get DC Ctrl */
#define     MMCU_EBCTRL_ICCTRL_S        8

/* META Cached Core Mode Registers */
#define MMCU_T0CCM_ICCTRL   0x04830680     /* Core cached code control */
#define     MMCU_TnCCM_xxCTRL_STRIDE    8
#define     MMCU_TnCCM_xxCTRL_STRIDE_S  3
#define MMCU_T1CCM_ICCTRL   0x04830688
#define MMCU_T2CCM_ICCTRL   0x04830690
#define MMCU_T3CCM_ICCTRL   0x04830698
#define MMCU_T0CCM_DCCTRL   0x048306C0     /* Core cached data control */
#define MMCU_T1CCM_DCCTRL   0x048306C8
#define MMCU_T2CCM_DCCTRL   0x048306D0
#define MMCU_T3CCM_DCCTRL   0x048306D8
#define     MMCU_TnCCM_ENABLE_BIT       0x00000001
#define     MMCU_TnCCM_WIN3_BIT         0x00000002
#define     MMCU_TnCCM_DCWRITE_BIT      0x00000004  /* In DCCTRL only */
#define     MMCU_TnCCM_REGSZ_BITS       0x00000F00
#define     MMCU_TnCCM_REGSZ_S          8
#define         MMCU_TnCCM_REGSZ0_POWER      12     /* RegSz 0 -> 4K */
#define         MMCU_TnCCM_REGSZ_MAXBYTES    0x00080000  /* 512K max */
#define     MMCU_TnCCM_ADDR_BITS        0xFFFFF000
#define     MMCU_TnCCM_ADDR_S           12

#endif /* METAC_2_1 */

/*
 * Hardware performance counter registers
 * --------------------------------------
 */
#ifdef METAC_2_1
/* Two Performance Counter Internal Core Events Control registers */
#define PERF_ICORE0   0x0480FFD0
#define PERF_ICORE1   0x0480FFD8
#define     PERFI_CTRL_BITS    0x0000000F
#define     PERFI_CTRL_S       0
#define         PERFI_CAH_DMISS    0x0  /* Dcache Misses in cache (TLB Hit) */
#define         PERFI_CAH_IMISS    0x1  /* Icache Misses in cache (TLB Hit) */
#define         PERFI_TLB_DMISS    0x2  /* Dcache Misses in per-thread TLB */
#define         PERFI_TLB_IMISS    0x3  /* Icache Misses in per-thread TLB */
#define         PERFI_TLB_DWRHITS  0x4  /* DC Write-Hits in per-thread TLB */
#define         PERFI_TLB_DWRMISS  0x5  /* DC Write-Miss in per-thread TLB */
#define         PERFI_CAH_DLFETCH  0x8  /* DC Read cache line fetch */
#define         PERFI_CAH_ILFETCH  0x9  /* DC Read cache line fetch */
#define         PERFI_CAH_DWFETCH  0xA  /* DC Read cache word fetch */
#define         PERFI_CAH_IWFETCH  0xB  /* DC Read cache word fetch */
#endif /* METAC_2_1 */

/* Two memory-mapped hardware performance counter registers */
#define PERF_COUNT0 0x0480FFE0
#define PERF_COUNT1 0x0480FFE8

/* Fields in PERF_COUNTn registers */
#define PERF_COUNT_BITS  0x00ffffff /* Event count value */

#define PERF_THREAD_BITS 0x0f000000 /* Thread mask selects threads */
#define PERF_THREAD_S    24

#define PERF_CTRL_BITS   0xf0000000 /* Event filter control */
#define PERF_CTRL_S      28

#define    PERFCTRL_SUPER   0  /* Superthread cycles */
#define    PERFCTRL_REWIND  1  /* Rewinds due to Dcache Misses */
#ifdef METAC_2_1
#define    PERFCTRL_SUPREW  2  /* Rewinds of superthreaded cycles (no mask) */

#define    PERFCTRL_CYCLES  3  /* Counts all cycles (no mask) */

#define    PERFCTRL_PREDBC  4  /* Conditional branch predictions */
#define    PERFCTRL_MISPBC  5  /* Conditional branch mispredictions */
#define    PERFCTRL_PREDRT  6  /* Return predictions */
#define    PERFCTRL_MISPRT  7  /* Return mispredictions */
#endif /* METAC_2_1 */

#define    PERFCTRL_DHITS   8  /* Dcache Hits */
#define    PERFCTRL_IHITS   9  /* Icache Hits */
#define    PERFCTRL_IMISS   10 /* Icache Misses in cache or TLB */
#ifdef METAC_2_1
#define    PERFCTRL_DCSTALL 11 /* Dcache+TLB o/p delayed (per-thread) */
#define    PERFCTRL_ICSTALL 12 /* Icache+TLB o/p delayed (per-thread) */

#define    PERFCTRL_INT     13 /* Internal core delailed events (see next) */
#define    PERFCTRL_EXT     15 /* External source in core periphery */
#endif /* METAC_2_1 */

#ifdef METAC_2_1
/* These definitions replace the EXPAND_PERFCHANx register defines which are to
 * be deprecated.
 */
#define PERF_CHAN0 0x04830150
#define PERF_CHAN1 0x04830158
#define     PERF_CHAN_BITS    0x0000000F
#define     PERF_CHAN_S       0
#define         PERFCHAN_WRC_WRBURST   0x0   /* Write combiner write burst */
#define         PERFCHAN_WRC_WRITE     0x1   /* Write combiner write       */
#define         PERFCHAN_WRC_RDBURST   0x2   /* Write combiner read burst  */
#define         PERFCHAN_WRC_READ      0x3   /* Write combiner read        */
#define         PERFCHAN_PREARB_DELAY  0x4   /* Pre-arbiter delay cycle    */
					     /* Cross-bar hold-off cycle:  */
#define         PERFCHAN_XBAR_HOLDWRAP 0x5   /*    wrapper register        */
#define         PERFCHAN_XBAR_HOLDSBUS 0x6   /*    system bus (ATP only)   */
#define         PERFCHAN_XBAR_HOLDCREG 0x9   /*    core registers          */
#define         PERFCHAN_L2C_MISS      0x6   /* L2 Cache miss              */
#define         PERFCHAN_L2C_HIT       0x7   /* L2 Cache hit               */
#define         PERFCHAN_L2C_WRITEBACK 0x8   /* L2 Cache writeback         */
					     /* Admission delay cycle:     */
#define         PERFCHAN_INPUT_CREG    0xB   /*    core registers          */
#define         PERFCHAN_INPUT_INTR    0xC   /*    internal ram            */
#define         PERFCHAN_INPUT_WRC     0xD   /*    write combiners(memory) */

/* Should following be removed as not in TRM anywhere? */
#define         PERFCHAN_XBAR_HOLDINTR 0x8   /*    internal ram            */
#define         PERFCHAN_INPUT_SBUS    0xA   /*    register port           */
/* End of remove section. */

#define         PERFCHAN_MAINARB_DELAY 0xF   /* Main arbiter delay cycle   */

#endif /* METAC_2_1 */

#ifdef METAC_2_1
/*
 * Write combiner registers
 * ------------------------
 *
 * These replace the EXPAND_T0WRCOMBINE register defines, which will be
 * deprecated.
 */
#define WRCOMB_CONFIG0             0x04830100
#define     WRCOMB_LFFEn_BIT           0x00004000  /* Enable auto line full flush */
#define     WRCOMB_ENABLE_BIT          0x00002000  /* Enable write combiner */
#define     WRCOMB_TIMEOUT_ENABLE_BIT  0x00001000  /* Timeout flush enable */
#define     WRCOMB_TIMEOUT_COUNT_BITS  0x000003FF
#define     WRCOMB_TIMEOUT_COUNT_S     0
#define WRCOMB_CONFIG4             0x04830180
#define     WRCOMB_PARTALLOC_BITS      0x000000C0
#define     WRCOMB_PARTALLOC_S         64
#define     WRCOMB_PARTSIZE_BITS       0x00000030
#define     WRCOMB_PARTSIZE_S          4
#define     WRCOMB_PARTOFFSET_BITS     0x0000000F
#define     WRCOMB_PARTOFFSET_S        0
#define WRCOMB_CONFIG_STRIDE       8
#endif /* METAC_2_1 */

#ifdef METAC_2_1
/*
 * Thread arbiter registers
 * ------------------------
 *
 * These replace the EXPAND_T0ARBITER register defines, which will be
 * deprecated.
 */
#define ARBITER_ARBCONFIG0       0x04830120
#define     ARBCFG_BPRIORITY_BIT     0x02000000
#define     ARBCFG_IPRIORITY_BIT     0x01000000
#define     ARBCFG_PAGE_BITS         0x00FF0000
#define     ARBCFG_PAGE_S            16
#define     ARBCFG_BBASE_BITS        0x0000FF00
#define     ARGCFG_BBASE_S           8
#define     ARBCFG_IBASE_BITS        0x000000FF
#define     ARBCFG_IBASE_S           0
#define ARBITER_TTECONFIG0       0x04820160
#define     ARBTTE_IUPPER_BITS       0xFF000000
#define     ARBTTE_IUPPER_S          24
#define     ARBTTE_ILOWER_BITS       0x00FF0000
#define     ARBTTE_ILOWER_S          16
#define     ARBTTE_BUPPER_BITS       0x0000FF00
#define     ARBTTE_BUPPER_S          8
#define     ARBTTE_BLOWER_BITS       0x000000FF
#define     ARBTTE_BLOWER_S          0
#define ARBITER_STRIDE           8
#endif /* METAC_2_1 */

/*
 * Expansion area registers
 * --------------------------------------
 */

/* These defines are to be deprecated. See above instead. */
#define EXPAND_T0WRCOMBINE         0x03000000
#ifdef METAC_2_1
#define     EXPWRC_LFFEn_BIT           0x00004000  /* Enable auto line full flush */
#endif /* METAC_2_1 */
#define     EXPWRC_ENABLE_BIT          0x00002000  /* Enable write combiner */
#define     EXPWRC_TIMEOUT_ENABLE_BIT  0x00001000  /* Timeout flush enable */
#define     EXPWRC_TIMEOUT_COUNT_BITS  0x000003FF
#define     EXPWRC_TIMEOUT_COUNT_S     0
#define EXPAND_TnWRCOMBINE_STRIDE  0x00000008

/* These defines are to be deprecated. See above instead. */
#define EXPAND_T0ARBITER         0x03000020
#define     EXPARB_BPRIORITY_BIT 0x02000000
#define     EXPARB_IPRIORITY_BIT 0x01000000
#define     EXPARB_PAGE_BITS     0x00FF0000
#define     EXPARB_PAGE_S        16
#define     EXPARB_BBASE_BITS    0x0000FF00
#define     EXPARB_BBASE_S       8
#define     EXPARB_IBASE_BITS    0x000000FF
#define     EXPARB_IBASE_S       0
#define EXPAND_TnARBITER_STRIDE  0x00000008

/* These definitions are to be deprecated. See above instead. */
#define EXPAND_TIMER_DIV   0x03000040
#define     EXPTIM_DIV_BITS      0x000000FF
#define     EXPTIM_DIV_S         0

/* These definitions are to be deprecated. See above instead. */
#define EXPAND_PERFCHAN0   0x03000050
#define EXPAND_PERFCHAN1   0x03000058
#define     EXPPERF_CTRL_BITS    0x0000000F
#define     EXPPERF_CTRL_S       0
#define         EXPPERF_WRC_WRBURST   0x0   /* Write combiner write burst */
#define         EXPPERF_WRC_WRITE     0x1   /* Write combiner write       */
#define         EXPPERF_WRC_RDBURST   0x2   /* Write combiner read burst  */
#define         EXPPERF_WRC_READ      0x3   /* Write combiner read        */
#define         EXPPERF_PREARB_DELAY  0x4   /* Pre-arbiter delay cycle    */
					    /* Cross-bar hold-off cycle:  */
#define         EXPPERF_XBAR_HOLDWRAP 0x5   /*    wrapper register        */
#define         EXPPERF_XBAR_HOLDSBUS 0x6   /*    system bus              */
#ifdef METAC_1_2
#define         EXPPERF_XBAR_HOLDLBUS 0x7   /*    local bus               */
#else /* METAC_1_2 */
#define         EXPPERF_XBAR_HOLDINTR 0x8   /*    internal ram            */
#define         EXPPERF_XBAR_HOLDCREG 0x9   /*    core registers          */
					    /* Admission delay cycle:     */
#define         EXPPERF_INPUT_SBUS    0xA   /*    register port           */
#define         EXPPERF_INPUT_CREG    0xB   /*    core registers          */
#define         EXPPERF_INPUT_INTR    0xC   /*    internal ram            */
#define         EXPPERF_INPUT_WRC     0xD   /*    write combiners(memory) */
#endif /* !METAC_1_2 */
#define         EXPPERF_MAINARB_DELAY 0xF   /* Main arbiter delay cycle   */

/*
 * Debug port registers
 * --------------------------------------
 */

/* Data Exchange Register */
#define DBGPORT_MDBGDATAX                    0x0

/* Data Transfer register */
#define DBGPORT_MDBGDATAT                    0x4

/* Control Register 0 */
#define DBGPORT_MDBGCTRL0                    0x8
#define     DBGPORT_MDBGCTRL0_ADDR_BITS      0xFFFFFFFC
#define     DBGPORT_MDBGCTRL0_ADDR_S         2
#define     DBGPORT_MDBGCTRL0_AUTOINCR_BIT   0x00000002
#define     DBGPORT_MDBGCTRL0_RD_BIT         0x00000001

/* Control Register 1 */
#define DBGPORT_MDBGCTRL1                    0xC
#ifdef METAC_2_1
#define    DBGPORT_MDBGCTRL1_DEFERRTHREAD_BITS      0xC0000000
#define    DBGPORT_MDBGCTRL1_DEFERRTHREAD_S         30
#endif /* METAC_2_1 */
#define     DBGPORT_MDBGCTRL1_LOCK2_INTERLOCK_BIT   0x20000000
#define     DBGPORT_MDBGCTRL1_ATOMIC_INTERLOCK_BIT  0x10000000
#define     DBGPORT_MDBGCTRL1_TRIGSTATUS_BIT        0x08000000
#define     DBGPORT_MDBGCTRL1_GBLPORT_IDLE_BIT      0x04000000
#define     DBGPORT_MDBGCTRL1_COREMEM_IDLE_BIT      0x02000000
#define     DBGPORT_MDBGCTRL1_READY_BIT             0x01000000
#ifdef METAC_2_1
#define     DBGPORT_MDBGCTRL1_DEFERRID_BITS         0x00E00000
#define     DBGPORT_MDBGCTRL1_DEFERRID_S            21
#define     DBGPORT_MDBGCTRL1_DEFERR_BIT            0x00100000
#endif /* METAC_2_1 */
#define     DBGPORT_MDBGCTRL1_WR_ACTIVE_BIT         0x00040000
#define     DBGPORT_MDBGCTRL1_COND_LOCK2_BIT        0x00020000
#define     DBGPORT_MDBGCTRL1_LOCK2_BIT             0x00010000
#define     DBGPORT_MDBGCTRL1_DIAGNOSE_BIT          0x00008000
#define     DBGPORT_MDBGCTRL1_FORCEDIAG_BIT         0x00004000
#define     DBGPORT_MDBGCTRL1_MEMFAULT_BITS         0x00003000
#define     DBGPORT_MDBGCTRL1_MEMFAULT_S            12
#define     DBGPORT_MDBGCTRL1_TRIGGER_BIT           0x00000100
#ifdef METAC_2_1
#define     DBGPORT_MDBGCTRL1_INTSPECIAL_BIT        0x00000080
#define     DBGPORT_MDBGCTRL1_INTRUSIVE_BIT         0x00000040
#endif /* METAC_2_1 */
#define     DBGPORT_MDBGCTRL1_THREAD_BITS           0x00000030 /* Thread mask selects threads */
#define     DBGPORT_MDBGCTRL1_THREAD_S              4
#define     DBGPORT_MDBGCTRL1_TRANS_SIZE_BITS       0x0000000C
#define     DBGPORT_MDBGCTRL1_TRANS_SIZE_S          2
#define         DBGPORT_MDBGCTRL1_TRANS_SIZE_32_BIT 0x00000000
#define         DBGPORT_MDBGCTRL1_TRANS_SIZE_16_BIT 0x00000004
#define         DBGPORT_MDBGCTRL1_TRANS_SIZE_8_BIT  0x00000008
#define     DBGPORT_MDBGCTRL1_BYTE_ROUND_BITS       0x00000003
#define     DBGPORT_MDBGCTRL1_BYTE_ROUND_S          0
#define         DBGPORT_MDBGCTRL1_BYTE_ROUND_8_BIT  0x00000001
#define         DBGPORT_MDBGCTRL1_BYTE_ROUND_16_BIT 0x00000002


/* L2 Cache registers */
#define SYSC_L2C_INIT              0x048300C0
#define SYSC_L2C_INIT_INIT                  1
#define SYSC_L2C_INIT_IN_PROGRESS           0
#define SYSC_L2C_INIT_COMPLETE              1

#define SYSC_L2C_ENABLE            0x048300D0
#define SYSC_L2C_ENABLE_ENABLE_BIT     0x00000001
#define SYSC_L2C_ENABLE_PFENABLE_BIT   0x00000002

#define SYSC_L2C_PURGE             0x048300C8
#define SYSC_L2C_PURGE_PURGE                1
#define SYSC_L2C_PURGE_IN_PROGRESS          0
#define SYSC_L2C_PURGE_COMPLETE             1

#endif /* _ASM_METAG_MEM_H_ */
