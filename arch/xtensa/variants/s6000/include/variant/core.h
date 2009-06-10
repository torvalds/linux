/*
 * Xtensa processor core configuration information.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2008 Tensilica Inc.
 */

#ifndef _XTENSA_CORE_CONFIGURATION_H
#define _XTENSA_CORE_CONFIGURATION_H


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

#define XCHAL_HAVE_BE			0	/* big-endian byte ordering */
#define XCHAL_HAVE_WINDOWED		1	/* windowed registers option */
#define XCHAL_NUM_AREGS			64	/* num of physical addr regs */
#define XCHAL_NUM_AREGS_LOG2		6	/* log2(XCHAL_NUM_AREGS) */
#define XCHAL_MAX_INSTRUCTION_SIZE	8	/* max instr bytes (3..8) */
#define XCHAL_HAVE_DEBUG		1	/* debug option */
#define XCHAL_HAVE_DENSITY		1	/* 16-bit instructions */
#define XCHAL_HAVE_LOOPS		1	/* zero-overhead loops */
#define XCHAL_HAVE_NSA			1	/* NSA/NSAU instructions */
#define XCHAL_HAVE_MINMAX		1	/* MIN/MAX instructions */
#define XCHAL_HAVE_SEXT			1	/* SEXT instruction */
#define XCHAL_HAVE_CLAMPS		1	/* CLAMPS instruction */
#define XCHAL_HAVE_MUL16		1	/* MUL16S/MUL16U instructions */
#define XCHAL_HAVE_MUL32		1	/* MULL instruction */
#define XCHAL_HAVE_MUL32_HIGH		1	/* MULUH/MULSH instructions */
#define XCHAL_HAVE_DIV32		0	/* QUOS/QUOU/REMS/REMU instructions */
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
#define XCHAL_HAVE_FULL_RESET		0	/* all regs/state reset */
#define XCHAL_NUM_CONTEXTS		1	/* */
#define XCHAL_NUM_MISC_REGS		4	/* num of scratch regs (0..4) */
#define XCHAL_HAVE_TAP_MASTER		0	/* JTAG TAP control instr's */
#define XCHAL_HAVE_PRID			0	/* processor ID register */
#define XCHAL_HAVE_THREADPTR		0	/* THREADPTR register */
#define XCHAL_HAVE_BOOLEANS		1	/* boolean registers */
#define XCHAL_HAVE_CP			1	/* CPENABLE reg (coprocessor) */
#define XCHAL_CP_MAXCFG			8	/* max allowed cp id plus one */
#define XCHAL_HAVE_MAC16		0	/* MAC16 package */
#define XCHAL_HAVE_VECTORFPU2005	0	/* vector floating-point pkg */
#define XCHAL_HAVE_FP			1	/* floating point pkg */
#define XCHAL_HAVE_VECTRA1		0	/* Vectra I  pkg */
#define XCHAL_HAVE_VECTRALX		0	/* Vectra LX pkg */
#define XCHAL_HAVE_HIFI2		0	/* HiFi2 Audio Engine pkg */


/*----------------------------------------------------------------------
				MISC
  ----------------------------------------------------------------------*/

#define XCHAL_NUM_WRITEBUFFER_ENTRIES	8	/* size of write buffer */
#define XCHAL_INST_FETCH_WIDTH		8	/* instr-fetch width in bytes */
#define XCHAL_DATA_WIDTH		16	/* data width in bytes */
/*  In T1050, applies to selected core load and store instructions (see ISA): */
#define XCHAL_UNALIGNED_LOAD_EXCEPTION	1	/* unaligned loads cause exc. */
#define XCHAL_UNALIGNED_STORE_EXCEPTION	1	/* unaligned stores cause exc.*/

#define XCHAL_SW_VERSION		701001	/* sw version of this header */

#define XCHAL_CORE_ID			"stretch_bali"	/* alphanum core name
						   (CoreID) set in the Xtensa
						   Processor Generator */

#define XCHAL_BUILD_UNIQUE_ID		0x000104B9	/* 22-bit sw build ID */

