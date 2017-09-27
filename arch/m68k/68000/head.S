/*
 * head.S - Common startup code for 68000 core based CPU's
 *
 * 2012.10.21, Luis Alves <ljalvs@gmail.com>, Single head.S file for all
 *             68000 core based CPU's. Based on the sources from:
 *             Coldfire by Greg Ungerer <gerg@snapgear.com>
 *             68328 by D. Jeff Dionne <jeff@ryeham.ee.ryerson.ca>,
 *                      Kenneth Albanowski <kjahds@kjahds.com>,
 *                      The Silver Hammer Group, Ltd.
 *
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>


/*****************************************************************************
 * UCSIMM and UCDIMM use CONFIG_MEMORY_RESERVE to reserve some RAM
 *****************************************************************************/
#ifdef CONFIG_MEMORY_RESERVE
#define RAMEND (CONFIG_RAMBASE+CONFIG_RAMSIZE)-(CONFIG_MEMORY_RESERVE*0x100000)
#else
#define RAMEND (CONFIG_RAMBASE+CONFIG_RAMSIZE)
#endif
/*****************************************************************************/

.global _start
.global _rambase
.global _ramvec
.global _ramstart
.global _ramend

#if defined(CONFIG_PILOT) || defined(CONFIG_INIT_LCD)
.global bootlogo_bits
#endif

/* Defining DEBUG_HEAD_CODE, serial port in 68x328 is inited */
/* #define DEBUG_HEAD_CODE */
#undef DEBUG_HEAD_CODE

.data

/*****************************************************************************
 * RAM setup pointers. Used by the kernel to determine RAM location and size.
 *****************************************************************************/

_rambase:
	.long	0
_ramvec:
	.long	0
_ramstart:
	.long	0
_ramend:
	.long	0

__HEAD

/*****************************************************************************
 * Entry point, where all begins!
 *****************************************************************************/

_start:

/* Pilot need this specific signature at the start of ROM */
#ifdef CONFIG_PILOT
	.byte	0x4e, 0xfa, 0x00, 0x0a		/* bra opcode (jmp 10 bytes) */
	.byte	'b', 'o', 'o', 't'
	.word	10000
	nop
	moveq	#0, %d0
	movew	%d0, 0xfffff618			/* Watchdog off */
	movel	#0x00011f07, 0xfffff114		/* CS A1 Mask */
#endif /* CONFIG_PILOT */

	movew	#0x2700, %sr			/* disable all interrupts */

/*****************************************************************************
 * Setup PLL and wait for it to settle (in 68x328 cpu's).
 * Also, if enabled, init serial port.
 *****************************************************************************/
#if defined(CONFIG_M68328) || \
    defined(CONFIG_M68EZ328) || \
    defined(CONFIG_M68VZ328)

/* Serial port setup. Should only be needed if debugging this startup code. */
#ifdef DEBUG_HEAD_CODE
	movew	#0x0800, 0xfffff906		/* Ignore CTS */
	movew	#0x010b, 0xfffff902		/* BAUD to 9600 */
	movew	#0xe100, 0xfffff900		/* enable */
#endif /* DEBUG_HEAD */

#ifdef CONFIG_PILOT
	movew	#0x2410, 0xfffff200		/* PLLCR */
#else
	movew	#0x2400, 0xfffff200		/* PLLCR */
#endif
	movew	#0x0123, 0xfffff202		/* PLLFSR */
	moveq	#0, %d0
	movew	#16384, %d0			/* PLL settle wait loop */
_pll_settle:
	subw	#1, %d0
	bne	_pll_settle
#endif /* CONFIG_M68x328 */


/*****************************************************************************
 * If running kernel from ROM some specific initialization has to be done.
 * (Assuming that everything is already init'ed when running from RAM)
 *****************************************************************************/
#ifdef CONFIG_ROMKERNEL

/*****************************************************************************
 * Init chip registers (uCsimm specific)
 *****************************************************************************/
#ifdef CONFIG_UCSIMM
	moveb	#0x00, 0xfffffb0b	/* Watchdog off */
	moveb	#0x10, 0xfffff000	/* SCR */
	moveb	#0x00, 0xfffff40b	/* enable chip select */
	moveb	#0x00, 0xfffff423	/* enable /DWE */
	moveb	#0x08, 0xfffffd0d	/* disable hardmap */
	moveb	#0x07, 0xfffffd0e	/* level 7 interrupt clear */
	movew	#0x8600, 0xfffff100	/* FLASH at 0x10c00000 */
	movew	#0x018b, 0xfffff110	/* 2Meg, enable, 0ws */
	movew	#0x8f00, 0xfffffc00	/* DRAM configuration */
	movew	#0x9667, 0xfffffc02	/* DRAM control */
	movew	#0x0000, 0xfffff106	/* DRAM at 0x00000000 */
	movew	#0x068f, 0xfffff116	/* 8Meg, enable, 0ws */
	moveb	#0x40, 0xfffff300	/* IVR */
	movel	#0x007FFFFF, %d0	/* IMR */
	movel	%d0, 0xfffff304
	moveb	0xfffff42b, %d0
	andb	#0xe0, %d0
	moveb	%d0, 0xfffff42b
