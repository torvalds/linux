/*
 * Xtensa processor core configuration information.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999-2006 Tensilica Inc.
 */

#ifndef _XTENSA_CORE_H
#define _XTENSA_CORE_H


/****************************************************************************
	    Parameters Useful for Any Code, USER or PRIVILEGED
 ****************************************************************************/

/*
 *  Note:  Macros of the form XCHAL_HAVE_*** have a value of 1 if the option is
 *  configured, and a value of 0 otherwise.  These macros are always defined.
 */


/*----------------------------------------------------------------------
				ISA
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_BE			1	/* big-endian byte ordering */
#define XCHAL_HAVE_WINDOWED		1	/* windowed registers option */
#define XCHAL_NUM_AREGS			64	/* num of physical addr regs */
#define XCHAL_NUM_AREGS_LOG2		6	/* log2(XCHAL_NUM_AREGS) */
#define XCHAL_MAX_INSTRUCTION_SIZE	3	/* max instr bytes (3..8) */
#define XCHAL_HAVE_DEBUG		1	/* debug option */
#define XCHAL_HAVE_DENSITY		1	/* 16-bit instructions */
#define XCHAL_HAVE_LOOPS		1	/* zero-overhead loops */
#define XCHAL_HAVE_NSA			1	/* NSA/NSAU instructions */
#define XCHAL_HAVE_MINMAX		0	/* MIN/MAX instructions */
#define XCHAL_HAVE_SEXT			0	/* SEXT instruction */
#define XCHAL_HAVE_CLAMPS		0	/* CLAMPS instruction */
#define XCHAL_HAVE_MUL16		0	/* MUL16S/MUL16U instructions */
#define XCHAL_HAVE_MUL32		0	/* MULL instruction */
#define XCHAL_HAVE_MUL32_HIGH		0	/* MULUH/MULSH instructions */
#define XCHAL_HAVE_L32R			1	/* L32R instruction */
#define XCHAL_HAVE_ABSOLUTE_LITERALS	1	/* non-PC-rel (extended) L32R */
#define XCHAL_HAVE_CONST16		0	/* CONST16 instruction */
#define XCHAL_HAVE_ADDX			1	/* ADDX#/SUBX# instructions */
#define XCHAL_HAVE_WIDE_BRANCHES	0	/* B*.W18 or B*.W15 instr's */
#define XCHAL_HAVE_PREDICTED_BRANCHES	0	/* B[EQ/EQZ/NE/NEZ]T instr's */
#define XCHAL_HAVE_CALL4AND12		1	/* (obsolete option) */
#define XCHAL_HAVE_ABS			1	/* ABS instruction */
/*#define XCHAL_HAVE_POPC		0*/	/* POPC instruction */
/*#define XCHAL_HAVE_CRC		0*/	/* CRC instruction */
#define XCHAL_HAVE_RELEASE_SYNC		0	/* L32AI/S32RI instructions */
#define XCHAL_HAVE_S32C1I		0	/* S32C1I instruction */
#define XCHAL_HAVE_SPECULATION		0	/* speculation */
#define XCHAL_HAVE_FULL_RESET		1	/* all regs/state reset */
#define XCHAL_NUM_CONTEXTS		1	/* */
#define XCHAL_NUM_MISC_REGS		2	/* num of scratch regs (0..4) */
#define XCHAL_HAVE_TAP_MASTER		0	/* JTAG TAP control instr's */
#define XCHAL_HAVE_PRID			1	/* processor ID register */
#define XCHAL_HAVE_THREADPTR		1	/* THREADPTR register */
#define XCHAL_HAVE_BOOLEANS		0	/* boolean registers */
#define XCHAL_HAVE_CP			0	/* CPENABLE reg (coprocessor) */
#define XCHAL_CP_MAXCFG			0	/* max allowed cp id plus one */
#define XCHAL_HAVE_MAC16		0	/* MAC16 package */
#define XCHAL_HAVE_VECTORFPU2005	0	/* vector floating-point pkg */
#define XCHAL_HAVE_FP			0	/* floating point pkg */
#define XCHAL_HAVE_VECTRA1		0	/* Vectra I  pkg */
#define XCHAL_HAVE_VECTRALX		0	/* Vectra LX pkg */
#define XCHAL_HAVE_HIFI2		0	/* HiFi2 Audio Engine pkg */