/*
 *  These definitions describe the hardware targeted by this software.
 */
#define XCHAL_HW_CONFIGID0		0xC2F3F9FE	/* ConfigID hi 32 bits*/
#define XCHAL_HW_CONFIGID1		0x054104B9	/* ConfigID lo 32 bits*/
#define XCHAL_HW_VERSION_NAME		"LX1.0.2"	/* full version name */
#define XCHAL_HW_VERSION_MAJOR		2100	/* major ver# of targeted hw */
#define XCHAL_HW_VERSION_MINOR		2	/* minor ver# of targeted hw */
#define XCHAL_HW_VERSION		210002	/* major*100+minor */
#define XCHAL_HW_REL_LX1		1
#define XCHAL_HW_REL_LX1_0		1
#define XCHAL_HW_REL_LX1_0_2		1
#define XCHAL_HW_CONFIGID_RELIABLE	1
/*  If software targets a *range* of hardware versions, these are the bounds: */
#define XCHAL_HW_MIN_VERSION_MAJOR	2100	/* major v of earliest tgt hw */
#define XCHAL_HW_MIN_VERSION_MINOR	2	/* minor v of earliest tgt hw */
#define XCHAL_HW_MIN_VERSION		210002	/* earliest targeted hw */
#define XCHAL_HW_MAX_VERSION_MAJOR	2100	/* major v of latest tgt hw */
#define XCHAL_HW_MAX_VERSION_MINOR	2	/* minor v of latest tgt hw */
#define XCHAL_HW_MAX_VERSION		210002	/* latest targeted hw */


/*----------------------------------------------------------------------
				CACHE
  ----------------------------------------------------------------------*/

#define XCHAL_ICACHE_LINESIZE		16	/* I-cache line size in bytes */
#define XCHAL_DCACHE_LINESIZE		16	/* D-cache line size in bytes */
#define XCHAL_ICACHE_LINEWIDTH		4	/* log2(I line size in bytes) */
#define XCHAL_DCACHE_LINEWIDTH		4	/* log2(D line size in bytes) */

#define XCHAL_ICACHE_SIZE		32768	/* I-cache size in bytes or 0 */
#define XCHAL_DCACHE_SIZE		32768	/* D-cache size in bytes or 0 */

#define XCHAL_DCACHE_IS_WRITEBACK	1	/* writeback feature */




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
#define XCHAL_ICACHE_SETWIDTH		9
#define XCHAL_DCACHE_SETWIDTH		10

/*  Cache set associativity (number of ways):  */
#define XCHAL_ICACHE_WAYS		4
#define XCHAL_DCACHE_WAYS		2

/*  Cache features:  */
#define XCHAL_ICACHE_LINE_LOCKABLE	1
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
#define XCHAL_NUM_DATARAM		1	/* number of core data RAMs */
#define XCHAL_NUM_URAM			0	/* number of core unified RAMs*/
#define XCHAL_NUM_XLMI			1	/* number of core XLMI ports */

/*  Data RAM 0:  */
#define XCHAL_DATARAM0_VADDR		0x3FFF0000
#define XCHAL_DATARAM0_PADDR		0x3FFF0000
#define XCHAL_DATARAM0_SIZE		65536
#define XCHAL_DATARAM0_ECC_PARITY	0

/*  XLMI Port 0:  */
#define XCHAL_XLMI0_VADDR		0x37F80000
#define XCHAL_XLMI0_PADDR		0x37F80000
#define XCHAL_XLMI0_SIZE		262144
#define XCHAL_XLMI0_ECC_PARITY	0


/*----------------------------------------------------------------------
			INTERRUPTS and TIMERS
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_INTERRUPTS		1	/* interrupt option */
#define XCHAL_HAVE_HIGHPRI_INTERRUPTS	1	/* med/high-pri. interrupts */
#define XCHAL_HAVE_NMI			1	/* non-maskable interrupt */
#define XCHAL_HAVE_CCOUNT		1	/* CCOUNT reg. (timer option) */
#define XCHAL_NUM_TIMERS		3	/* number of CCOMPAREn regs */
#define XCHAL_NUM_INTERRUPTS		27	/* number of interrupts */
#define XCHAL_NUM_INTERRUPTS_LOG2	5	/* ceil(log2(NUM_INTERRUPTS)) */
#define XCHAL_NUM_EXTINTERRUPTS		20	/* num of external interrupts */
#define XCHAL_NUM_INTLEVELS		4	/* number of interrupt levels
						   (not including level zero) */