#endif

/*****************************************************************************
 * Init LCD controller.
 * (Assuming that LCD controller is already init'ed when running from RAM)
 *****************************************************************************/
#ifdef CONFIG_INIT_LCD
#ifdef CONFIG_PILOT
	moveb	#0, 0xfffffA27			/* LCKCON */
	movel	#_start, 0xfffffA00		/* LSSA */
	moveb	#0xa, 0xfffffA05		/* LVPW */
	movew	#0x9f, 0xFFFFFa08		/* LXMAX */
	movew	#0x9f, 0xFFFFFa0a		/* LYMAX */
	moveb	#9, 0xfffffa29			/* LBAR */
	moveb	#0, 0xfffffa25			/* LPXCD */
	moveb	#0x04, 0xFFFFFa20		/* LPICF */
	moveb	#0x58, 0xfffffA27		/* LCKCON */
	moveb	#0x85, 0xfffff429		/* PFDATA */
	moveb	#0xd8, 0xfffffA27		/* LCKCON */
	moveb	#0xc5, 0xfffff429		/* PFDATA */
	moveb	#0xd5, 0xfffff429		/* PFDATA */
	movel	#bootlogo_bits, 0xFFFFFA00	/* LSSA */
	moveb	#10, 0xFFFFFA05			/* LVPW */
	movew	#160, 0xFFFFFA08		/* LXMAX */
	movew	#160, 0xFFFFFA0A		/* LYMAX */
#else /* CONFIG_PILOT */
	movel	#bootlogo_bits, 0xfffffA00	/* LSSA */
	moveb	#0x28, 0xfffffA05		/* LVPW */
	movew	#0x280, 0xFFFFFa08		/* LXMAX */
	movew	#0x1df, 0xFFFFFa0a		/* LYMAX */
	moveb	#0, 0xfffffa29			/* LBAR */
	moveb	#0, 0xfffffa25			/* LPXCD */
	moveb	#0x08, 0xFFFFFa20		/* LPICF */
	moveb	#0x01, 0xFFFFFA21		/* -ve pol */
	moveb	#0x81, 0xfffffA27		/* LCKCON */
	movew	#0xff00, 0xfffff412		/* LCD pins */
#endif /* CONFIG_PILOT */
#endif /* CONFIG_INIT_LCD */

/*****************************************************************************
 * Kernel is running from FLASH/ROM (XIP)
 * Copy init text & data to RAM
 *****************************************************************************/
	moveal	#_etext, %a0
	moveal	#_sdata, %a1
	moveal	#__bss_start, %a2
_copy_initmem:
	movel	%a0@+, %a1@+
	cmpal	%a1, %a2
	bhi	_copy_initmem
#endif /* CONFIG_ROMKERNEL */

/*****************************************************************************
 * Setup basic memory information for kernel
 *****************************************************************************/
	movel	#CONFIG_VECTORBASE,_ramvec	/* set vector base location */
	movel	#CONFIG_RAMBASE,_rambase	/* set the base of RAM */
	movel	#RAMEND, _ramend		/* set end ram addr */
	lea	__bss_stop,%a1
	movel	%a1,_ramstart

/*****************************************************************************
 * If the kernel is in RAM, move romfs to right above bss and
 * adjust _ramstart to where romfs ends.
 *
 * (Do this only if CONFIG_MTD_UCLINUX is true)
 *****************************************************************************/

#if defined(CONFIG_ROMFS_FS) && defined(CONFIG_RAMKERNEL) && \
    defined(CONFIG_MTD_UCLINUX)
	lea	__bss_start, %a0		/* get start of bss */
	lea	__bss_stop, %a1			/* set up destination  */
	movel	%a0, %a2			/* copy of bss start */

	movel	8(%a0), %d0			/* get size of ROMFS */
	addql	#8, %d0				/* allow for rounding */
	andl	#0xfffffffc, %d0		/* whole words */

	addl	%d0, %a0			/* copy from end */
	addl	%d0, %a1			/* copy from end */
	movel	%a1, _ramstart			/* set start of ram */
_copy_romfs:
	movel	-(%a0), -(%a1)			/* copy dword */
	cmpl	%a0, %a2			/* check if at end */
	bne	_copy_romfs
#endif /* CONFIG_ROMFS_FS && CONFIG_RAMKERNEL && CONFIG_MTD_UCLINUX */

/*****************************************************************************
 * Clear bss region
 *****************************************************************************/
	lea	__bss_start, %a0		/* get start of bss */
	lea	__bss_stop, %a1			/* get end of bss */
_clear_bss:
	movel	#0, (%a0)+			/* clear each word */
	cmpl	%a0, %a1			/* check if at end */
	bne	_clear_bss

/*****************************************************************************
 * Load the current task pointer and stack.
 *****************************************************************************/
	lea	init_thread_union,%a0
	lea	THREAD_SIZE(%a0),%sp
	jsr	start_kernel			/* start Linux kernel */
_exit:
	jmp	_exit				/* should never get here */