/*----------------------------------------------------------------------
				MISC
  ----------------------------------------------------------------------*/

#define XCHAL_NUM_WRITEBUFFER_ENTRIES	4	/* size of write buffer */
#define XCHAL_INST_FETCH_WIDTH		4	/* instr-fetch width in bytes */
#define XCHAL_DATA_WIDTH		4	/* data width in bytes */
/*  In T1050, applies to selected core load and store instructions (see ISA): */
#define XCHAL_UNALIGNED_LOAD_EXCEPTION	1	/* unaligned loads cause exc. */
#define XCHAL_UNALIGNED_STORE_EXCEPTION	1	/* unaligned stores cause exc.*/

#define XCHAL_CORE_ID			"fsf"	/* alphanum core name
						   (CoreID) set in the Xtensa
						   Processor Generator */

#define XCHAL_BUILD_UNIQUE_ID		0x00006700	/* 22-bit sw build ID */

/*
 *  These definitions describe the hardware targeted by this software.
 */
#define XCHAL_HW_CONFIGID0		0xC103C3FF	/* ConfigID hi 32 bits*/
#define XCHAL_HW_CONFIGID1		0x0C006700	/* ConfigID lo 32 bits*/
#define XCHAL_HW_VERSION_NAME		"LX2.0.0"	/* full version name */
#define XCHAL_HW_VERSION_MAJOR		2200	/* major ver# of targeted hw */
#define XCHAL_HW_VERSION_MINOR		0	/* minor ver# of targeted hw */
#define XTHAL_HW_REL_LX2		1
#define XTHAL_HW_REL_LX2_0		1
#define XTHAL_HW_REL_LX2_0_0		1
#define XCHAL_HW_CONFIGID_RELIABLE	1
/*  If software targets a *range* of hardware versions, these are the bounds: */
#define XCHAL_HW_MIN_VERSION_MAJOR	2200	/* major v of earliest tgt hw */
#define XCHAL_HW_MIN_VERSION_MINOR	0	/* minor v of earliest tgt hw */
#define XCHAL_HW_MAX_VERSION_MAJOR	2200	/* major v of latest tgt hw */
#define XCHAL_HW_MAX_VERSION_MINOR	0	/* minor v of latest tgt hw */


/*----------------------------------------------------------------------
				CACHE
  ----------------------------------------------------------------------*/

#define XCHAL_ICACHE_LINESIZE		16	/* I-cache line size in bytes */
#define XCHAL_DCACHE_LINESIZE		16	/* D-cache line size in bytes */
#define XCHAL_ICACHE_LINEWIDTH		4	/* log2(I line size in bytes) */
#define XCHAL_DCACHE_LINEWIDTH		4	/* log2(D line size in bytes) */

#define XCHAL_ICACHE_SIZE		8192	/* I-cache size in bytes or 0 */
#define XCHAL_DCACHE_SIZE		8192	/* D-cache size in bytes or 0 */

#define XCHAL_DCACHE_IS_WRITEBACK	0	/* writeback feature */




/****************************************************************************
    Parameters Useful for PRIVILEGED (Supervisory or Non-Virtualized) Code
 ****************************************************************************/


#ifndef XTENSA_HAL_NON_PRIVILEGED_ONLY

/*----------------------------------------------------------------------
				CACHE
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_PIF			1	/* any outbound PIF present */

/*  If present, cache size in bytes == (ways * 2^(linewidth + setwidth)).  */

/*  Number of cache sets in log2(lines per way):  */
#define XCHAL_ICACHE_SETWIDTH		8
#define XCHAL_DCACHE_SETWIDTH		8

/*  Cache set associativity (number of ways):  */
#define XCHAL_ICACHE_WAYS		2
#define XCHAL_DCACHE_WAYS		2