#define XCHAL_EXCM_LEVEL		1	/* level masked by PS.EXCM */
	/* (always 1 in XEA1; levels 2 .. EXCM_LEVEL are "medium priority") */

/*  Masks of interrupts at each interrupt level:  */
#define XCHAL_INTLEVEL1_MASK		0x01F07FFF
#define XCHAL_INTLEVEL2_MASK		0x02018000
#define XCHAL_INTLEVEL3_MASK		0x04060000
#define XCHAL_INTLEVEL4_MASK		0x00000000
#define XCHAL_INTLEVEL5_MASK		0x00080000
#define XCHAL_INTLEVEL6_MASK		0x00000000
#define XCHAL_INTLEVEL7_MASK		0x00000000

/*  Masks of interrupts at each range 1..n of interrupt levels:  */
#define XCHAL_INTLEVEL1_ANDBELOW_MASK	0x01F07FFF
#define XCHAL_INTLEVEL2_ANDBELOW_MASK	0x03F1FFFF
#define XCHAL_INTLEVEL3_ANDBELOW_MASK	0x07F7FFFF
#define XCHAL_INTLEVEL4_ANDBELOW_MASK	0x07F7FFFF
#define XCHAL_INTLEVEL5_ANDBELOW_MASK	0x07FFFFFF
#define XCHAL_INTLEVEL6_ANDBELOW_MASK	0x07FFFFFF
#define XCHAL_INTLEVEL7_ANDBELOW_MASK	0x07FFFFFF

/*  Level of each interrupt:  */
#define XCHAL_INT0_LEVEL		1
#define XCHAL_INT1_LEVEL		1
#define XCHAL_INT2_LEVEL		1
#define XCHAL_INT3_LEVEL		1
#define XCHAL_INT4_LEVEL		1
#define XCHAL_INT5_LEVEL		1
#define XCHAL_INT6_LEVEL		1
#define XCHAL_INT7_LEVEL		1
#define XCHAL_INT8_LEVEL		1
#define XCHAL_INT9_LEVEL		1
#define XCHAL_INT10_LEVEL		1
#define XCHAL_INT11_LEVEL		1
#define XCHAL_INT12_LEVEL		1
#define XCHAL_INT13_LEVEL		1
#define XCHAL_INT14_LEVEL		1
#define XCHAL_INT15_LEVEL		2
#define XCHAL_INT16_LEVEL		2
#define XCHAL_INT17_LEVEL		3
#define XCHAL_INT18_LEVEL		3
#define XCHAL_INT19_LEVEL		5
#define XCHAL_INT20_LEVEL		1
#define XCHAL_INT21_LEVEL		1
#define XCHAL_INT22_LEVEL		1
#define XCHAL_INT23_LEVEL		1
#define XCHAL_INT24_LEVEL		1
#define XCHAL_INT25_LEVEL		2
#define XCHAL_INT26_LEVEL		3
#define XCHAL_DEBUGLEVEL		4	/* debug interrupt level */
#define XCHAL_HAVE_DEBUG_EXTERN_INT	1	/* OCD external db interrupt */
#define XCHAL_NMILEVEL			5	/* NMI "level" (for use with
						   EXCSAVE/EPS/EPC_n, RFI n) */

/*  Type of each interrupt:  */
#define XCHAL_INT0_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT1_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT2_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT3_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT4_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT5_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT6_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT7_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT8_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT9_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT10_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT11_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT12_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT13_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT14_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT15_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT16_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT17_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT18_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT19_TYPE 	XTHAL_INTTYPE_NMI
#define XCHAL_INT20_TYPE 	XTHAL_INTTYPE_SOFTWARE
#define XCHAL_INT21_TYPE 	XTHAL_INTTYPE_SOFTWARE
#define XCHAL_INT22_TYPE 	XTHAL_INTTYPE_SOFTWARE
#define XCHAL_INT23_TYPE 	XTHAL_INTTYPE_SOFTWARE
#define XCHAL_INT24_TYPE 	XTHAL_INTTYPE_TIMER
#define XCHAL_INT25_TYPE 	XTHAL_INTTYPE_TIMER
#define XCHAL_INT26_TYPE 	XTHAL_INTTYPE_TIMER

/*  Masks of interrupts for each type of interrupt:  */
#define XCHAL_INTTYPE_MASK_UNCONFIGURED	0xF8000000
#define XCHAL_INTTYPE_MASK_SOFTWARE	0x00F00000
#define XCHAL_INTTYPE_MASK_EXTERN_EDGE	0x00000000
#define XCHAL_INTTYPE_MASK_EXTERN_LEVEL	0x0007FFFF
#define XCHAL_INTTYPE_MASK_TIMER	0x07000000
#define XCHAL_INTTYPE_MASK_NMI		0x00080000
#define XCHAL_INTTYPE_MASK_WRITE_ERROR	0x00000000

/*  Interrupt numbers assigned to specific interrupt sources:  */
#define XCHAL_TIMER0_INTERRUPT		24	/* CCOMPARE0 */
#define XCHAL_TIMER1_INTERRUPT		25	/* CCOMPARE1 */
#define XCHAL_TIMER2_INTERRUPT		26	/* CCOMPARE2 */
#define XCHAL_TIMER3_INTERRUPT		XTHAL_TIMER_UNCONFIGURED
#define XCHAL_NMI_INTERRUPT		19	/* non-maskable interrupt */

/*  Interrupt numbers for levels at which only one interrupt is configured:  */
#define XCHAL_INTLEVEL5_NUM		19
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
#define XCHAL_EXTINT1_NUM		1	/* (intlevel 1) */
#define XCHAL_EXTINT2_NUM		2	/* (intlevel 1) */
#define XCHAL_EXTINT3_NUM		3	/* (intlevel 1) */
#define XCHAL_EXTINT4_NUM		4	/* (intlevel 1) */
#define XCHAL_EXTINT5_NUM		5	/* (intlevel 1) */
#define XCHAL_EXTINT6_NUM		6	/* (intlevel 1) */
#define XCHAL_EXTINT7_NUM		7	/* (intlevel 1) */
#define XCHAL_EXTINT8_NUM		8	/* (intlevel 1) */
#define XCHAL_EXTINT9_NUM		9	/* (intlevel 1) */
#define XCHAL_EXTINT10_NUM		10	/* (intlevel 1) */
#define XCHAL_EXTINT11_NUM		11	/* (intlevel 1) */
#define XCHAL_EXTINT12_NUM		12	/* (intlevel 1) */
#define XCHAL_EXTINT13_NUM		13	/* (intlevel 1) */
#define XCHAL_EXTINT14_NUM		14	/* (intlevel 1) */
#define XCHAL_EXTINT15_NUM		15	/* (intlevel 2) */
#define XCHAL_EXTINT16_NUM		16	/* (intlevel 2) */
#define XCHAL_EXTINT17_NUM		17	/* (intlevel 3) */
#define XCHAL_EXTINT18_NUM		18	/* (intlevel 3) */
#define XCHAL_EXTINT19_NUM		19	/* (intlevel 5) */


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
#define XCHAL_HAVE_VECTOR_SELECT	0	/* relocatable vectors */
#define XCHAL_HAVE_VECBASE		0	/* relocatable vectors */