/*  Cache features:  */
#define XCHAL_ICACHE_LINE_LOCKABLE	0
#define XCHAL_DCACHE_LINE_LOCKABLE	0
#define XCHAL_ICACHE_ECC_PARITY		0
#define XCHAL_DCACHE_ECC_PARITY		0

/*  Number of encoded cache attr bits (see <xtensa/hal.h> for decoded bits):  */
#define XCHAL_CA_BITS			4


/*----------------------------------------------------------------------
			INTERNAL I/D RAM/ROMs and XLMI
  ----------------------------------------------------------------------*/

#define XCHAL_NUM_INSTROM		0	/* number of core instr. ROMs */
#define XCHAL_NUM_INSTRAM		0	/* number of core instr. RAMs */
#define XCHAL_NUM_DATAROM		0	/* number of core data ROMs */
#define XCHAL_NUM_DATARAM		0	/* number of core data RAMs */
#define XCHAL_NUM_URAM			0	/* number of core unified RAMs*/
#define XCHAL_NUM_XLMI			0	/* number of core XLMI ports */


/*----------------------------------------------------------------------
			INTERRUPTS and TIMERS
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_INTERRUPTS		1	/* interrupt option */
#define XCHAL_HAVE_HIGHPRI_INTERRUPTS	1	/* med/high-pri. interrupts */
#define XCHAL_HAVE_NMI			0	/* non-maskable interrupt */
#define XCHAL_HAVE_CCOUNT		1	/* CCOUNT reg. (timer option) */
#define XCHAL_NUM_TIMERS		3	/* number of CCOMPAREn regs */
#define XCHAL_NUM_INTERRUPTS		17	/* number of interrupts */
#define XCHAL_NUM_INTERRUPTS_LOG2	5	/* ceil(log2(NUM_INTERRUPTS)) */
#define XCHAL_NUM_EXTINTERRUPTS		10	/* num of external interrupts */
#define XCHAL_NUM_INTLEVELS		4	/* number of interrupt levels
						   (not including level zero) */
#define XCHAL_EXCM_LEVEL		1	/* level masked by PS.EXCM */
	/* (always 1 in XEA1; levels 2 .. EXCM_LEVEL are "medium priority") */

/*  Masks of interrupts at each interrupt level:  */
#define XCHAL_INTLEVEL1_MASK		0x000064F9
#define XCHAL_INTLEVEL2_MASK		0x00008902
#define XCHAL_INTLEVEL3_MASK		0x00011204
#define XCHAL_INTLEVEL4_MASK		0x00000000
#define XCHAL_INTLEVEL5_MASK		0x00000000
#define XCHAL_INTLEVEL6_MASK		0x00000000
#define XCHAL_INTLEVEL7_MASK		0x00000000

/*  Masks of interrupts at each range 1..n of interrupt levels:  */
#define XCHAL_INTLEVEL1_ANDBELOW_MASK	0x000064F9
#define XCHAL_INTLEVEL2_ANDBELOW_MASK	0x0000EDFB
#define XCHAL_INTLEVEL3_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL4_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL5_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL6_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL7_ANDBELOW_MASK	0x0001FFFF

/*  Level of each interrupt:  */
#define XCHAL_INT0_LEVEL		1
#define XCHAL_INT1_LEVEL		2
#define XCHAL_INT2_LEVEL		3
#define XCHAL_INT3_LEVEL		1
#define XCHAL_INT4_LEVEL		1
#define XCHAL_INT5_LEVEL		1
#define XCHAL_INT6_LEVEL		1
#define XCHAL_INT7_LEVEL		1
#define XCHAL_INT8_LEVEL		2
#define XCHAL_INT9_LEVEL		3
#define XCHAL_INT10_LEVEL		1
#define XCHAL_INT11_LEVEL		2
#define XCHAL_INT12_LEVEL		3
#define XCHAL_INT13_LEVEL		1
#define XCHAL_INT14_LEVEL		1
#define XCHAL_INT15_LEVEL		2
#define XCHAL_INT16_LEVEL		3
#define XCHAL_DEBUGLEVEL		4	/* debug interrupt level */
#define XCHAL_HAVE_DEBUG_EXTERN_INT	0	/* OCD external db interrupt */