#define XCHAL_RESET_VECOFS		0x00000000
#define XCHAL_RESET_VECTOR_VADDR	0x3FFE03D0
#define XCHAL_RESET_VECTOR_PADDR	0x3FFE03D0
#define XCHAL_USER_VECOFS		0x00000000
#define XCHAL_USER_VECTOR_VADDR		0x40000220
#define XCHAL_USER_VECTOR_PADDR		0x40000220
#define XCHAL_KERNEL_VECOFS		0x00000000
#define XCHAL_KERNEL_VECTOR_VADDR	0x40000200
#define XCHAL_KERNEL_VECTOR_PADDR	0x40000200
#define XCHAL_DOUBLEEXC_VECOFS		0x00000000
#define XCHAL_DOUBLEEXC_VECTOR_VADDR	0x400002A0
#define XCHAL_DOUBLEEXC_VECTOR_PADDR	0x400002A0
#define XCHAL_WINDOW_OF4_VECOFS		0x00000000
#define XCHAL_WINDOW_UF4_VECOFS		0x00000040
#define XCHAL_WINDOW_OF8_VECOFS		0x00000080
#define XCHAL_WINDOW_UF8_VECOFS		0x000000C0
#define XCHAL_WINDOW_OF12_VECOFS	0x00000100
#define XCHAL_WINDOW_UF12_VECOFS	0x00000140
#define XCHAL_WINDOW_VECTORS_VADDR	0x40000000
#define XCHAL_WINDOW_VECTORS_PADDR	0x40000000
#define XCHAL_INTLEVEL2_VECOFS		0x00000000
#define XCHAL_INTLEVEL2_VECTOR_VADDR	0x40000240
#define XCHAL_INTLEVEL2_VECTOR_PADDR	0x40000240
#define XCHAL_INTLEVEL3_VECOFS		0x00000000
#define XCHAL_INTLEVEL3_VECTOR_VADDR	0x40000260
#define XCHAL_INTLEVEL3_VECTOR_PADDR	0x40000260
#define XCHAL_INTLEVEL4_VECOFS		0x00000000
#define XCHAL_INTLEVEL4_VECTOR_VADDR	0x40000390
#define XCHAL_INTLEVEL4_VECTOR_PADDR	0x40000390
#define XCHAL_DEBUG_VECOFS		XCHAL_INTLEVEL4_VECOFS
#define XCHAL_DEBUG_VECTOR_VADDR	XCHAL_INTLEVEL4_VECTOR_VADDR
#define XCHAL_DEBUG_VECTOR_PADDR	XCHAL_INTLEVEL4_VECTOR_PADDR
#define XCHAL_NMI_VECOFS		0x00000000
#define XCHAL_NMI_VECTOR_VADDR		0x400003B0
#define XCHAL_NMI_VECTOR_PADDR		0x400003B0
#define XCHAL_INTLEVEL5_VECOFS		XCHAL_NMI_VECOFS
#define XCHAL_INTLEVEL5_VECTOR_VADDR	XCHAL_NMI_VECTOR_VADDR
#define XCHAL_INTLEVEL5_VECTOR_PADDR	XCHAL_NMI_VECTOR_PADDR


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

/*  See core-matmap.h header file for more details.  */

#define XCHAL_HAVE_TLBS			1	/* inverse of HAVE_CACHEATTR */
#define XCHAL_HAVE_SPANNING_WAY		1	/* one way maps I+D 4GB vaddr */
#define XCHAL_HAVE_IDENTITY_MAP		1	/* vaddr == paddr always */
#define XCHAL_HAVE_CACHEATTR		0	/* CACHEATTR register present */
#define XCHAL_HAVE_MIMIC_CACHEATTR	1	/* region protection */
#define XCHAL_HAVE_XLT_CACHEATTR	0	/* region prot. w/translation */
#define XCHAL_HAVE_PTP_MMU		0	/* full MMU (with page table
						   [autorefill] and protection)
						   usable for an MMU-based OS */
/*  If none of the above last 4 are set, it's a custom TLB configuration.  */

#define XCHAL_MMU_ASID_BITS		0	/* number of bits in ASIDs */
#define XCHAL_MMU_RINGS			1	/* number of rings (1..4) */
#define XCHAL_MMU_RING_BITS		0	/* num of bits in RING field */

#endif /* !XTENSA_HAL_NON_PRIVILEGED_ONLY */


#endif /* _XTENSA_CORE_CONFIGURATION_H */