/*  Type of each interrupt:  */
#define XCHAL_INT0_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT1_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT2_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT3_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT4_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT5_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT6_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT7_TYPE 	XTHAL_INTTYPE_EXTERN_EDGE
#define XCHAL_INT8_TYPE 	XTHAL_INTTYPE_EXTERN_EDGE
#define XCHAL_INT9_TYPE 	XTHAL_INTTYPE_EXTERN_EDGE
#define XCHAL_INT10_TYPE 	XTHAL_INTTYPE_TIMER
#define XCHAL_INT11_TYPE 	XTHAL_INTTYPE_TIMER
#define XCHAL_INT12_TYPE 	XTHAL_INTTYPE_TIMER
#define XCHAL_INT13_TYPE 	XTHAL_INTTYPE_SOFTWARE
#define XCHAL_INT14_TYPE 	XTHAL_INTTYPE_SOFTWARE
#define XCHAL_INT15_TYPE 	XTHAL_INTTYPE_SOFTWARE
#define XCHAL_INT16_TYPE 	XTHAL_INTTYPE_SOFTWARE

/*  Masks of interrupts for each type of interrupt:  */
#define XCHAL_INTTYPE_MASK_UNCONFIGURED	0xFFFE0000
#define XCHAL_INTTYPE_MASK_SOFTWARE	0x0001E000
#define XCHAL_INTTYPE_MASK_EXTERN_EDGE	0x00000380
#define XCHAL_INTTYPE_MASK_EXTERN_LEVEL	0x0000007F
#define XCHAL_INTTYPE_MASK_TIMER	0x00001C00
#define XCHAL_INTTYPE_MASK_NMI		0x00000000
#define XCHAL_INTTYPE_MASK_WRITE_ERROR	0x00000000

/*  Interrupt numbers assigned to specific interrupt sources:  */
#define XCHAL_TIMER0_INTERRUPT		10	/* CCOMPARE0 */
#define XCHAL_TIMER1_INTERRUPT		11	/* CCOMPARE1 */
#define XCHAL_TIMER2_INTERRUPT		12	/* CCOMPARE2 */
#define XCHAL_TIMER3_INTERRUPT		XTHAL_TIMER_UNCONFIGURED

/*  Interrupt numbers for levels at which only one interrupt is configured:  */
/*  (There are many interrupts each at level(s) 1, 2, 3.)  */


/*
 *  External interrupt vectors/levels.
 *  These macros describe how Xtensa processor interrupt numbers
 *  (as numbered internally, eg. in INTERRUPT and INTENABLE registers)
 *  map to external BInterrupt<n> pins, for those interrupts
 *  configured as external (level-triggered, edge-triggered, or NMI).
 *  See the Xtensa processor databook for more details.
 */

/*  Core interrupt numbers mapped to each EXTERNAL interrupt number:  */
#define XCHAL_EXTINT0_NUM		0	/* (intlevel 1) */
#define XCHAL_EXTINT1_NUM		1	/* (intlevel 2) */
#define XCHAL_EXTINT2_NUM		2	/* (intlevel 3) */
#define XCHAL_EXTINT3_NUM		3	/* (intlevel 1) */
#define XCHAL_EXTINT4_NUM		4	/* (intlevel 1) */
#define XCHAL_EXTINT5_NUM		5	/* (intlevel 1) */
#define XCHAL_EXTINT6_NUM		6	/* (intlevel 1) */
#define XCHAL_EXTINT7_NUM		7	/* (intlevel 1) */
#define XCHAL_EXTINT8_NUM		8	/* (intlevel 2) */
#define XCHAL_EXTINT9_NUM		9	/* (intlevel 3) */


/*----------------------------------------------------------------------
			EXCEPTIONS and VECTORS
  ----------------------------------------------------------------------*/

#define XCHAL_XEA_VERSION		2	/* Xtensa Exception Architecture
						   number: 1 == XEA1 (old)
							   2 == XEA2 (new)
							   0 == XEAX (extern) */
#define XCHAL_HAVE_XEA1			0	/* Exception Architecture 1 */
#define XCHAL_HAVE_XEA2			1	/* Exception Architecture 2 */
#define XCHAL_HAVE_XEAX			0	/* External Exception Arch. */
#define XCHAL_HAVE_EXCEPTIONS		1	/* exception option */
#define XCHAL_HAVE_MEM_ECC_PARITY	0	/* local memory ECC/parity */

#define XCHAL_RESET_VECTOR_VADDR	0xFE000020
#define XCHAL_RESET_VECTOR_PADDR	0xFE000020
#define XCHAL_USER_VECTOR_VADDR		0xD0000220
#define XCHAL_USER_VECTOR_PADDR		0x00000220
#define XCHAL_KERNEL_VECTOR_VADDR	0xD0000200
#define XCHAL_KERNEL_VECTOR_PADDR	0x00000200
#define XCHAL_DOUBLEEXC_VECTOR_VADDR	0xD0000290
#define XCHAL_DOUBLEEXC_VECTOR_PADDR	0x00000290
#define XCHAL_WINDOW_VECTORS_VADDR	0xD0000000
#define XCHAL_WINDOW_VECTORS_PADDR	0x00000000
#define XCHAL_INTLEVEL2_VECTOR_VADDR	0xD0000240
#define XCHAL_INTLEVEL2_VECTOR_PADDR	0x00000240
#define XCHAL_INTLEVEL3_VECTOR_VADDR	0xD0000250
#define XCHAL_INTLEVEL3_VECTOR_PADDR	0x00000250
#define XCHAL_INTLEVEL4_VECTOR_VADDR	0xFE000520
#define XCHAL_INTLEVEL4_VECTOR_PADDR	0xFE000520
#define XCHAL_DEBUG_VECTOR_VADDR	XCHAL_INTLEVEL4_VECTOR_VADDR
#define XCHAL_DEBUG_VECTOR_PADDR	XCHAL_INTLEVEL4_VECTOR_PADDR


/*----------------------------------------------------------------------
				DEBUG
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_OCD			1	/* OnChipDebug option */
#define XCHAL_NUM_IBREAK		2	/* number of IBREAKn regs */
#define XCHAL_NUM_DBREAK		2	/* number of DBREAKn regs */
#define XCHAL_HAVE_OCD_DIR_ARRAY	1	/* faster OCD option */


/*----------------------------------------------------------------------
				MMU
  ----------------------------------------------------------------------*/

/*  See <xtensa/config/core-matmap.h> header file for more details.  */

#define XCHAL_HAVE_TLBS			1	/* inverse of HAVE_CACHEATTR */
#define XCHAL_HAVE_SPANNING_WAY		0	/* one way maps I+D 4GB vaddr */
#define XCHAL_HAVE_IDENTITY_MAP		0	/* vaddr == paddr always */
#define XCHAL_HAVE_CACHEATTR		0	/* CACHEATTR register present */
#define XCHAL_HAVE_MIMIC_CACHEATTR	0	/* region protection */
#define XCHAL_HAVE_XLT_CACHEATTR	0	/* region prot. w/translation */
#define XCHAL_HAVE_PTP_MMU		1	/* full MMU (with page table
						   [autorefill] and protection)
						   usable for an MMU-based OS */
/*  If none of the above last 4 are set, it's a custom TLB configuration.  */
#define XCHAL_ITLB_ARF_ENTRIES_LOG2	2	/* log2(autorefill way size) */
#define XCHAL_DTLB_ARF_ENTRIES_LOG2	2	/* log2(autorefill way size) */

#define XCHAL_MMU_ASID_BITS		8	/* number of bits in ASIDs */
#define XCHAL_MMU_RINGS			4	/* number of rings (1..4) */
#define XCHAL_MMU_RING_BITS		2	/* num of bits in RING field */

#endif /* !XTENSA_HAL_NON_PRIVILEGED_ONLY */


#endif /* _XTENSA_CORE_CONFIGURATION_H */

