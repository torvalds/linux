/* -*- mode: asm -*-
**
** head.S -- This file contains the initial boot code for the
**	     Linux/68k kernel.
**
** Copyright 1993 by Hamish Macdonald
**
** 68040 fixes by Michael Rausch
** 68060 fixes by Roman Hodek
** MMU cleanup by Randy Thelen
** Final MMU cleanup by Roman Zippel
**
** Atari support by Andreas Schwab, using ideas of Robert de Vries
** and Bjoern Brauel
** VME Support by Richard Hirst
**
** 94/11/14 Andreas Schwab: put kernel at PAGESIZE
** 94/11/18 Andreas Schwab: remove identity mapping of STRAM for Atari
** ++ Bjoern & Roman: ATARI-68040 support for the Medusa
** 95/11/18 Richard Hirst: Added MVME166 support
** 96/04/26 Guenther Kelleter: fixed identity mapping for Falcon with
**			      Magnum- and FX-alternate ram
** 98/04/25 Phil Blundell: added HP300 support
** 1998/08/30 David Kilzer: Added support for font_desc structures
**            for linux-2.1.115
** 9/02/11  Richard Zidlicky: added Q40 support (initial vesion 99/01/01)
** 2004/05/13 Kars de Jong: Finalised HP300 support
**
** This file is subject to the terms and conditions of the GNU General Public
** License. See the file README.legal in the main directory of this archive
** for more details.
**
*/

/*
 * Linux startup code.
 *
 * At this point, the boot loader has:
 * Disabled interrupts
 * Disabled caches
 * Put us in supervisor state.
 *
 * The kernel setup code takes the following steps:
 * .  Raise interrupt level
 * .  Set up initial kernel memory mapping.
 *    .  This sets up a mapping of the 4M of memory the kernel is located in.
 *    .  It also does a mapping of any initial machine specific areas.
 * .  Enable the MMU
 * .  Enable cache memories
 * .  Jump to kernel startup
 *
 * Much of the file restructuring was to accomplish:
 * 1) Remove register dependency through-out the file.
 * 2) Increase use of subroutines to perform functions
 * 3) Increase readability of the code
 *
 * Of course, readability is a subjective issue, so it will never be
 * argued that that goal was accomplished.  It was merely a goal.
 * A key way to help make code more readable is to give good
 * documentation.  So, the first thing you will find is exaustive
 * write-ups on the structure of the file, and the features of the
 * functional subroutines.
 *
 * General Structure:
 * ------------------
 *	Without a doubt the single largest chunk of head.S is spent
 * mapping the kernel and I/O physical space into the logical range
 * for the kernel.
 *	There are new subroutines and data structures to make MMU
 * support cleaner and easier to understand.
 *	First, you will find a routine call "mmu_map" which maps
 * a logical to a physical region for some length given a cache
 * type on behalf of the caller.  This routine makes writing the
 * actual per-machine specific code very simple.
 *	A central part of the code, but not a subroutine in itself,
 * is the mmu_init code which is broken down into mapping the kernel
 * (the same for all machines) and mapping machine-specific I/O
 * regions.
 *	Also, there will be a description of engaging the MMU and
 * caches.
 *	You will notice that there is a chunk of code which
 * can emit the entire MMU mapping of the machine.  This is present
 * only in debug modes and can be very helpful.
 *	Further, there is a new console driver in head.S that is
 * also only engaged in debug mode.  Currently, it's only supported
 * on the Macintosh class of machines.  However, it is hoped that
 * others will plug-in support for specific machines.
 *
 * ######################################################################
 *
 * mmu_map
 * -------
 *	mmu_map was written for two key reasons.  First, it was clear
 * that it was very difficult to read the previous code for mapping
 * regions of memory.  Second, the Macintosh required such extensive
 * memory allocations that it didn't make sense to propagate the
 * existing code any further.
 *	mmu_map requires some parameters:
 *
 *	mmu_map (logical, physical, length, cache_type)
 *
 *	While this essentially describes the function in the abstract, you'll
 * find more indepth description of other parameters at the implementation site.
 *
 * mmu_get_root_table_entry
 * ------------------------
 * mmu_get_ptr_table_entry
 * -----------------------
 * mmu_get_page_table_entry
 * ------------------------
 *
 *	These routines are used by other mmu routines to get a pointer into
 * a table, if necessary a new table is allocated. These routines are working
 * basically like pmd_alloc() and pte_alloc() in <asm/pgtable.h>. The root
 * table needs of course only to be allocated once in mmu_get_root_table_entry,
 * so that here also some mmu specific initialization is done. The second page
 * at the start of the kernel (the first page is unmapped later) is used for
 * the kernel_pg_dir. It must be at a position known at link time (as it's used
 * to initialize the init task struct) and since it needs special cache
 * settings, it's the easiest to use this page, the rest of the page is used
 * for further pointer tables.
 * mmu_get_page_table_entry allocates always a whole page for page tables, this
 * means 1024 pages and so 4MB of memory can be mapped. It doesn't make sense
 * to manage page tables in smaller pieces as nearly all mappings have that
 * size.
 *
 * ######################################################################
 *
 *
 * ######################################################################
 *
 * mmu_engage
 * ----------
 *	Thanks to a small helping routine enabling the mmu got quite simple
 * and there is only one way left. mmu_engage makes a complete a new mapping
 * that only includes the absolute necessary to be able to jump to the final
 * postion and to restore the original mapping.
 * As this code doesn't need a transparent translation register anymore this
 * means all registers are free to be used by machines that needs them for
 * other purposes.
 *
 * ######################################################################
 *
 * mmu_print
 * ---------
 *	This algorithm will print out the page tables of the system as
 * appropriate for an 030 or an 040.  This is useful for debugging purposes
 * and as such is enclosed in #ifdef MMU_PRINT/#endif clauses.
 *
 * ######################################################################
 *
 * console_init
 * ------------
 *	The console is also able to be turned off.  The console in head.S
 * is specifically for debugging and can be very useful.  It is surrounded by
 * #ifdef CONSOLE/#endif clauses so it doesn't have to ship in known-good
 * kernels.  It's basic algorithm is to determine the size of the screen
 * (in height/width and bit depth) and then use that information for
 * displaying an 8x8 font or an 8x16 (widthxheight).  I prefer the 8x8 for
 * debugging so I can see more good data.  But it was trivial to add support
 * for both fonts, so I included it.
 *	Also, the algorithm for plotting pixels is abstracted so that in
 * theory other platforms could add support for different kinds of frame
 * buffers.  This could be very useful.
 *
 * console_put_penguin
 * -------------------
 *	An important part of any Linux bring up is the penguin and there's
 * nothing like getting the Penguin on the screen!  This algorithm will work
 * on any machine for which there is a console_plot_pixel.
 *
 * console_scroll
 * --------------
 *	My hope is that the scroll algorithm does the right thing on the
 * various platforms, but it wouldn't be hard to add the test conditions
 * and new code if it doesn't.
 *
 * console_putc
 * -------------
 *
 * ######################################################################
 *
 *	Register usage has greatly simplified within head.S. Every subroutine
 * saves and restores all registers that it modifies (except it returns a
 * value in there of course). So the only register that needs to be initialized
 * is the stack pointer.
 * All other init code and data is now placed in the init section, so it will
 * be automatically freed at the end of the kernel initialization.
 *
 * ######################################################################
 *
 * options
 * -------
 *	There are many options available in a build of this file.  I've
 * taken the time to describe them here to save you the time of searching
 * for them and trying to understand what they mean.
 *
 * CONFIG_xxx:	These are the obvious machine configuration defines created
 * during configuration.  These are defined in include/linux/autoconf.h.
 *
 * CONSOLE:	There is support for head.S console in this file.  This
 * console can talk to a Mac frame buffer, but could easily be extrapolated
 * to extend it to support other platforms.
 *
 * TEST_MMU:	This is a test harness for running on any given machine but
 * getting an MMU dump for another class of machine.  The classes of machines
 * that can be tested are any of the makes (Atari, Amiga, Mac, VME, etc.)
 * and any of the models (030, 040, 060, etc.).
 *
 *	NOTE:	TEST_MMU is NOT permanent!  It is scheduled to be removed
 *		When head.S boots on Atari, Amiga, Macintosh, and VME
 *		machines.  At that point the underlying logic will be
 *		believed to be solid enough to be trusted, and TEST_MMU
 *		can be dropped.  Do note that that will clean up the
 *		head.S code significantly as large blocks of #if/#else
 *		clauses can be removed.
 *
 * MMU_NOCACHE_KERNEL:	On the Macintosh platform there was an inquiry into
 * determing why devices don't appear to work.  A test case was to remove
 * the cacheability of the kernel bits.
 *
 * MMU_PRINT:	There is a routine built into head.S that can display the
 * MMU data structures.  It outputs its result through the serial_putc
 * interface.  So where ever that winds up driving data, that's where the
 * mmu struct will appear.  On the Macintosh that's typically the console.
 *
 * SERIAL_DEBUG:	There are a series of putc() macro statements
 * scattered through out the code to give progress of status to the
 * person sitting at the console.  This constant determines whether those
 * are used.
 *
 * DEBUG:	This is the standard DEBUG flag that can be set for building
 *		the kernel.  It has the effect adding additional tests into
 *		the code.
 *
 * FONT_6x11:
 * FONT_8x8:
 * FONT_8x16:
 *		In theory these could be determined at run time or handed
 *		over by the booter.  But, let's be real, it's a fine hard
 *		coded value.  (But, you will notice the code is run-time
 *		flexible!)  A pointer to the font's struct font_desc
 *		is kept locally in Lconsole_font.  It is used to determine
 *		font size information dynamically.
 *
 * Atari constants:
 * USE_PRINTER:	Use the printer port for serial debug.
 * USE_SCC_B:	Use the SCC port A (Serial2) for serial debug.
 * USE_SCC_A:	Use the SCC port B (Modem2) for serial debug.
 * USE_MFP:	Use the ST-MFP port (Modem1) for serial debug.
 *
 * Macintosh constants:
 * MAC_SERIAL_DEBUG:	Turns on serial debug output for the Macintosh.
 * MAC_USE_SCC_A:	Use the SCC port A (modem) for serial debug.
 * MAC_USE_SCC_B:	Use the SCC port B (printer) for serial debug (default).
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/bootinfo.h>
#include <asm/setup.h>
#include <asm/entry.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/asm-offsets.h>

#ifdef CONFIG_MAC

#include <asm/machw.h>

/*
 * Macintosh console support
 */

#ifdef CONFIG_FRAMEBUFFER_CONSOLE
#define CONSOLE
#define CONSOLE_PENGUIN
#endif

/*
 * Macintosh serial debug support; outputs boot info to the printer
 *   and/or modem serial ports
 */
#undef MAC_SERIAL_DEBUG

/*
 * Macintosh serial debug port selection; define one or both;
 *   requires MAC_SERIAL_DEBUG to be defined
 */
#define MAC_USE_SCC_A		/* Macintosh modem serial port */
#define MAC_USE_SCC_B		/* Macintosh printer serial port */

#endif	/* CONFIG_MAC */

#undef MMU_PRINT
#undef MMU_NOCACHE_KERNEL
#define SERIAL_DEBUG
#undef DEBUG

/*
 * For the head.S console, there are three supported fonts, 6x11, 8x16 and 8x8.
 * The 8x8 font is harder to read but fits more on the screen.
 */
#define FONT_8x8	/* default */
/* #define FONT_8x16 */	/* 2nd choice */
/* #define FONT_6x11 */	/* 3rd choice */

.globl kernel_pg_dir
.globl availmem
.globl m68k_pgtable_cachemode
.globl m68k_supervisor_cachemode
#ifdef CONFIG_MVME16x
.globl mvme_bdid
#endif
#ifdef CONFIG_Q40
.globl q40_mem_cptr
#endif

CPUTYPE_040	= 1	/* indicates an 040 */
CPUTYPE_060	= 2	/* indicates an 060 */
CPUTYPE_0460	= 3	/* if either above are set, this is set */
CPUTYPE_020	= 4	/* indicates an 020 */

/* Translation control register */
TC_ENABLE = 0x8000
TC_PAGE8K = 0x4000
TC_PAGE4K = 0x0000

/* Transparent translation registers */
TTR_ENABLE	= 0x8000	/* enable transparent translation */
TTR_ANYMODE	= 0x4000	/* user and kernel mode access */
TTR_KERNELMODE	= 0x2000	/* only kernel mode access */
TTR_USERMODE	= 0x0000	/* only user mode access */
TTR_CI		= 0x0400	/* inhibit cache */
TTR_RW		= 0x0200	/* read/write mode */
TTR_RWM		= 0x0100	/* read/write mask */
TTR_FCB2	= 0x0040	/* function code base bit 2 */
TTR_FCB1	= 0x0020	/* function code base bit 1 */
TTR_FCB0	= 0x0010	/* function code base bit 0 */
TTR_FCM2	= 0x0004	/* function code mask bit 2 */
TTR_FCM1	= 0x0002	/* function code mask bit 1 */
TTR_FCM0	= 0x0001	/* function code mask bit 0 */

/* Cache Control registers */
CC6_ENABLE_D	= 0x80000000	/* enable data cache (680[46]0) */
CC6_FREEZE_D	= 0x40000000	/* freeze data cache (68060) */
CC6_ENABLE_SB	= 0x20000000	/* enable store buffer (68060) */
CC6_PUSH_DPI	= 0x10000000	/* disable CPUSH invalidation (68060) */
CC6_HALF_D	= 0x08000000	/* half-cache mode for data cache (68060) */
CC6_ENABLE_B	= 0x00800000	/* enable branch cache (68060) */
CC6_CLRA_B	= 0x00400000	/* clear all entries in branch cache (68060) */
CC6_CLRU_B	= 0x00200000	/* clear user entries in branch cache (68060) */
CC6_ENABLE_I	= 0x00008000	/* enable instruction cache (680[46]0) */
CC6_FREEZE_I	= 0x00004000	/* freeze instruction cache (68060) */
CC6_HALF_I	= 0x00002000	/* half-cache mode for instruction cache (68060) */
CC3_ALLOC_WRITE	= 0x00002000	/* write allocate mode(68030) */
CC3_ENABLE_DB	= 0x00001000	/* enable data burst (68030) */
CC3_CLR_D	= 0x00000800	/* clear data cache (68030) */
CC3_CLRE_D	= 0x00000400	/* clear entry in data cache (68030) */
CC3_FREEZE_D	= 0x00000200	/* freeze data cache (68030) */
CC3_ENABLE_D	= 0x00000100	/* enable data cache (68030) */
CC3_ENABLE_IB	= 0x00000010	/* enable instruction burst (68030) */
CC3_CLR_I	= 0x00000008	/* clear instruction cache (68030) */
CC3_CLRE_I	= 0x00000004	/* clear entry in instruction cache (68030) */
CC3_FREEZE_I	= 0x00000002	/* freeze instruction cache (68030) */
CC3_ENABLE_I	= 0x00000001	/* enable instruction cache (68030) */

/* Miscellaneous definitions */
PAGESIZE	= 4096
PAGESHIFT	= 12

ROOT_TABLE_SIZE	= 128
PTR_TABLE_SIZE	= 128
PAGE_TABLE_SIZE	= 64
ROOT_INDEX_SHIFT = 25
PTR_INDEX_SHIFT  = 18
PAGE_INDEX_SHIFT = 12

#ifdef DEBUG
/* When debugging use readable names for labels */
#ifdef __STDC__
#define L(name) .head.S.##name
#else
#define L(name) .head.S./**/name
#endif
#else
#ifdef __STDC__
#define L(name) .L##name
#else
#define L(name) .L/**/name
#endif
#endif

/* The __INITDATA stuff is a no-op when ftrace or kgdb are turned on */
#ifndef __INITDATA
#define __INITDATA	.data
#define __FINIT		.previous
#endif

/* Several macros to make the writing of subroutines easier:
 * - func_start marks the beginning of the routine which setups the frame
 *   register and saves the registers, it also defines another macro
 *   to automatically restore the registers again.
 * - func_return marks the end of the routine and simply calls the prepared
 *   macro to restore registers and jump back to the caller.
 * - func_define generates another macro to automatically put arguments
 *   onto the stack call the subroutine and cleanup the stack again.
 */

/* Within subroutines these macros can be used to access the arguments
 * on the stack. With STACK some allocated memory on the stack can be
 * accessed and ARG0 points to the return address (used by mmu_engage).
 */
#define	STACK	%a6@(stackstart)
#define ARG0	%a6@(4)
#define ARG1	%a6@(8)
#define ARG2	%a6@(12)
#define ARG3	%a6@(16)
#define ARG4	%a6@(20)

.macro	func_start	name,saveregs,stack=0
L(\name):
	linkw	%a6,#-\stack
	moveml	\saveregs,%sp@-
.set	stackstart,-\stack

.macro	func_return_\name
	moveml	%sp@+,\saveregs
	unlk	%a6
	rts
.endm
.endm

.macro	func_return	name
	func_return_\name
.endm

.macro	func_call	name
	jbsr	L(\name)
.endm

.macro	move_stack	nr,arg1,arg2,arg3,arg4
.if	\nr
	move_stack	"(\nr-1)",\arg2,\arg3,\arg4
	movel	\arg1,%sp@-
.endif
.endm

.macro	func_define	name,nr=0
.macro	\name	arg1,arg2,arg3,arg4
	move_stack	\nr,\arg1,\arg2,\arg3,\arg4
	func_call	\name
.if	\nr
	lea	%sp@(\nr*4),%sp
.endif
.endm
.endm

func_define	mmu_map,4
func_define	mmu_map_tt,4
func_define	mmu_fixup_page_mmu_cache,1
func_define	mmu_temp_map,2
func_define	mmu_engage
func_define	mmu_get_root_table_entry,1
func_define	mmu_get_ptr_table_entry,2
func_define	mmu_get_page_table_entry,2
func_define	mmu_print
func_define	get_new_page
#if defined(CONFIG_HP300) || defined(CONFIG_APOLLO)
func_define	set_leds
#endif

.macro	mmu_map_eq	arg1,arg2,arg3
	mmu_map	\arg1,\arg1,\arg2,\arg3
.endm

.macro	get_bi_record	record
	pea	\record
	func_call	get_bi_record
	addql	#4,%sp
.endm

func_define	serial_putc,1
func_define	console_putc,1

func_define	console_init
func_define	console_put_stats
func_define	console_put_penguin
func_define	console_plot_pixel,3
func_define	console_scroll

.macro	putc	ch
#if defined(CONSOLE) || defined(SERIAL_DEBUG)
	pea	\ch
#endif
#ifdef CONSOLE
	func_call	console_putc
#endif
#ifdef SERIAL_DEBUG
	func_call	serial_putc
#endif
#if defined(CONSOLE) || defined(SERIAL_DEBUG)
	addql	#4,%sp
#endif
.endm

.macro	dputc	ch
#ifdef DEBUG
	putc	\ch
#endif
.endm

func_define	putn,1

.macro	dputn	nr
#ifdef DEBUG
	putn	\nr
#endif
.endm

.macro	puts		string
#if defined(CONSOLE) || defined(SERIAL_DEBUG)
	__INITDATA
.Lstr\@:
	.string	"\string"
	__FINIT
	pea	%pc@(.Lstr\@)
	func_call	puts
	addql	#4,%sp
#endif
.endm

.macro	dputs	string
#ifdef DEBUG
	puts	"\string"
#endif
.endm

#define is_not_amiga(lab) cmpl &MACH_AMIGA,%pc@(m68k_machtype); jne lab
#define is_not_atari(lab) cmpl &MACH_ATARI,%pc@(m68k_machtype); jne lab
#define is_not_mac(lab) cmpl &MACH_MAC,%pc@(m68k_machtype); jne lab
#define is_not_mvme147(lab) cmpl &MACH_MVME147,%pc@(m68k_machtype); jne lab
#define is_not_mvme16x(lab) cmpl &MACH_MVME16x,%pc@(m68k_machtype); jne lab
#define is_not_bvme6000(lab) cmpl &MACH_BVME6000,%pc@(m68k_machtype); jne lab
#define is_mvme147(lab) cmpl &MACH_MVME147,%pc@(m68k_machtype); jeq lab
#define is_mvme16x(lab) cmpl &MACH_MVME16x,%pc@(m68k_machtype); jeq lab
#define is_bvme6000(lab) cmpl &MACH_BVME6000,%pc@(m68k_machtype); jeq lab
#define is_not_hp300(lab) cmpl &MACH_HP300,%pc@(m68k_machtype); jne lab
#define is_not_apollo(lab) cmpl &MACH_APOLLO,%pc@(m68k_machtype); jne lab
#define is_not_q40(lab) cmpl &MACH_Q40,%pc@(m68k_machtype); jne lab
#define is_not_sun3x(lab) cmpl &MACH_SUN3X,%pc@(m68k_machtype); jne lab

#define hasnt_leds(lab) cmpl &MACH_HP300,%pc@(m68k_machtype); \
			jeq 42f; \
			cmpl &MACH_APOLLO,%pc@(m68k_machtype); \
			jne lab ;\
		42:\

#define is_040_or_060(lab)	btst &CPUTYPE_0460,%pc@(L(cputype)+3); jne lab
#define is_not_040_or_060(lab)	btst &CPUTYPE_0460,%pc@(L(cputype)+3); jeq lab
#define is_040(lab)		btst &CPUTYPE_040,%pc@(L(cputype)+3); jne lab
#define is_060(lab)		btst &CPUTYPE_060,%pc@(L(cputype)+3); jne lab
#define is_not_060(lab)		btst &CPUTYPE_060,%pc@(L(cputype)+3); jeq lab
#define is_020(lab)		btst &CPUTYPE_020,%pc@(L(cputype)+3); jne lab
#define is_not_020(lab)		btst &CPUTYPE_020,%pc@(L(cputype)+3); jeq lab

/* On the HP300 we use the on-board LEDs for debug output before
   the console is running.  Writing a 1 bit turns the corresponding LED
   _off_ - on the 340 bit 7 is towards the back panel of the machine.  */
.macro	leds	mask
#if defined(CONFIG_HP300) || defined(CONFIG_APOLLO)
	hasnt_leds(.Lled\@)
	pea	\mask
	func_call	set_leds
	addql	#4,%sp
.Lled\@:
#endif
.endm

__HEAD
ENTRY(_stext)
/*
 * Version numbers of the bootinfo interface
 * The area from _stext to _start will later be used as kernel pointer table
 */
	bras	1f	/* Jump over bootinfo version numbers */

	.long	BOOTINFOV_MAGIC
	.long	MACH_AMIGA, AMIGA_BOOTI_VERSION
	.long	MACH_ATARI, ATARI_BOOTI_VERSION
	.long	MACH_MVME147, MVME147_BOOTI_VERSION
	.long	MACH_MVME16x, MVME16x_BOOTI_VERSION
	.long	MACH_BVME6000, BVME6000_BOOTI_VERSION
	.long	MACH_MAC, MAC_BOOTI_VERSION
	.long	MACH_Q40, Q40_BOOTI_VERSION
	.long	MACH_HP300, HP300_BOOTI_VERSION
	.long	0
1:	jra	__start

.equ	kernel_pg_dir,_stext

.equ	.,_stext+PAGESIZE

ENTRY(_start)
	jra	__start
__INIT
ENTRY(__start)
/*
 * Setup initial stack pointer
 */
	lea	%pc@(_stext),%sp

/*
 * Record the CPU and machine type.
 */
	get_bi_record	BI_MACHTYPE
	lea	%pc@(m68k_machtype),%a1
	movel	%a0@,%a1@

	get_bi_record	BI_FPUTYPE
	lea	%pc@(m68k_fputype),%a1
	movel	%a0@,%a1@

	get_bi_record	BI_MMUTYPE
	lea	%pc@(m68k_mmutype),%a1
	movel	%a0@,%a1@

	get_bi_record	BI_CPUTYPE
	lea	%pc@(m68k_cputype),%a1
	movel	%a0@,%a1@

	leds	0x1

#ifdef CONFIG_MAC
/*
 * For Macintosh, we need to determine the display parameters early (at least
 * while debugging it).
 */

	is_not_mac(L(test_notmac))

	get_bi_record	BI_MAC_VADDR
	lea	%pc@(L(mac_videobase)),%a1
	movel	%a0@,%a1@

	get_bi_record	BI_MAC_VDEPTH
	lea	%pc@(L(mac_videodepth)),%a1
	movel	%a0@,%a1@

	get_bi_record	BI_MAC_VDIM
	lea	%pc@(L(mac_dimensions)),%a1
	movel	%a0@,%a1@

	get_bi_record	BI_MAC_VROW
	lea	%pc@(L(mac_rowbytes)),%a1
	movel	%a0@,%a1@

#ifdef MAC_SERIAL_DEBUG
	get_bi_record	BI_MAC_SCCBASE
	lea	%pc@(L(mac_sccbase)),%a1
	movel	%a0@,%a1@
#endif /* MAC_SERIAL_DEBUG */

#if 0
	/*
	 * Clear the screen
	 */
	lea	%pc@(L(mac_videobase)),%a0
	movel	%a0@,%a1
	lea	%pc@(L(mac_dimensions)),%a0
	movel	%a0@,%d1
	swap	%d1		/* #rows is high bytes */
	andl	#0xFFFF,%d1	/* rows */
	subl	#10,%d1
	lea	%pc@(L(mac_rowbytes)),%a0
loopy2:
	movel	%a0@,%d0
	subql	#1,%d0
loopx2:
	moveb	#0x55, %a1@+
	dbra	%d0,loopx2
	dbra	%d1,loopy2
#endif

L(test_notmac):
#endif /* CONFIG_MAC */


/*
 * There are ultimately two pieces of information we want for all kinds of
 * processors CpuType and CacheBits.  The CPUTYPE was passed in from booter
 * and is converted here from a booter type definition to a separate bit
 * number which allows for the standard is_0x0 macro tests.
 */
	movel	%pc@(m68k_cputype),%d0
	/*
	 * Assume it's an 030
	 */
	clrl	%d1

	/*
	 * Test the BootInfo cputype for 060
	 */
	btst	#CPUB_68060,%d0
	jeq	1f
	bset	#CPUTYPE_060,%d1
	bset	#CPUTYPE_0460,%d1
	jra	3f
1:
	/*
	 * Test the BootInfo cputype for 040
	 */
	btst	#CPUB_68040,%d0
	jeq	2f
	bset	#CPUTYPE_040,%d1
	bset	#CPUTYPE_0460,%d1
	jra	3f
2:
	/*
	 * Test the BootInfo cputype for 020
	 */
	btst	#CPUB_68020,%d0
	jeq	3f
	bset	#CPUTYPE_020,%d1
	jra	3f
3:
	/*
	 * Record the cpu type
	 */
	lea	%pc@(L(cputype)),%a0
	movel	%d1,%a0@

	/*
	 * NOTE:
	 *
	 * Now the macros are valid:
	 *	is_040_or_060
	 *	is_not_040_or_060
	 *	is_040
	 *	is_060
	 *	is_not_060
	 */

	/*
	 * Determine the cache mode for pages holding MMU tables
	 * and for supervisor mode, unused for '020 and '030
	 */
	clrl	%d0
	clrl	%d1

	is_not_040_or_060(L(save_cachetype))

	/*
	 * '040 or '060
	 * d1 := cacheable write-through
	 * NOTE: The 68040 manual strongly recommends non-cached for MMU tables,
	 * but we have been using write-through since at least 2.0.29 so I
	 * guess it is OK.
	 */
#ifdef CONFIG_060_WRITETHROUGH
	/*
	 * If this is a 68060 board using drivers with cache coherency
	 * problems, then supervisor memory accesses need to be write-through
	 * also; otherwise, we want copyback.
	 */

	is_not_060(1f)
	movel	#_PAGE_CACHE040W,%d0
	jra	L(save_cachetype)
#endif /* CONFIG_060_WRITETHROUGH */
1:
	movew	#_PAGE_CACHE040,%d0

	movel	#_PAGE_CACHE040W,%d1

L(save_cachetype):
	/* Save cache mode for supervisor mode and page tables
	 */
	lea	%pc@(m68k_supervisor_cachemode),%a0
	movel	%d0,%a0@
	lea	%pc@(m68k_pgtable_cachemode),%a0
	movel	%d1,%a0@

/*
 * raise interrupt level
 */
	movew	#0x2700,%sr

/*
   If running on an Atari, determine the I/O base of the
   serial port and test if we are running on a Medusa or Hades.
   This test is necessary here, because on the Hades the serial
   port is only accessible in the high I/O memory area.

   The test whether it is a Medusa is done by writing to the byte at
   phys. 0x0. This should result in a bus error on all other machines.

   ...should, but doesn't. The Afterburner040 for the Falcon has the
   same behaviour (0x0..0x7 are no ROM shadow). So we have to do
   another test to distinguish Medusa and AB040. This is a
   read attempt for 0x00ff82fe phys. that should bus error on a Falcon
   (+AB040), but is in the range where the Medusa always asserts DTACK.

   The test for the Hades is done by reading address 0xb0000000. This
   should give a bus error on the Medusa.
 */

#ifdef CONFIG_ATARI
	is_not_atari(L(notypetest))

	/* get special machine type (Medusa/Hades/AB40) */
	moveq	#0,%d3 /* default if tag doesn't exist */
	get_bi_record	BI_ATARI_MCH_TYPE
	tstl	%d0
	jbmi	1f
	movel	%a0@,%d3
	lea	%pc@(atari_mch_type),%a0
	movel	%d3,%a0@
1:
	/* On the Hades, the iobase must be set up before opening the
	 * serial port. There are no I/O regs at 0x00ffxxxx at all. */
	moveq	#0,%d0
	cmpl	#ATARI_MACH_HADES,%d3
	jbne	1f
	movel	#0xff000000,%d0		/* Hades I/O base addr: 0xff000000 */
1:	lea     %pc@(L(iobase)),%a0
	movel   %d0,%a0@

L(notypetest):
#endif

#ifdef CONFIG_VME
	is_mvme147(L(getvmetype))
	is_bvme6000(L(getvmetype))
	is_not_mvme16x(L(gvtdone))

	/* See if the loader has specified the BI_VME_TYPE tag.  Recent
	 * versions of VMELILO and TFTPLILO do this.  We have to do this
	 * early so we know how to handle console output.  If the tag
	 * doesn't exist then we use the Bug for output on MVME16x.
	 */
L(getvmetype):
	get_bi_record	BI_VME_TYPE
	tstl	%d0
	jbmi	1f
	movel	%a0@,%d3
	lea	%pc@(vme_brdtype),%a0
	movel	%d3,%a0@
1:
#ifdef CONFIG_MVME16x
	is_not_mvme16x(L(gvtdone))

	/* Need to get the BRD_ID info to differentiate between 162, 167,
	 * etc.  This is available as a BI_VME_BRDINFO tag with later
	 * versions of VMELILO and TFTPLILO, otherwise we call the Bug.
	 */
	get_bi_record	BI_VME_BRDINFO
	tstl	%d0
	jpl	1f

	/* Get pointer to board ID data from Bug */
	movel	%d2,%sp@-
	trap	#15
	.word	0x70		/* trap 0x70 - .BRD_ID */
	movel	%sp@+,%a0
1:
	lea	%pc@(mvme_bdid),%a1
	/* Structure is 32 bytes long */
	movel	%a0@+,%a1@+
	movel	%a0@+,%a1@+
	movel	%a0@+,%a1@+
	movel	%a0@+,%a1@+
	movel	%a0@+,%a1@+
	movel	%a0@+,%a1@+
	movel	%a0@+,%a1@+
	movel	%a0@+,%a1@+
#endif

L(gvtdone):

#endif

#ifdef CONFIG_HP300
	is_not_hp300(L(nothp))

	/* Get the address of the UART for serial debugging */
	get_bi_record	BI_HP300_UART_ADDR
	tstl	%d0
	jbmi	1f
	movel	%a0@,%d3
	lea	%pc@(L(uartbase)),%a0
	movel	%d3,%a0@
	get_bi_record	BI_HP300_UART_SCODE
	tstl	%d0
	jbmi	1f
	movel	%a0@,%d3
	lea	%pc@(L(uart_scode)),%a0
	movel	%d3,%a0@
1:
L(nothp):
#endif

/*
 * Initialize serial port
 */
	jbsr	L(serial_init)

/*
 * Initialize console
 */
#ifdef CONFIG_MAC
	is_not_mac(L(nocon))
#ifdef CONSOLE
	console_init
#ifdef CONSOLE_PENGUIN
	console_put_penguin
#endif	/* CONSOLE_PENGUIN */
	console_put_stats
#endif	/* CONSOLE */
L(nocon):
#endif	/* CONFIG_MAC */


	putc	'\n'
	putc	'A'
	leds	0x2
	dputn	%pc@(L(cputype))
	dputn	%pc@(m68k_supervisor_cachemode)
	dputn	%pc@(m68k_pgtable_cachemode)
	dputc	'\n'

/*
 * Save physical start address of kernel
 */
	lea	%pc@(L(phys_kernel_start)),%a0
	lea	%pc@(_stext),%a1
	subl	#_stext,%a1
	addl	#PAGE_OFFSET,%a1
	movel	%a1,%a0@

	putc	'B'

	leds	0x4

/*
 *	mmu_init
 *
 *	This block of code does what's necessary to map in the various kinds
 *	of machines for execution of Linux.
 *	First map the first 4 MB of kernel code & data
 */

	mmu_map	#PAGE_OFFSET,%pc@(L(phys_kernel_start)),#4*1024*1024,\
		%pc@(m68k_supervisor_cachemode)

	putc	'C'

#ifdef CONFIG_AMIGA

L(mmu_init_amiga):

	is_not_amiga(L(mmu_init_not_amiga))
/*
 * mmu_init_amiga
 */

	putc	'D'

	is_not_040_or_060(1f)

	/*
	 * 040: Map the 16Meg range physical 0x0 upto logical 0x8000.0000
	 */
	mmu_map		#0x80000000,#0,#0x01000000,#_PAGE_NOCACHE_S
	/*
	 * Map the Zorro III I/O space with transparent translation
	 * for frame buffer memory etc.
	 */
	mmu_map_tt	#1,#0x40000000,#0x20000000,#_PAGE_NOCACHE_S

	jbra	L(mmu_init_done)

1:
	/*
	 * 030:	Map the 32Meg range physical 0x0 upto logical 0x8000.0000
	 */
	mmu_map		#0x80000000,#0,#0x02000000,#_PAGE_NOCACHE030
	mmu_map_tt	#1,#0x40000000,#0x20000000,#_PAGE_NOCACHE030

	jbra	L(mmu_init_done)

L(mmu_init_not_amiga):
#endif

#ifdef CONFIG_ATARI

L(mmu_init_atari):

	is_not_atari(L(mmu_init_not_atari))

	putc	'E'

/* On the Atari, we map the I/O region (phys. 0x00ffxxxx) by mapping
   the last 16 MB of virtual address space to the first 16 MB (i.e.
   0xffxxxxxx -> 0x00xxxxxx). For this, an additional pointer table is
   needed. I/O ranges are marked non-cachable.

   For the Medusa it is better to map the I/O region transparently
   (i.e. 0xffxxxxxx -> 0xffxxxxxx), because some I/O registers are
   accessible only in the high area.

   On the Hades all I/O registers are only accessible in the high
   area.
*/

	/* I/O base addr for non-Medusa, non-Hades: 0x00000000 */
	moveq	#0,%d0
	movel	%pc@(atari_mch_type),%d3
	cmpl	#ATARI_MACH_MEDUSA,%d3
	jbeq	2f
	cmpl	#ATARI_MACH_HADES,%d3
	jbne	1f
2:	movel	#0xff000000,%d0 /* Medusa/Hades base addr: 0xff000000 */
1:	movel	%d0,%d3

	is_040_or_060(L(spata68040))

	/* Map everything non-cacheable, though not all parts really
	 * need to disable caches (crucial only for 0xff8000..0xffffff
	 * (standard I/O) and 0xf00000..0xf3ffff (IDE)). The remainder
	 * isn't really used, except for sometimes peeking into the
	 * ROMs (mirror at phys. 0x0), so caching isn't necessary for
	 * this. */
	mmu_map	#0xff000000,%d3,#0x01000000,#_PAGE_NOCACHE030

	jbra	L(mmu_init_done)

L(spata68040):

	mmu_map	#0xff000000,%d3,#0x01000000,#_PAGE_NOCACHE_S

	jbra	L(mmu_init_done)

L(mmu_init_not_atari):
#endif

#ifdef CONFIG_Q40
	is_not_q40(L(notq40))
	/*
	 * add transparent mapping for 0xff00 0000 - 0xffff ffff
	 * non-cached serialized etc..
	 * this includes master chip, DAC, RTC and ISA ports
	 * 0xfe000000-0xfeffffff is for screen and ROM
	 */

	putc    'Q'

	mmu_map_tt	#0,#0xfe000000,#0x01000000,#_PAGE_CACHE040W
	mmu_map_tt	#1,#0xff000000,#0x01000000,#_PAGE_NOCACHE_S

	jbra	L(mmu_init_done)

L(notq40):
#endif

#ifdef CONFIG_HP300
	is_not_hp300(L(nothp300))

	/* On the HP300, we map the ROM, INTIO and DIO regions (phys. 0x00xxxxxx)
	 * by mapping 32MB (on 020/030) or 16 MB (on 040) from 0xf0xxxxxx -> 0x00xxxxxx).
	 * The ROM mapping is needed because the LEDs are mapped there too.
	 */

	is_040(1f)

	/*
	 * 030: Map the 32Meg range physical 0x0 upto logical 0xf000.0000
	 */
	mmu_map	#0xf0000000,#0,#0x02000000,#_PAGE_NOCACHE030

	jbra	L(mmu_init_done)

1:
	/*
	 * 040: Map the 16Meg range physical 0x0 upto logical 0xf000.0000
	 */
	mmu_map #0xf0000000,#0,#0x01000000,#_PAGE_NOCACHE_S

	jbra	L(mmu_init_done)

L(nothp300):
#endif /* CONFIG_HP300 */

#ifdef CONFIG_MVME147

	is_not_mvme147(L(not147))

	/*
	 * On MVME147 we have already created kernel page tables for
	 * 4MB of RAM at address 0, so now need to do a transparent
	 * mapping of the top of memory space.  Make it 0.5GByte for now,
	 * so we can access on-board i/o areas.
	 */

	mmu_map_tt	#1,#0xe0000000,#0x20000000,#_PAGE_NOCACHE030

	jbra	L(mmu_init_done)

L(not147):
#endif /* CONFIG_MVME147 */

#ifdef CONFIG_MVME16x

	is_not_mvme16x(L(not16x))

	/*
	 * On MVME16x we have already created kernel page tables for
	 * 4MB of RAM at address 0, so now need to do a transparent
	 * mapping of the top of memory space.  Make it 0.5GByte for now.
	 * Supervisor only access, so transparent mapping doesn't
	 * clash with User code virtual address space.
	 * this covers IO devices, PROM and SRAM.  The PROM and SRAM
	 * mapping is needed to allow 167Bug to run.
	 * IO is in the range 0xfff00000 to 0xfffeffff.
	 * PROM is 0xff800000->0xffbfffff and SRAM is
	 * 0xffe00000->0xffe1ffff.
	 */

	mmu_map_tt	#1,#0xe0000000,#0x20000000,#_PAGE_NOCACHE_S

	jbra	L(mmu_init_done)

L(not16x):
#endif	/* CONFIG_MVME162 | CONFIG_MVME167 */

#ifdef CONFIG_BVME6000

	is_not_bvme6000(L(not6000))

	/*
	 * On BVME6000 we have already created kernel page tables for
	 * 4MB of RAM at address 0, so now need to do a transparent
	 * mapping of the top of memory space.  Make it 0.5GByte for now,
	 * so we can access on-board i/o areas.
	 * Supervisor only access, so transparent mapping doesn't
	 * clash with User code virtual address space.
	 */

	mmu_map_tt	#1,#0xe0000000,#0x20000000,#_PAGE_NOCACHE_S

	jbra	L(mmu_init_done)

L(not6000):
#endif /* CONFIG_BVME6000 */

/*
 * mmu_init_mac
 *
 * The Macintosh mappings are less clear.
 *
 * Even as of this writing, it is unclear how the
 * Macintosh mappings will be done.  However, as
 * the first author of this code I'm proposing the
 * following model:
 *
 * Map the kernel (that's already done),
 * Map the I/O (on most machines that's the
 * 0x5000.0000 ... 0x5300.0000 range,
 * Map the video frame buffer using as few pages
 * as absolutely (this requirement mostly stems from
 * the fact that when the frame buffer is at
 * 0x0000.0000 then we know there is valid RAM just
 * above the screen that we don't want to waste!).
 *
 * By the way, if the frame buffer is at 0x0000.0000
 * then the Macintosh is known as an RBV based Mac.
 *
 * By the way 2, the code currently maps in a bunch of
 * regions.  But I'd like to cut that out.  (And move most
 * of the mappings up into the kernel proper ... or only
 * map what's necessary.)
 */

#ifdef CONFIG_MAC

L(mmu_init_mac):

	is_not_mac(L(mmu_init_not_mac))

	putc	'F'

	is_not_040_or_060(1f)

	moveq	#_PAGE_NOCACHE_S,%d3
	jbra	2f
1:
	moveq	#_PAGE_NOCACHE030,%d3
2:
	/*
	 * Mac Note: screen address of logical 0xF000.0000 -> <screen physical>
	 *	     we simply map the 4MB that contains the videomem
	 */

	movel	#VIDEOMEMMASK,%d0
	andl	%pc@(L(mac_videobase)),%d0

	mmu_map		#VIDEOMEMBASE,%d0,#VIDEOMEMSIZE,%d3
	/* ROM from 4000 0000 to 4200 0000 (only for mac_reset()) */
	mmu_map_eq	#0x40000000,#0x02000000,%d3
	/* IO devices (incl. serial port) from 5000 0000 to 5300 0000 */
	mmu_map_eq	#0x50000000,#0x03000000,%d3
	/* Nubus slot space (video at 0xF0000000, rom at 0xF0F80000) */
	mmu_map_tt	#1,#0xf8000000,#0x08000000,%d3

	jbra	L(mmu_init_done)

L(mmu_init_not_mac):
#endif

#ifdef CONFIG_SUN3X
	is_not_sun3x(L(notsun3x))

	/* oh, the pain..  We're gonna want the prom code after
	 * starting the MMU, so we copy the mappings, translating
	 * from 8k -> 4k pages as we go.
	 */

	/* copy maps from 0xfee00000 to 0xff000000 */
	movel	#0xfee00000, %d0
	moveq	#ROOT_INDEX_SHIFT, %d1
	lsrl	%d1,%d0
	mmu_get_root_table_entry	%d0

	movel	#0xfee00000, %d0
	moveq	#PTR_INDEX_SHIFT, %d1
	lsrl	%d1,%d0
	andl	#PTR_TABLE_SIZE-1, %d0
	mmu_get_ptr_table_entry		%a0,%d0

	movel	#0xfee00000, %d0
	moveq	#PAGE_INDEX_SHIFT, %d1
	lsrl	%d1,%d0
	andl	#PAGE_TABLE_SIZE-1, %d0
	mmu_get_page_table_entry	%a0,%d0

	/* this is where the prom page table lives */
	movel	0xfefe00d4, %a1
	movel	%a1@, %a1

	movel	#((0x200000 >> 13)-1), %d1

1:
	movel	%a1@+, %d3
	movel	%d3,%a0@+
	addl	#0x1000,%d3
	movel	%d3,%a0@+

	dbra	%d1,1b

	/* setup tt1 for I/O */
	mmu_map_tt	#1,#0x40000000,#0x40000000,#_PAGE_NOCACHE_S
	jbra	L(mmu_init_done)

L(notsun3x):
#endif

#ifdef CONFIG_APOLLO
	is_not_apollo(L(notapollo))

	putc	'P'
	mmu_map         #0x80000000,#0,#0x02000000,#_PAGE_NOCACHE030

L(notapollo):
	jbra	L(mmu_init_done)
#endif

L(mmu_init_done):

	putc	'G'
	leds	0x8

/*
 * mmu_fixup
 *
 * On the 040 class machines, all pages that are used for the
 * mmu have to be fixed up. According to Motorola, pages holding mmu
 * tables should be non-cacheable on a '040 and write-through on a
 * '060. But analysis of the reasons for this, and practical
 * experience, showed that write-through also works on a '040.
 *
 * Allocated memory so far goes from kernel_end to memory_start that
 * is used for all kind of tables, for that the cache attributes
 * are now fixed.
 */
L(mmu_fixup):

	is_not_040_or_060(L(mmu_fixup_done))

#ifdef MMU_NOCACHE_KERNEL
	jbra	L(mmu_fixup_done)
#endif

	/* first fix the page at the start of the kernel, that
	 * contains also kernel_pg_dir.
	 */
	movel	%pc@(L(phys_kernel_start)),%d0
	subl	#PAGE_OFFSET,%d0
	lea	%pc@(_stext),%a0
	subl	%d0,%a0
	mmu_fixup_page_mmu_cache	%a0

	movel	%pc@(L(kernel_end)),%a0
	subl	%d0,%a0
	movel	%pc@(L(memory_start)),%a1
	subl	%d0,%a1
	bra	2f
1:
	mmu_fixup_page_mmu_cache	%a0
	addw	#PAGESIZE,%a0
2:
	cmpl	%a0,%a1
	jgt	1b

L(mmu_fixup_done):

#ifdef MMU_PRINT
	mmu_print
#endif

/*
 * mmu_engage
 *
 * This chunk of code performs the gruesome task of engaging the MMU.
 * The reason its gruesome is because when the MMU becomes engaged it
 * maps logical addresses to physical addresses.  The Program Counter
 * register is then passed through the MMU before the next instruction
 * is fetched (the instruction following the engage MMU instruction).
 * This may mean one of two things:
 * 1. The Program Counter falls within the logical address space of
 *    the kernel of which there are two sub-possibilities:
 *    A. The PC maps to the correct instruction (logical PC == physical
 *       code location), or
 *    B. The PC does not map through and the processor will read some
 *       data (or instruction) which is not the logically next instr.
 *    As you can imagine, A is good and B is bad.
 * Alternatively,
 * 2. The Program Counter does not map through the MMU.  The processor
 *    will take a Bus Error.
 * Clearly, 2 is bad.
 * It doesn't take a wiz kid to figure you want 1.A.
 * This code creates that possibility.
 * There are two possible 1.A. states (we now ignore the other above states):
 * A. The kernel is located at physical memory addressed the same as
 *    the logical memory for the kernel, i.e., 0x01000.
 * B. The kernel is located some where else.  e.g., 0x0400.0000
 *
 *    Under some conditions the Macintosh can look like A or B.
 * [A friend and I once noted that Apple hardware engineers should be
 * wacked twice each day: once when they show up at work (as in, Whack!,
 * "This is for the screwy hardware we know you're going to design today."),
 * and also at the end of the day (as in, Whack! "I don't know what
 * you designed today, but I'm sure it wasn't good."). -- rst]
 *
 * This code works on the following premise:
 * If the kernel start (%d5) is within the first 16 Meg of RAM,
 * then create a mapping for the kernel at logical 0x8000.0000 to
 * the physical location of the pc.  And, create a transparent
 * translation register for the first 16 Meg.  Then, after the MMU
 * is engaged, the PC can be moved up into the 0x8000.0000 range
 * and then the transparent translation can be turned off and then
 * the PC can jump to the correct logical location and it will be
 * home (finally).  This is essentially the code that the Amiga used
 * to use.  Now, it's generalized for all processors.  Which means
 * that a fresh (but temporary) mapping has to be created.  The mapping
 * is made in page 0 (an as of yet unused location -- except for the
 * stack!).  This temporary mapping will only require 1 pointer table
 * and a single page table (it can map 256K).
 *
 * OK, alternatively, imagine that the Program Counter is not within
 * the first 16 Meg.  Then, just use Transparent Translation registers
 * to do the right thing.
 *
 * Last, if _start is already at 0x01000, then there's nothing special
 * to do (in other words, in a degenerate case of the first case above,
 * do nothing).
 *
 * Let's do it.
 *
 *
 */

	putc	'H'

	mmu_engage

/*
 * After this point no new memory is allocated and
 * the start of available memory is stored in availmem.
 * (The bootmem allocator requires now the physicall address.)
 */

	movel	L(memory_start),availmem

#ifdef CONFIG_AMIGA
	is_not_amiga(1f)
	/* fixup the Amiga custom register location before printing */
	clrl	L(custom)
1:
#endif

#ifdef CONFIG_ATARI
	is_not_atari(1f)
	/* fixup the Atari iobase register location before printing */
	movel	#0xff000000,L(iobase)
1:
#endif

#ifdef CONFIG_MAC
	is_not_mac(1f)
	movel	#~VIDEOMEMMASK,%d0
	andl	L(mac_videobase),%d0
	addl	#VIDEOMEMBASE,%d0
	movel	%d0,L(mac_videobase)
#if defined(CONSOLE)
	movel	%pc@(L(phys_kernel_start)),%d0
	subl	#PAGE_OFFSET,%d0
	subl	%d0,L(console_font)
	subl	%d0,L(console_font_data)
#endif
#ifdef MAC_SERIAL_DEBUG
	orl	#0x50000000,L(mac_sccbase)
#endif
1:
#endif

#ifdef CONFIG_HP300
	is_not_hp300(2f)
	/*
	 * Fix up the iobase register to point to the new location of the LEDs.
	 */
	movel	#0xf0000000,L(iobase)

	/*
	 * Energise the FPU and caches.
	 */
	is_040(1f)
	movel	#0x60,0xf05f400c
	jbra	2f

	/*
	 * 040: slightly different, apparently.
	 */
1:	movew	#0,0xf05f400e
	movew	#0x64,0xf05f400e
2:
#endif

#ifdef CONFIG_SUN3X
	is_not_sun3x(1f)

	/* enable copro */
	oriw	#0x4000,0x61000000
1:
#endif

#ifdef CONFIG_APOLLO
	is_not_apollo(1f)

	/*
	 * Fix up the iobase before printing
	 */
	movel	#0x80000000,L(iobase)
1:
#endif

	putc	'I'
	leds	0x10

/*
 * Enable caches
 */

	is_not_040_or_060(L(cache_not_680460))

L(cache680460):
	.chip	68040
	nop
	cpusha	%bc
	nop

	is_060(L(cache68060))

	movel	#CC6_ENABLE_D+CC6_ENABLE_I,%d0
	/* MMU stuff works in copyback mode now, so enable the cache */
	movec	%d0,%cacr
	jra	L(cache_done)

L(cache68060):
	movel	#CC6_ENABLE_D+CC6_ENABLE_I+CC6_ENABLE_SB+CC6_PUSH_DPI+CC6_ENABLE_B+CC6_CLRA_B,%d0
	/* MMU stuff works in copyback mode now, so enable the cache */
	movec	%d0,%cacr
	/* enable superscalar dispatch in PCR */
	moveq	#1,%d0
	.chip	68060
	movec	%d0,%pcr

	jbra	L(cache_done)
L(cache_not_680460):
L(cache68030):
	.chip	68030
	movel	#CC3_ENABLE_DB+CC3_CLR_D+CC3_ENABLE_D+CC3_ENABLE_IB+CC3_CLR_I+CC3_ENABLE_I,%d0
	movec	%d0,%cacr

	jra	L(cache_done)
	.chip	68k
L(cache_done):

	putc	'J'

/*
 * Setup initial stack pointer
 */
	lea	init_task,%curptr
	lea	init_thread_union+THREAD_SIZE,%sp

	putc	'K'

	subl	%a6,%a6		/* clear a6 for gdb */

/*
 * The new 64bit printf support requires an early exception initialization.
 */
	jbsr	base_trap_init

/* jump to the kernel start */

	putc	'\n'
	leds	0x55

	jbsr	start_kernel

/*
 * Find a tag record in the bootinfo structure
 * The bootinfo structure is located right after the kernel bss
 * Returns: d0: size (-1 if not found)
 *          a0: data pointer (end-of-records if not found)
 */
func_start	get_bi_record,%d1

	movel	ARG1,%d0
	lea	%pc@(_end),%a0
1:	tstw	%a0@(BIR_TAG)
	jeq	3f
	cmpw	%a0@(BIR_TAG),%d0
	jeq	2f
	addw	%a0@(BIR_SIZE),%a0
	jra	1b
2:	moveq	#0,%d0
	movew	%a0@(BIR_SIZE),%d0
	lea	%a0@(BIR_DATA),%a0
	jra	4f
3:	moveq	#-1,%d0
	lea	%a0@(BIR_SIZE),%a0
4:
func_return	get_bi_record


/*
 *	MMU Initialization Begins Here
 *
 *	The structure of the MMU tables on the 68k machines
 *	is thus:
 *	Root Table
 *		Logical addresses are translated through
 *	a hierarchical translation mechanism where the high-order
 *	seven bits of the logical address (LA) are used as an
 *	index into the "root table."  Each entry in the root
 *	table has a bit which specifies if it's a valid pointer to a
 *	pointer table.  Each entry defines a 32KMeg range of memory.
 *	If an entry is invalid then that logical range of 32M is
 *	invalid and references to that range of memory (when the MMU
 *	is enabled) will fault.  If the entry is valid, then it does
 *	one of two things.  On 040/060 class machines, it points to
 *	a pointer table which then describes more finely the memory
 *	within that 32M range.  On 020/030 class machines, a technique
 *	called "early terminating descriptors" are used.  This technique
 *	allows an entire 32Meg to be described by a single entry in the
 *	root table.  Thus, this entry in the root table, contains the
 *	physical address of the memory or I/O at the logical address
 *	which the entry represents and it also contains the necessary
 *	cache bits for this region.
 *
 *	Pointer Tables
 *		Per the Root Table, there will be one or more
 *	pointer tables.  Each pointer table defines a 32M range.
 *	Not all of the 32M range need be defined.  Again, the next
 *	seven bits of the logical address are used an index into
 *	the pointer table to point to page tables (if the pointer
 *	is valid).  There will undoubtedly be more than one
 *	pointer table for the kernel because each pointer table
 *	defines a range of only 32M.  Valid pointer table entries
 *	point to page tables, or are early terminating entries
 *	themselves.
 *
 *	Page Tables
 *		Per the Pointer Tables, each page table entry points
 *	to the physical page in memory that supports the logical
 *	address that translates to the particular index.
 *
 *	In short, the Logical Address gets translated as follows:
 *		bits 31..26 - index into the Root Table
 *		bits 25..18 - index into the Pointer Table
 *		bits 17..12 - index into the Page Table
 *		bits 11..0  - offset into a particular 4K page
 *
 *	The algorithms which follows do one thing: they abstract
 *	the MMU hardware.  For example, there are three kinds of
 *	cache settings that are relevant.  Either, memory is
 *	being mapped in which case it is either Kernel Code (or
 *	the RamDisk) or it is MMU data.  On the 030, the MMU data
 *	option also describes the kernel.  Or, I/O is being mapped
 *	in which case it has its own kind of cache bits.  There
 *	are constants which abstract these notions from the code that
 *	actually makes the call to map some range of memory.
 *
 *
 *
 */

#ifdef MMU_PRINT
/*
 *	mmu_print
 *
 *	This algorithm will print out the current MMU mappings.
 *
 *	Input:
 *		%a5 points to the root table.  Everything else is calculated
 *			from this.
 */

#define mmu_next_valid		0
#define mmu_start_logical	4
#define mmu_next_logical	8
#define mmu_start_physical	12
#define mmu_next_physical	16

#define MMU_PRINT_INVALID		-1
#define MMU_PRINT_VALID			1
#define MMU_PRINT_UNINITED		0

#define putZc(z,n)		jbne 1f; putc z; jbra 2f; 1: putc n; 2:

func_start	mmu_print,%a0-%a6/%d0-%d7

	movel	%pc@(L(kernel_pgdir_ptr)),%a5
	lea	%pc@(L(mmu_print_data)),%a0
	movel	#MMU_PRINT_UNINITED,%a0@(mmu_next_valid)

	is_not_040_or_060(mmu_030_print)

mmu_040_print:
	puts	"\nMMU040\n"
	puts	"rp:"
	putn	%a5
	putc	'\n'
#if 0
	/*
	 * The following #if/#endif block is a tight algorithm for dumping the 040
	 * MMU Map in gory detail.  It really isn't that practical unless the
	 * MMU Map algorithm appears to go awry and you need to debug it at the
	 * entry per entry level.
	 */
	movel	#ROOT_TABLE_SIZE,%d5
#if 0
	movel	%a5@+,%d7		| Burn an entry to skip the kernel mappings,
	subql	#1,%d5			| they (might) work
#endif
1:	tstl	%d5
	jbeq	mmu_print_done
	subq	#1,%d5
	movel	%a5@+,%d7
	btst	#1,%d7
	jbeq	1b

2:	putn	%d7
	andil	#0xFFFFFE00,%d7
	movel	%d7,%a4
	movel	#PTR_TABLE_SIZE,%d4
	putc	' '
3:	tstl	%d4
	jbeq	11f
	subq	#1,%d4
	movel	%a4@+,%d7
	btst	#1,%d7
	jbeq	3b

4:	putn	%d7
	andil	#0xFFFFFF00,%d7
	movel	%d7,%a3
	movel	#PAGE_TABLE_SIZE,%d3
5:	movel	#8,%d2
6:	tstl	%d3
	jbeq	31f
	subq	#1,%d3
	movel	%a3@+,%d6
	btst	#0,%d6
	jbeq	6b
7:	tstl	%d2
	jbeq	8f
	subq	#1,%d2
	putc	' '
	jbra	91f
8:	putc	'\n'
	movel	#8+1+8+1+1,%d2
9:	putc	' '
	dbra	%d2,9b
	movel	#7,%d2
91:	putn	%d6
	jbra	6b

31:	putc	'\n'
	movel	#8+1,%d2
32:	putc	' '
	dbra	%d2,32b
	jbra	3b

11:	putc	'\n'
	jbra	1b
#endif /* MMU 040 Dumping code that's gory and detailed */

	lea	%pc@(kernel_pg_dir),%a5
	movel	%a5,%a0			/* a0 has the address of the root table ptr */
	movel	#0x00000000,%a4		/* logical address */
	moveql	#0,%d0
40:
	/* Increment the logical address and preserve in d5 */
	movel	%a4,%d5
	addil	#PAGESIZE<<13,%d5
	movel	%a0@+,%d6
	btst	#1,%d6
	jbne	41f
	jbsr	mmu_print_tuple_invalidate
	jbra	48f
41:
	movel	#0,%d1
	andil	#0xfffffe00,%d6
	movel	%d6,%a1
42:
	movel	%a4,%d5
	addil	#PAGESIZE<<6,%d5
	movel	%a1@+,%d6
	btst	#1,%d6
	jbne	43f
	jbsr	mmu_print_tuple_invalidate
	jbra	47f
43:
	movel	#0,%d2
	andil	#0xffffff00,%d6
	movel	%d6,%a2
44:
	movel	%a4,%d5
	addil	#PAGESIZE,%d5
	movel	%a2@+,%d6
	btst	#0,%d6
	jbne	45f
	jbsr	mmu_print_tuple_invalidate
	jbra	46f
45:
	moveml	%d0-%d1,%sp@-
	movel	%a4,%d0
	movel	%d6,%d1
	andil	#0xfffff4e0,%d1
	lea	%pc@(mmu_040_print_flags),%a6
	jbsr	mmu_print_tuple
	moveml	%sp@+,%d0-%d1
46:
	movel	%d5,%a4
	addq	#1,%d2
	cmpib	#64,%d2
	jbne	44b
47:
	movel	%d5,%a4
	addq	#1,%d1
	cmpib	#128,%d1
	jbne	42b
48:
	movel	%d5,%a4			/* move to the next logical address */
	addq	#1,%d0
	cmpib	#128,%d0
	jbne	40b

	.chip	68040
	movec	%dtt1,%d0
	movel	%d0,%d1
	andiw	#0x8000,%d1		/* is it valid ? */
	jbeq	1f			/* No, bail out */

	movel	%d0,%d1
	andil	#0xff000000,%d1		/* Get the address */
	putn	%d1
	puts	"=="
	putn	%d1

	movel	%d0,%d6
	jbsr	mmu_040_print_flags_tt
1:
	movec	%dtt0,%d0
	movel	%d0,%d1
	andiw	#0x8000,%d1		/* is it valid ? */
	jbeq	1f			/* No, bail out */

	movel	%d0,%d1
	andil	#0xff000000,%d1		/* Get the address */
	putn	%d1
	puts	"=="
	putn	%d1

	movel	%d0,%d6
	jbsr	mmu_040_print_flags_tt
1:
	.chip	68k

	jbra	mmu_print_done

mmu_040_print_flags:
	btstl	#10,%d6
	putZc(' ','G')	/* global bit */
	btstl	#7,%d6
	putZc(' ','S')	/* supervisor bit */
mmu_040_print_flags_tt:
	btstl	#6,%d6
	jbne	3f
	putc	'C'
	btstl	#5,%d6
	putZc('w','c')	/* write through or copy-back */
	jbra	4f
3:
	putc	'N'
	btstl	#5,%d6
	putZc('s',' ')	/* serialized non-cacheable, or non-cacheable */
4:
	rts

mmu_030_print_flags:
	btstl	#6,%d6
	putZc('C','I')	/* write through or copy-back */
	rts

mmu_030_print:
	puts	"\nMMU030\n"
	puts	"\nrp:"
	putn	%a5
	putc	'\n'
	movel	%a5,%d0
	andil	#0xfffffff0,%d0
	movel	%d0,%a0
	movel	#0x00000000,%a4		/* logical address */
	movel	#0,%d0
30:
	movel	%a4,%d5
	addil	#PAGESIZE<<13,%d5
	movel	%a0@+,%d6
	btst	#1,%d6			/* is it a table ptr? */
	jbne	31f			/* yes */
	btst	#0,%d6			/* is it early terminating? */
	jbeq	1f			/* no */
	jbsr	mmu_030_print_helper
	jbra	38f
1:
	jbsr	mmu_print_tuple_invalidate
	jbra	38f
31:
	movel	#0,%d1
	andil	#0xfffffff0,%d6
	movel	%d6,%a1
32:
	movel	%a4,%d5
	addil	#PAGESIZE<<6,%d5
	movel	%a1@+,%d6
	btst	#1,%d6			/* is it a table ptr? */
	jbne	33f			/* yes */
	btst	#0,%d6			/* is it a page descriptor? */
	jbeq	1f			/* no */
	jbsr	mmu_030_print_helper
	jbra	37f
1:
	jbsr	mmu_print_tuple_invalidate
	jbra	37f
33:
	movel	#0,%d2
	andil	#0xfffffff0,%d6
	movel	%d6,%a2
34:
	movel	%a4,%d5
	addil	#PAGESIZE,%d5
	movel	%a2@+,%d6
	btst	#0,%d6
	jbne	35f
	jbsr	mmu_print_tuple_invalidate
	jbra	36f
35:
	jbsr	mmu_030_print_helper
36:
	movel	%d5,%a4
	addq	#1,%d2
	cmpib	#64,%d2
	jbne	34b
37:
	movel	%d5,%a4
	addq	#1,%d1
	cmpib	#128,%d1
	jbne	32b
38:
	movel	%d5,%a4			/* move to the next logical address */
	addq	#1,%d0
	cmpib	#128,%d0
	jbne	30b

mmu_print_done:
	puts	"\n\n"

func_return	mmu_print


mmu_030_print_helper:
	moveml	%d0-%d1,%sp@-
	movel	%a4,%d0
	movel	%d6,%d1
	lea	%pc@(mmu_030_print_flags),%a6
	jbsr	mmu_print_tuple
	moveml	%sp@+,%d0-%d1
	rts

mmu_print_tuple_invalidate:
	moveml	%a0/%d7,%sp@-

	lea	%pc@(L(mmu_print_data)),%a0
	tstl	%a0@(mmu_next_valid)
	jbmi	mmu_print_tuple_invalidate_exit

	movel	#MMU_PRINT_INVALID,%a0@(mmu_next_valid)

	putn	%a4

	puts	"##\n"

mmu_print_tuple_invalidate_exit:
	moveml	%sp@+,%a0/%d7
	rts


mmu_print_tuple:
	moveml	%d0-%d7/%a0,%sp@-

	lea	%pc@(L(mmu_print_data)),%a0

	tstl	%a0@(mmu_next_valid)
	jble	mmu_print_tuple_print

	cmpl	%a0@(mmu_next_physical),%d1
	jbeq	mmu_print_tuple_increment

mmu_print_tuple_print:
	putn	%d0
	puts	"->"
	putn	%d1

	movel	%d1,%d6
	jbsr	%a6@

mmu_print_tuple_record:
	movel	#MMU_PRINT_VALID,%a0@(mmu_next_valid)

	movel	%d1,%a0@(mmu_next_physical)

mmu_print_tuple_increment:
	movel	%d5,%d7
	subl	%a4,%d7
	addl	%d7,%a0@(mmu_next_physical)

mmu_print_tuple_exit:
	moveml	%sp@+,%d0-%d7/%a0
	rts

mmu_print_machine_cpu_types:
	puts	"machine: "

	is_not_amiga(1f)
	puts	"amiga"
	jbra	9f
1:
	is_not_atari(2f)
	puts	"atari"
	jbra	9f
2:
	is_not_mac(3f)
	puts	"macintosh"
	jbra	9f
3:	puts	"unknown"
9:	putc	'\n'

	puts	"cputype: 0"
	is_not_060(1f)
	putc	'6'
	jbra	9f
1:
	is_not_040_or_060(2f)
	putc	'4'
	jbra	9f
2:	putc	'3'
9:	putc	'0'
	putc	'\n'

	rts
#endif /* MMU_PRINT */

/*
 * mmu_map_tt
 *
 * This is a specific function which works on all 680x0 machines.
 * On 030, 040 & 060 it will attempt to use Transparent Translation
 * registers (tt1).
 * On 020 it will call the standard mmu_map which will use early
 * terminating descriptors.
 */
func_start	mmu_map_tt,%d0/%d1/%a0,4

	dputs	"mmu_map_tt:"
	dputn	ARG1
	dputn	ARG2
	dputn	ARG3
	dputn	ARG4
	dputc	'\n'

	is_020(L(do_map))

	/* Extract the highest bit set
	 */
	bfffo	ARG3{#0,#32},%d1
	cmpw	#8,%d1
	jcc	L(do_map)

	/* And get the mask
	 */
	moveq	#-1,%d0
	lsrl	%d1,%d0
	lsrl	#1,%d0

	/* Mask the address
	 */
	movel	%d0,%d1
	notl	%d1
	andl	ARG2,%d1

	/* Generate the upper 16bit of the tt register
	 */
	lsrl	#8,%d0
	orl	%d0,%d1
	clrw	%d1

	is_040_or_060(L(mmu_map_tt_040))

	/* set 030 specific bits (read/write access for supervisor mode
	 * (highest function code set, lower two bits masked))
	 */
	orw	#TTR_ENABLE+TTR_RWM+TTR_FCB2+TTR_FCM1+TTR_FCM0,%d1
	movel	ARG4,%d0
	btst	#6,%d0
	jeq	1f
	orw	#TTR_CI,%d1

1:	lea	STACK,%a0
	dputn	%d1
	movel	%d1,%a0@
	.chip	68030
	tstl	ARG1
	jne	1f
	pmove	%a0@,%tt0
	jra	2f
1:	pmove	%a0@,%tt1
2:	.chip	68k
	jra	L(mmu_map_tt_done)

	/* set 040 specific bits
	 */
L(mmu_map_tt_040):
	orw	#TTR_ENABLE+TTR_KERNELMODE,%d1
	orl	ARG4,%d1
	dputn	%d1

	.chip	68040
	tstl	ARG1
	jne	1f
	movec	%d1,%itt0
	movec	%d1,%dtt0
	jra	2f
1:	movec	%d1,%itt1
	movec	%d1,%dtt1
2:	.chip	68k

	jra	L(mmu_map_tt_done)

L(do_map):
	mmu_map_eq	ARG2,ARG3,ARG4

L(mmu_map_tt_done):

func_return	mmu_map_tt

/*
 *	mmu_map
 *
 *	This routine will map a range of memory using a pointer
 *	table and allocating the pages on the fly from the kernel.
 *	The pointer table does not have to be already linked into
 *	the root table, this routine will do that if necessary.
 *
 *	NOTE
 *	This routine will assert failure and use the serial_putc
 *	routines in the case of a run-time error.  For example,
 *	if the address is already mapped.
 *
 *	NOTE-2
 *	This routine will use early terminating descriptors
 *	where possible for the 68020+68851 and 68030 type
 *	processors.
 */
func_start	mmu_map,%d0-%d4/%a0-%a4

	dputs	"\nmmu_map:"
	dputn	ARG1
	dputn	ARG2
	dputn	ARG3
	dputn	ARG4
	dputc	'\n'

	/* Get logical address and round it down to 256KB
	 */
	movel	ARG1,%d0
	andl	#-(PAGESIZE*PAGE_TABLE_SIZE),%d0
	movel	%d0,%a3

	/* Get the end address
	 */
	movel	ARG1,%a4
	addl	ARG3,%a4
	subql	#1,%a4

	/* Get physical address and round it down to 256KB
	 */
	movel	ARG2,%d0
	andl	#-(PAGESIZE*PAGE_TABLE_SIZE),%d0
	movel	%d0,%a2

	/* Add page attributes to the physical address
	 */
	movel	ARG4,%d0
	orw	#_PAGE_PRESENT+_PAGE_ACCESSED+_PAGE_DIRTY,%d0
	addw	%d0,%a2

	dputn	%a2
	dputn	%a3
	dputn	%a4

	is_not_040_or_060(L(mmu_map_030))

	addw	#_PAGE_GLOBAL040,%a2
/*
 *	MMU 040 & 060 Support
 *
 *	The MMU usage for the 040 and 060 is different enough from
 *	the 030 and 68851 that there is separate code.  This comment
 *	block describes the data structures and algorithms built by
 *	this code.
 *
 *	The 040 does not support early terminating descriptors, as
 *	the 030 does.  Therefore, a third level of table is needed
 *	for the 040, and that would be the page table.  In Linux,
 *	page tables are allocated directly from the memory above the
 *	kernel.
 *
 */

L(mmu_map_040):
	/* Calculate the offset into the root table
	 */
	movel	%a3,%d0
	moveq	#ROOT_INDEX_SHIFT,%d1
	lsrl	%d1,%d0
	mmu_get_root_table_entry	%d0

	/* Calculate the offset into the pointer table
	 */
	movel	%a3,%d0
	moveq	#PTR_INDEX_SHIFT,%d1
	lsrl	%d1,%d0
	andl	#PTR_TABLE_SIZE-1,%d0
	mmu_get_ptr_table_entry		%a0,%d0

	/* Calculate the offset into the page table
	 */
	movel	%a3,%d0
	moveq	#PAGE_INDEX_SHIFT,%d1
	lsrl	%d1,%d0
	andl	#PAGE_TABLE_SIZE-1,%d0
	mmu_get_page_table_entry	%a0,%d0

	/* The page table entry must not no be busy
	 */
	tstl	%a0@
	jne	L(mmu_map_error)

	/* Do the mapping and advance the pointers
	 */
	movel	%a2,%a0@
2:
	addw	#PAGESIZE,%a2
	addw	#PAGESIZE,%a3

	/* Ready with mapping?
	 */
	lea	%a3@(-1),%a0
	cmpl	%a0,%a4
	jhi	L(mmu_map_040)
	jra	L(mmu_map_done)

L(mmu_map_030):
	/* Calculate the offset into the root table
	 */
	movel	%a3,%d0
	moveq	#ROOT_INDEX_SHIFT,%d1
	lsrl	%d1,%d0
	mmu_get_root_table_entry	%d0

	/* Check if logical address 32MB aligned,
	 * so we can try to map it once
	 */
	movel	%a3,%d0
	andl	#(PTR_TABLE_SIZE*PAGE_TABLE_SIZE*PAGESIZE-1)&(-ROOT_TABLE_SIZE),%d0
	jne	1f

	/* Is there enough to map for 32MB at once
	 */
	lea	%a3@(PTR_TABLE_SIZE*PAGE_TABLE_SIZE*PAGESIZE-1),%a1
	cmpl	%a1,%a4
	jcs	1f

	addql	#1,%a1

	/* The root table entry must not no be busy
	 */
	tstl	%a0@
	jne	L(mmu_map_error)

	/* Do the mapping and advance the pointers
	 */
	dputs	"early term1"
	dputn	%a2
	dputn	%a3
	dputn	%a1
	dputc	'\n'
	movel	%a2,%a0@

	movel	%a1,%a3
	lea	%a2@(PTR_TABLE_SIZE*PAGE_TABLE_SIZE*PAGESIZE),%a2
	jra	L(mmu_mapnext_030)
1:
	/* Calculate the offset into the pointer table
	 */
	movel	%a3,%d0
	moveq	#PTR_INDEX_SHIFT,%d1
	lsrl	%d1,%d0
	andl	#PTR_TABLE_SIZE-1,%d0
	mmu_get_ptr_table_entry		%a0,%d0

	/* The pointer table entry must not no be busy
	 */
	tstl	%a0@
	jne	L(mmu_map_error)

	/* Do the mapping and advance the pointers
	 */
	dputs	"early term2"
	dputn	%a2
	dputn	%a3
	dputc	'\n'
	movel	%a2,%a0@

	addl	#PAGE_TABLE_SIZE*PAGESIZE,%a2
	addl	#PAGE_TABLE_SIZE*PAGESIZE,%a3

L(mmu_mapnext_030):
	/* Ready with mapping?
	 */
	lea	%a3@(-1),%a0
	cmpl	%a0,%a4
	jhi	L(mmu_map_030)
	jra	L(mmu_map_done)

L(mmu_map_error):

	dputs	"mmu_map error:"
	dputn	%a2
	dputn	%a3
	dputc	'\n'

L(mmu_map_done):

func_return	mmu_map

/*
 *	mmu_fixup
 *
 *	On the 040 class machines, all pages that are used for the
 *	mmu have to be fixed up.
 */

func_start	mmu_fixup_page_mmu_cache,%d0/%a0

	dputs	"mmu_fixup_page_mmu_cache"
	dputn	ARG1

	/* Calculate the offset into the root table
	 */
	movel	ARG1,%d0
	moveq	#ROOT_INDEX_SHIFT,%d1
	lsrl	%d1,%d0
	mmu_get_root_table_entry	%d0

	/* Calculate the offset into the pointer table
	 */
	movel	ARG1,%d0
	moveq	#PTR_INDEX_SHIFT,%d1
	lsrl	%d1,%d0
	andl	#PTR_TABLE_SIZE-1,%d0
	mmu_get_ptr_table_entry		%a0,%d0

	/* Calculate the offset into the page table
	 */
	movel	ARG1,%d0
	moveq	#PAGE_INDEX_SHIFT,%d1
	lsrl	%d1,%d0
	andl	#PAGE_TABLE_SIZE-1,%d0
	mmu_get_page_table_entry	%a0,%d0

	movel	%a0@,%d0
	andil	#_CACHEMASK040,%d0
	orl	%pc@(m68k_pgtable_cachemode),%d0
	movel	%d0,%a0@

	dputc	'\n'

func_return	mmu_fixup_page_mmu_cache

/*
 *	mmu_temp_map
 *
 *	create a temporary mapping to enable the mmu,
 *	this we don't need any transparation translation tricks.
 */

func_start	mmu_temp_map,%d0/%d1/%a0/%a1

	dputs	"mmu_temp_map"
	dputn	ARG1
	dputn	ARG2
	dputc	'\n'

	lea	%pc@(L(temp_mmap_mem)),%a1

	/* Calculate the offset in the root table
	 */
	movel	ARG2,%d0
	moveq	#ROOT_INDEX_SHIFT,%d1
	lsrl	%d1,%d0
	mmu_get_root_table_entry	%d0

	/* Check if the table is temporary allocated, so we have to reuse it
	 */
	movel	%a0@,%d0
	cmpl	%pc@(L(memory_start)),%d0
	jcc	1f

	/* Temporary allocate a ptr table and insert it into the root table
	 */
	movel	%a1@,%d0
	addl	#PTR_TABLE_SIZE*4,%a1@
	orw	#_PAGE_TABLE+_PAGE_ACCESSED,%d0
	movel	%d0,%a0@
	dputs	" (new)"
1:
	dputn	%d0
	/* Mask the root table entry for the ptr table
	 */
	andw	#-ROOT_TABLE_SIZE,%d0
	movel	%d0,%a0

	/* Calculate the offset into the pointer table
	 */
	movel	ARG2,%d0
	moveq	#PTR_INDEX_SHIFT,%d1
	lsrl	%d1,%d0
	andl	#PTR_TABLE_SIZE-1,%d0
	lea	%a0@(%d0*4),%a0
	dputn	%a0

	/* Check if a temporary page table is already allocated
	 */
	movel	%a0@,%d0
	jne	1f

	/* Temporary allocate a page table and insert it into the ptr table
	 */
	movel	%a1@,%d0
	/* The 512 should be PAGE_TABLE_SIZE*4, but that violates the
	   alignment restriction for pointer tables on the '0[46]0.  */
	addl	#512,%a1@
	orw	#_PAGE_TABLE+_PAGE_ACCESSED,%d0
	movel	%d0,%a0@
	dputs	" (new)"
1:
	dputn	%d0
	/* Mask the ptr table entry for the page table
	 */
	andw	#-PTR_TABLE_SIZE,%d0
	movel	%d0,%a0

	/* Calculate the offset into the page table
	 */
	movel	ARG2,%d0
	moveq	#PAGE_INDEX_SHIFT,%d1
	lsrl	%d1,%d0
	andl	#PAGE_TABLE_SIZE-1,%d0
	lea	%a0@(%d0*4),%a0
	dputn	%a0

	/* Insert the address into the page table
	 */
	movel	ARG1,%d0
	andw	#-PAGESIZE,%d0
	orw	#_PAGE_PRESENT+_PAGE_ACCESSED+_PAGE_DIRTY,%d0
	movel	%d0,%a0@
	dputn	%d0

	dputc	'\n'

func_return	mmu_temp_map

func_start	mmu_engage,%d0-%d2/%a0-%a3

	moveq	#ROOT_TABLE_SIZE-1,%d0
	/* Temporarily use a different root table.  */
	lea	%pc@(L(kernel_pgdir_ptr)),%a0
	movel	%a0@,%a2
	movel	%pc@(L(memory_start)),%a1
	movel	%a1,%a0@
	movel	%a2,%a0
1:
	movel	%a0@+,%a1@+
	dbra	%d0,1b

	lea	%pc@(L(temp_mmap_mem)),%a0
	movel	%a1,%a0@

	movew	#PAGESIZE-1,%d0
1:
	clrl	%a1@+
	dbra	%d0,1b

	lea	%pc@(1b),%a0
	movel	#1b,%a1
	/* Skip temp mappings if phys == virt */
	cmpl	%a0,%a1
	jeq	1f

	mmu_temp_map	%a0,%a0
	mmu_temp_map	%a0,%a1

	addw	#PAGESIZE,%a0
	addw	#PAGESIZE,%a1
	mmu_temp_map	%a0,%a0
	mmu_temp_map	%a0,%a1
1:
	movel	%pc@(L(memory_start)),%a3
	movel	%pc@(L(phys_kernel_start)),%d2

	is_not_040_or_060(L(mmu_engage_030))

L(mmu_engage_040):
	.chip	68040
	nop
	cinva	%bc
	nop
	pflusha
	nop
	movec	%a3,%srp
	movel	#TC_ENABLE+TC_PAGE4K,%d0
	movec	%d0,%tc		/* enable the MMU */
	jmp	1f:l
1:	nop
	movec	%a2,%srp
	nop
	cinva	%bc
	nop
	pflusha
	.chip	68k
	jra	L(mmu_engage_cleanup)

L(mmu_engage_030_temp):
	.space	12
L(mmu_engage_030):
	.chip	68030
	lea	%pc@(L(mmu_engage_030_temp)),%a0
	movel	#0x80000002,%a0@
	movel	%a3,%a0@(4)
	movel	#0x0808,%d0
	movec	%d0,%cacr
	pmove	%a0@,%srp
	pflusha
	/*
	 * enable,super root enable,4096 byte pages,7 bit root index,
	 * 7 bit pointer index, 6 bit page table index.
	 */
	movel	#0x82c07760,%a0@(8)
	pmove	%a0@(8),%tc	/* enable the MMU */
	jmp	1f:l
1:	movel	%a2,%a0@(4)
	movel	#0x0808,%d0
	movec	%d0,%cacr
	pmove	%a0@,%srp
	pflusha
	.chip	68k

L(mmu_engage_cleanup):
	subl	#PAGE_OFFSET,%d2
	subl	%d2,%a2
	movel	%a2,L(kernel_pgdir_ptr)
	subl	%d2,%fp
	subl	%d2,%sp
	subl	%d2,ARG0

func_return	mmu_engage

func_start	mmu_get_root_table_entry,%d0/%a1

#if 0
	dputs	"mmu_get_root_table_entry:"
	dputn	ARG1
	dputs	" ="
#endif

	movel	%pc@(L(kernel_pgdir_ptr)),%a0
	tstl	%a0
	jne	2f

	dputs	"\nmmu_init:"

	/* Find the start of free memory, get_bi_record does this for us,
	 * as the bootinfo structure is located directly behind the kernel
	 * and and we simply search for the last entry.
	 */
	get_bi_record	BI_LAST
	addw	#PAGESIZE-1,%a0
	movel	%a0,%d0
	andw	#-PAGESIZE,%d0

	dputn	%d0

	lea	%pc@(L(memory_start)),%a0
	movel	%d0,%a0@
	lea	%pc@(L(kernel_end)),%a0
	movel	%d0,%a0@

	/* we have to return the first page at _stext since the init code
	 * in mm/init.c simply expects kernel_pg_dir there, the rest of
	 * page is used for further ptr tables in get_ptr_table.
	 */
	lea	%pc@(_stext),%a0
	lea	%pc@(L(mmu_cached_pointer_tables)),%a1
	movel	%a0,%a1@
	addl	#ROOT_TABLE_SIZE*4,%a1@

	lea	%pc@(L(mmu_num_pointer_tables)),%a1
	addql	#1,%a1@

	/* clear the page
	 */
	movel	%a0,%a1
	movew	#PAGESIZE/4-1,%d0
1:
	clrl	%a1@+
	dbra	%d0,1b

	lea	%pc@(L(kernel_pgdir_ptr)),%a1
	movel	%a0,%a1@

	dputn	%a0
	dputc	'\n'
2:
	movel	ARG1,%d0
	lea	%a0@(%d0*4),%a0

#if 0
	dputn	%a0
	dputc	'\n'
#endif

func_return	mmu_get_root_table_entry



func_start	mmu_get_ptr_table_entry,%d0/%a1

#if 0
	dputs	"mmu_get_ptr_table_entry:"
	dputn	ARG1
	dputn	ARG2
	dputs	" ="
#endif

	movel	ARG1,%a0
	movel	%a0@,%d0
	jne	2f

	/* Keep track of the number of pointer tables we use
	 */
	dputs	"\nmmu_get_new_ptr_table:"
	lea	%pc@(L(mmu_num_pointer_tables)),%a0
	movel	%a0@,%d0
	addql	#1,%a0@

	/* See if there is a free pointer table in our cache of pointer tables
	 */
	lea	%pc@(L(mmu_cached_pointer_tables)),%a1
	andw	#7,%d0
	jne	1f

	/* Get a new pointer table page from above the kernel memory
	 */
	get_new_page
	movel	%a0,%a1@
1:
	/* There is an unused pointer table in our cache... use it
	 */
	movel	%a1@,%d0
	addl	#PTR_TABLE_SIZE*4,%a1@

	dputn	%d0
	dputc	'\n'

	/* Insert the new pointer table into the root table
	 */
	movel	ARG1,%a0
	orw	#_PAGE_TABLE+_PAGE_ACCESSED,%d0
	movel	%d0,%a0@
2:
	/* Extract the pointer table entry
	 */
	andw	#-PTR_TABLE_SIZE,%d0
	movel	%d0,%a0
	movel	ARG2,%d0
	lea	%a0@(%d0*4),%a0

#if 0
	dputn	%a0
	dputc	'\n'
#endif

func_return	mmu_get_ptr_table_entry


func_start	mmu_get_page_table_entry,%d0/%a1

#if 0
	dputs	"mmu_get_page_table_entry:"
	dputn	ARG1
	dputn	ARG2
	dputs	" ="
#endif

	movel	ARG1,%a0
	movel	%a0@,%d0
	jne	2f

	/* If the page table entry doesn't exist, we allocate a complete new
	 * page and use it as one continues big page table which can cover
	 * 4MB of memory, nearly almost all mappings have that alignment.
	 */
	get_new_page
	addw	#_PAGE_TABLE+_PAGE_ACCESSED,%a0

	/* align pointer table entry for a page of page tables
	 */
	movel	ARG1,%d0
	andw	#-(PAGESIZE/PAGE_TABLE_SIZE),%d0
	movel	%d0,%a1

	/* Insert the page tables into the pointer entries
	 */
	moveq	#PAGESIZE/PAGE_TABLE_SIZE/4-1,%d0
1:
	movel	%a0,%a1@+
	lea	%a0@(PAGE_TABLE_SIZE*4),%a0
	dbra	%d0,1b

	/* Now we can get the initialized pointer table entry
	 */
	movel	ARG1,%a0
	movel	%a0@,%d0
2:
	/* Extract the page table entry
	 */
	andw	#-PAGE_TABLE_SIZE,%d0
	movel	%d0,%a0
	movel	ARG2,%d0
	lea	%a0@(%d0*4),%a0

#if 0
	dputn	%a0
	dputc	'\n'
#endif

func_return	mmu_get_page_table_entry

/*
 *	get_new_page
 *
 *	Return a new page from the memory start and clear it.
 */
func_start	get_new_page,%d0/%a1

	dputs	"\nget_new_page:"

	/* allocate the page and adjust memory_start
	 */
	lea	%pc@(L(memory_start)),%a0
	movel	%a0@,%a1
	addl	#PAGESIZE,%a0@

	/* clear the new page
	 */
	movel	%a1,%a0
	movew	#PAGESIZE/4-1,%d0
1:
	clrl	%a1@+
	dbra	%d0,1b

	dputn	%a0
	dputc	'\n'

func_return	get_new_page



/*
 * Debug output support
 * Atarians have a choice between the parallel port, the serial port
 * from the MFP or a serial port of the SCC
 */

#ifdef CONFIG_MAC

L(scc_initable_mac):
	.byte	9,12		/* Reset */
	.byte	4,0x44		/* x16, 1 stopbit, no parity */
	.byte	3,0xc0		/* receiver: 8 bpc */
	.byte	5,0xe2		/* transmitter: 8 bpc, assert dtr/rts */
	.byte	9,0		/* no interrupts */
	.byte	10,0		/* NRZ */
	.byte	11,0x50		/* use baud rate generator */
	.byte	12,10,13,0	/* 9600 baud */
	.byte	14,1		/* Baud rate generator enable */
	.byte	3,0xc1		/* enable receiver */
	.byte	5,0xea		/* enable transmitter */
	.byte	-1
	.even
#endif

#ifdef CONFIG_ATARI
/* #define USE_PRINTER */
/* #define USE_SCC_B */
/* #define USE_SCC_A */
#define USE_MFP

#if defined(USE_SCC_A) || defined(USE_SCC_B)
#define USE_SCC
/* Initialisation table for SCC */
L(scc_initable):
	.byte	9,12		/* Reset */
	.byte	4,0x44		/* x16, 1 stopbit, no parity */
	.byte	3,0xc0		/* receiver: 8 bpc */
	.byte	5,0xe2		/* transmitter: 8 bpc, assert dtr/rts */
	.byte	9,0		/* no interrupts */
	.byte	10,0		/* NRZ */
	.byte	11,0x50		/* use baud rate generator */
	.byte	12,24,13,0	/* 9600 baud */
	.byte	14,2,14,3	/* use master clock for BRG, enable */
	.byte	3,0xc1		/* enable receiver */
	.byte	5,0xea		/* enable transmitter */
	.byte	-1
	.even
#endif

#ifdef USE_PRINTER

LPSG_SELECT	= 0xff8800
LPSG_READ	= 0xff8800
LPSG_WRITE	= 0xff8802
LPSG_IO_A	= 14
LPSG_IO_B	= 15
LPSG_CONTROL	= 7
LSTMFP_GPIP	= 0xfffa01
LSTMFP_DDR	= 0xfffa05
LSTMFP_IERB	= 0xfffa09

#elif defined(USE_SCC_B)

LSCC_CTRL	= 0xff8c85
LSCC_DATA	= 0xff8c87

#elif defined(USE_SCC_A)

LSCC_CTRL	= 0xff8c81
LSCC_DATA	= 0xff8c83

#elif defined(USE_MFP)

LMFP_UCR     = 0xfffa29
LMFP_TDCDR   = 0xfffa1d
LMFP_TDDR    = 0xfffa25
LMFP_TSR     = 0xfffa2d
LMFP_UDR     = 0xfffa2f

#endif
#endif	/* CONFIG_ATARI */

/*
 * Serial port output support.
 */

/*
 * Initialize serial port hardware for 9600/8/1
 */
func_start	serial_init,%d0/%d1/%a0/%a1
	/*
	 *	Some of the register usage that follows
	 *	CONFIG_AMIGA
	 *		a0 = pointer to boot info record
	 *		d0 = boot info offset
	 *	CONFIG_ATARI
	 *		a0 = address of SCC
	 *		a1 = Liobase address/address of scc_initable
	 *		d0 = init data for serial port
	 *	CONFIG_MAC
	 *		a0 = address of SCC
	 *		a1 = address of scc_initable_mac
	 *		d0 = init data for serial port
	 */

#ifdef CONFIG_AMIGA
#define SERIAL_DTR	7
#define SERIAL_CNTRL	CIABBASE+C_PRA

	is_not_amiga(1f)
	lea	%pc@(L(custom)),%a0
	movel	#-ZTWOBASE,%a0@
	bclr	#SERIAL_DTR,SERIAL_CNTRL-ZTWOBASE
	get_bi_record	BI_AMIGA_SERPER
	movew	%a0@,CUSTOMBASE+C_SERPER-ZTWOBASE
|	movew	#61,CUSTOMBASE+C_SERPER-ZTWOBASE
1:
#endif
#ifdef CONFIG_ATARI
	is_not_atari(4f)
	movel	%pc@(L(iobase)),%a1
#if defined(USE_PRINTER)
	bclr	#0,%a1@(LSTMFP_IERB)
	bclr	#0,%a1@(LSTMFP_DDR)
	moveb	#LPSG_CONTROL,%a1@(LPSG_SELECT)
	moveb	#0xff,%a1@(LPSG_WRITE)
	moveb	#LPSG_IO_B,%a1@(LPSG_SELECT)
	clrb	%a1@(LPSG_WRITE)
	moveb	#LPSG_IO_A,%a1@(LPSG_SELECT)
	moveb	%a1@(LPSG_READ),%d0
	bset	#5,%d0
	moveb	%d0,%a1@(LPSG_WRITE)
#elif defined(USE_SCC)
	lea	%a1@(LSCC_CTRL),%a0
	lea	%pc@(L(scc_initable)),%a1
2:	moveb	%a1@+,%d0
	jmi	3f
	moveb	%d0,%a0@
	moveb	%a1@+,%a0@
	jra	2b
3:	clrb	%a0@
#elif defined(USE_MFP)
	bclr	#1,%a1@(LMFP_TSR)
	moveb   #0x88,%a1@(LMFP_UCR)
	andb	#0x70,%a1@(LMFP_TDCDR)
	moveb   #2,%a1@(LMFP_TDDR)
	orb	#1,%a1@(LMFP_TDCDR)
	bset	#1,%a1@(LMFP_TSR)
#endif
	jra	L(serial_init_done)
4:
#endif
#ifdef CONFIG_MAC
	is_not_mac(L(serial_init_not_mac))
#ifdef MAC_SERIAL_DEBUG
#if !defined(MAC_USE_SCC_A) && !defined(MAC_USE_SCC_B)
#define MAC_USE_SCC_B
#endif
#define mac_scc_cha_b_ctrl_offset	0x0
#define mac_scc_cha_a_ctrl_offset	0x2
#define mac_scc_cha_b_data_offset	0x4
#define mac_scc_cha_a_data_offset	0x6

#ifdef MAC_USE_SCC_A
	/* Initialize channel A */
	movel	%pc@(L(mac_sccbase)),%a0
	lea	%pc@(L(scc_initable_mac)),%a1
5:	moveb	%a1@+,%d0
	jmi	6f
	moveb	%d0,%a0@(mac_scc_cha_a_ctrl_offset)
	moveb	%a1@+,%a0@(mac_scc_cha_a_ctrl_offset)
	jra	5b
6:
#endif	/* MAC_USE_SCC_A */

#ifdef MAC_USE_SCC_B
	/* Initialize channel B */
#ifndef MAC_USE_SCC_A	/* Load mac_sccbase only if needed */
	movel	%pc@(L(mac_sccbase)),%a0
#endif	/* MAC_USE_SCC_A */
	lea	%pc@(L(scc_initable_mac)),%a1
7:	moveb	%a1@+,%d0
	jmi	8f
	moveb	%d0,%a0@(mac_scc_cha_b_ctrl_offset)
	moveb	%a1@+,%a0@(mac_scc_cha_b_ctrl_offset)
	jra	7b
8:
#endif	/* MAC_USE_SCC_B */
#endif	/* MAC_SERIAL_DEBUG */

	jra	L(serial_init_done)
L(serial_init_not_mac):
#endif	/* CONFIG_MAC */

#ifdef CONFIG_Q40
	is_not_q40(2f)
/* debug output goes into SRAM, so we don't do it unless requested
   - check for '%LX$' signature in SRAM   */
	lea	%pc@(q40_mem_cptr),%a1
	move.l	#0xff020010,%a1@  /* must be inited - also used by debug=mem */
	move.l	#0xff020000,%a1
	cmp.b	#'%',%a1@
	bne	2f	/*nodbg*/
	addq.w	#4,%a1
	cmp.b	#'L',%a1@
	bne	2f	/*nodbg*/
	addq.w	#4,%a1
	cmp.b	#'X',%a1@
	bne	2f	/*nodbg*/
	addq.w	#4,%a1
	cmp.b	#'$',%a1@
	bne	2f	/*nodbg*/
	/* signature OK */
	lea	%pc@(L(q40_do_debug)),%a1
	tas	%a1@
/*nodbg: q40_do_debug is 0 by default*/
2:
#endif

#ifdef CONFIG_APOLLO
/* We count on the PROM initializing SIO1 */
#endif

#ifdef CONFIG_HP300
/* We count on the boot loader initialising the UART */
#endif

L(serial_init_done):
func_return	serial_init

/*
 * Output character on serial port.
 */
func_start	serial_putc,%d0/%d1/%a0/%a1

	movel	ARG1,%d0
	cmpib	#'\n',%d0
	jbne	1f

	/* A little safe recursion is good for the soul */
	serial_putc	#'\r'
1:

#ifdef CONFIG_AMIGA
	is_not_amiga(2f)
	andw	#0x00ff,%d0
	oriw	#0x0100,%d0
	movel	%pc@(L(custom)),%a0
	movew	%d0,%a0@(CUSTOMBASE+C_SERDAT)
1:	movew	%a0@(CUSTOMBASE+C_SERDATR),%d0
	andw	#0x2000,%d0
	jeq	1b
	jra	L(serial_putc_done)
2:
#endif

#ifdef CONFIG_MAC
	is_not_mac(5f)

#ifdef MAC_SERIAL_DEBUG

#ifdef MAC_USE_SCC_A
	movel	%pc@(L(mac_sccbase)),%a1
3:	btst	#2,%a1@(mac_scc_cha_a_ctrl_offset)
	jeq	3b
	moveb	%d0,%a1@(mac_scc_cha_a_data_offset)
#endif	/* MAC_USE_SCC_A */

#ifdef MAC_USE_SCC_B
#ifndef MAC_USE_SCC_A	/* Load mac_sccbase only if needed */
	movel	%pc@(L(mac_sccbase)),%a1
#endif	/* MAC_USE_SCC_A */
4:	btst	#2,%a1@(mac_scc_cha_b_ctrl_offset)
	jeq	4b
	moveb	%d0,%a1@(mac_scc_cha_b_data_offset)
#endif	/* MAC_USE_SCC_B */

#endif	/* MAC_SERIAL_DEBUG */

	jra	L(serial_putc_done)
5:
#endif	/* CONFIG_MAC */

#ifdef CONFIG_ATARI
	is_not_atari(4f)
	movel	%pc@(L(iobase)),%a1
#if defined(USE_PRINTER)
3:	btst	#0,%a1@(LSTMFP_GPIP)
	jne	3b
	moveb	#LPSG_IO_B,%a1@(LPSG_SELECT)
	moveb	%d0,%a1@(LPSG_WRITE)
	moveb	#LPSG_IO_A,%a1@(LPSG_SELECT)
	moveb	%a1@(LPSG_READ),%d0
	bclr	#5,%d0
	moveb	%d0,%a1@(LPSG_WRITE)
	nop
	nop
	bset	#5,%d0
	moveb	%d0,%a1@(LPSG_WRITE)
#elif defined(USE_SCC)
3:	btst	#2,%a1@(LSCC_CTRL)
	jeq	3b
	moveb	%d0,%a1@(LSCC_DATA)
#elif defined(USE_MFP)
3:	btst	#7,%a1@(LMFP_TSR)
	jeq	3b
	moveb	%d0,%a1@(LMFP_UDR)
#endif
	jra	L(serial_putc_done)
4:
#endif	/* CONFIG_ATARI */

#ifdef CONFIG_MVME147
	is_not_mvme147(2f)
1:	btst	#2,M147_SCC_CTRL_A
	jeq	1b
	moveb	%d0,M147_SCC_DATA_A
	jbra	L(serial_putc_done)
2:
#endif

#ifdef CONFIG_MVME16x
	is_not_mvme16x(2f)
	/*
	 * If the loader gave us a board type then we can use that to
	 * select an appropriate output routine; otherwise we just use
	 * the Bug code.  If we haev to use the Bug that means the Bug
	 * workspace has to be valid, which means the Bug has to use
	 * the SRAM, which is non-standard.
	 */
	moveml	%d0-%d7/%a2-%a6,%sp@-
	movel	vme_brdtype,%d1
	jeq	1f			| No tag - use the Bug
	cmpi	#VME_TYPE_MVME162,%d1
	jeq	6f
	cmpi	#VME_TYPE_MVME172,%d1
	jne	5f
	/* 162/172; it's an SCC */
6:	btst	#2,M162_SCC_CTRL_A
	nop
	nop
	nop
	jeq	6b
	moveb	#8,M162_SCC_CTRL_A
	nop
	nop
	nop
	moveb	%d0,M162_SCC_CTRL_A
	jra	3f
5:
	/* 166/167/177; it's a CD2401 */
	moveb	#0,M167_CYCAR
	moveb	M167_CYIER,%d2
	moveb	#0x02,M167_CYIER
7:
	btst	#5,M167_PCSCCTICR
	jeq	7b
	moveb	M167_PCTPIACKR,%d1
	moveb	M167_CYLICR,%d1
	jeq	8f
	moveb	#0x08,M167_CYTEOIR
	jra	7b
8:
	moveb	%d0,M167_CYTDR
	moveb	#0,M167_CYTEOIR
	moveb	%d2,M167_CYIER
	jra	3f
1:
	moveb	%d0,%sp@-
	trap	#15
	.word	0x0020	/* TRAP 0x020 */
3:
	moveml	%sp@+,%d0-%d7/%a2-%a6
	jbra	L(serial_putc_done)
2:
#endif /* CONFIG_MVME16x */

#ifdef CONFIG_BVME6000
	is_not_bvme6000(2f)
	/*
	 * The BVME6000 machine has a serial port ...
	 */
1:	btst	#2,BVME_SCC_CTRL_A
	jeq	1b
	moveb	%d0,BVME_SCC_DATA_A
	jbra	L(serial_putc_done)
2:
#endif

#ifdef CONFIG_SUN3X
	is_not_sun3x(2f)
	movel	%d0,-(%sp)
	movel	0xFEFE0018,%a1
	jbsr	(%a1)
	addq	#4,%sp
	jbra	L(serial_putc_done)
2:
#endif

#ifdef CONFIG_Q40
	is_not_q40(2f)
	tst.l	%pc@(L(q40_do_debug))	/* only debug if requested */
	beq	2f
	lea	%pc@(q40_mem_cptr),%a1
	move.l	%a1@,%a0
	move.b	%d0,%a0@
	addq.l	#4,%a0
	move.l	%a0,%a1@
	jbra    L(serial_putc_done)
2:
#endif

#ifdef CONFIG_APOLLO
	is_not_apollo(2f)
	movl    %pc@(L(iobase)),%a1
	moveb	%d0,%a1@(LTHRB0)
1:      moveb   %a1@(LSRB0),%d0
	andb	#0x4,%d0
	beq	1b
	jbra	L(serial_putc_done)
2:
#endif

#ifdef CONFIG_HP300
	is_not_hp300(3f)
	movl    %pc@(L(iobase)),%a1
	addl	%pc@(L(uartbase)),%a1
	movel	%pc@(L(uart_scode)),%d1	/* Check the scode */
	jmi	3f			/* Unset? Exit */
	cmpi	#256,%d1		/* APCI scode? */
	jeq	2f
1:      moveb   %a1@(DCALSR),%d1	/* Output to DCA */
	andb	#0x20,%d1
	beq	1b
	moveb	%d0,%a1@(DCADATA)
	jbra	L(serial_putc_done)
2:	moveb	%a1@(APCILSR),%d1	/* Output to APCI */
	andb	#0x20,%d1
	beq	2b
	moveb	%d0,%a1@(APCIDATA)
	jbra	L(serial_putc_done)
3:
#endif

L(serial_putc_done):
func_return	serial_putc

/*
 * Output a string.
 */
func_start	puts,%d0/%a0

	movel	ARG1,%a0
	jra	2f
1:
#ifdef CONSOLE
	console_putc	%d0
#endif
#ifdef SERIAL_DEBUG
	serial_putc	%d0
#endif
2:	moveb	%a0@+,%d0
	jne	1b

func_return	puts

/*
 * Output number in hex notation.
 */

func_start	putn,%d0-%d2

	putc	' '

	movel	ARG1,%d0
	moveq	#7,%d1
1:	roll	#4,%d0
	move	%d0,%d2
	andb	#0x0f,%d2
	addb	#'0',%d2
	cmpb	#'9',%d2
	jls	2f
	addb	#'A'-('9'+1),%d2
2:
#ifdef CONSOLE
	console_putc	%d2
#endif
#ifdef SERIAL_DEBUG
	serial_putc	%d2
#endif
	dbra	%d1,1b

func_return	putn

#ifdef CONFIG_MAC
/*
 *	mac_serial_print
 *
 *	This routine takes its parameters on the stack.  It then
 *	turns around and calls the internal routine.  This routine
 *	is used until the Linux console driver initializes itself.
 *
 *	The calling parameters are:
 *		void mac_serial_print(const char *str);
 *
 *	This routine does NOT understand variable arguments only
 *	simple strings!
 */
ENTRY(mac_serial_print)
	moveml	%d0/%a0,%sp@-
#if 1
	move	%sr,%sp@-
	ori	#0x0700,%sr
#endif
	movel	%sp@(10),%a0		/* fetch parameter */
	jra	2f
1:	serial_putc	%d0
2:	moveb	%a0@+,%d0
	jne	1b
#if 1
	move	%sp@+,%sr
#endif
	moveml	%sp@+,%d0/%a0
	rts
#endif /* CONFIG_MAC */

#if defined(CONFIG_HP300) || defined(CONFIG_APOLLO)
func_start	set_leds,%d0/%a0
	movel	ARG1,%d0
#ifdef CONFIG_HP300
	is_not_hp300(1f)
	movel	%pc@(L(iobase)),%a0
	moveb	%d0,%a0@(0x1ffff)
	jra	2f
#endif
1:
#ifdef CONFIG_APOLLO
	movel   %pc@(L(iobase)),%a0
	lsll    #8,%d0
	eorw    #0xff00,%d0
	moveb	%d0,%a0@(LCPUCTRL)
#endif
2:
func_return	set_leds
#endif

#ifdef CONSOLE
/*
 *	For continuity, see the data alignment
 *	to which this structure is tied.
 */
#define Lconsole_struct_cur_column	0
#define Lconsole_struct_cur_row		4
#define Lconsole_struct_num_columns	8
#define Lconsole_struct_num_rows	12
#define Lconsole_struct_left_edge	16
#define Lconsole_struct_penguin_putc	20

func_start	console_init,%a0-%a4/%d0-%d7
	/*
	 *	Some of the register usage that follows
	 *		a0 = pointer to boot_info
	 *		a1 = pointer to screen
	 *		a2 = pointer to Lconsole_globals
	 *		d3 = pixel width of screen
	 *		d4 = pixel height of screen
	 *		(d3,d4) ~= (x,y) of a point just below
	 *			and to the right of the screen
	 *			NOT on the screen!
	 *		d5 = number of bytes per scan line
	 *		d6 = number of bytes on the entire screen
	 */

	lea	%pc@(L(console_globals)),%a2
	movel	%pc@(L(mac_videobase)),%a1
	movel	%pc@(L(mac_rowbytes)),%d5
	movel	%pc@(L(mac_dimensions)),%d3	/* -> low byte */
	movel	%d3,%d4
	swap	%d4		/* -> high byte */
	andl	#0xffff,%d3	/* d3 = screen width in pixels */
	andl	#0xffff,%d4	/* d4 = screen height in pixels */

	movel	%d5,%d6
|	subl	#20,%d6
	mulul	%d4,%d6		/* scan line bytes x num scan lines */
	divul	#8,%d6		/* we'll clear 8 bytes at a time */
	moveq	#-1,%d0		/* Mac_black */
	subq	#1,%d6

L(console_clear_loop):
	movel	%d0,%a1@+
	movel	%d0,%a1@+
	dbra	%d6,L(console_clear_loop)

	/* Calculate font size */

#if   defined(FONT_8x8) && defined(CONFIG_FONT_8x8)
	lea	%pc@(font_vga_8x8),%a0
#elif defined(FONT_8x16) && defined(CONFIG_FONT_8x16)
	lea	%pc@(font_vga_8x16),%a0
#elif defined(FONT_6x11) && defined(CONFIG_FONT_6x11)
	lea	%pc@(font_vga_6x11),%a0
#elif defined(CONFIG_FONT_8x8) /* default */
	lea	%pc@(font_vga_8x8),%a0
#else /* no compiled-in font */
	lea	0,%a0
#endif

	/*
	 *	At this point we make a shift in register usage
	 *	a1 = address of console_font pointer
	 */
	lea	%pc@(L(console_font)),%a1
	movel	%a0,%a1@	/* store pointer to struct fbcon_font_desc in console_font */
	tstl	%a0
	jeq	1f
	lea	%pc@(L(console_font_data)),%a4
	movel	%a0@(FONT_DESC_DATA),%d0
	subl	#L(console_font),%a1
	addl	%a1,%d0
	movel	%d0,%a4@

	/*
	 *	Calculate global maxs
	 *	Note - we can use either an
	 *	8 x 16 or 8 x 8 character font
	 *	6 x 11 also supported
	 */
		/* ASSERT: a0 = contents of Lconsole_font */
	movel	%d3,%d0				/* screen width in pixels */
	divul	%a0@(FONT_DESC_WIDTH),%d0	/* d0 = max num chars per row */

	movel	%d4,%d1				/* screen height in pixels */
	divul	%a0@(FONT_DESC_HEIGHT),%d1	/* d1 = max num rows */

	movel	%d0,%a2@(Lconsole_struct_num_columns)
	movel	%d1,%a2@(Lconsole_struct_num_rows)

	/*
	 *	Clear the current row and column
	 */
	clrl	%a2@(Lconsole_struct_cur_column)
	clrl	%a2@(Lconsole_struct_cur_row)
	clrl	%a2@(Lconsole_struct_left_edge)

	/*
	 * Initialization is complete
	 */
1:
func_return	console_init

func_start	console_put_stats,%a0/%d7
	/*
	 *	Some of the register usage that follows
	 *		a0 = pointer to boot_info
	 *		d7 = value of boot_info fields
	 */
	puts	"\nMacLinux\n\n"

#ifdef SERIAL_DEBUG
	puts	" vidaddr:"
	putn	%pc@(L(mac_videobase))		/* video addr. */

	puts	"\n  _stext:"
	lea	%pc@(_stext),%a0
	putn	%a0

	puts	"\nbootinfo:"
	lea	%pc@(_end),%a0
	putn	%a0

	puts	"\ncpuid:"
	putn	%pc@(L(cputype))
	putc	'\n'

#ifdef MAC_SERIAL_DEBUG
	putn	%pc@(L(mac_sccbase))
	putc	'\n'
#endif
#  if defined(MMU_PRINT)
	jbsr	mmu_print_machine_cpu_types
#  endif /* MMU_PRINT */
#endif /* SERIAL_DEBUG */

func_return	console_put_stats

#ifdef CONSOLE_PENGUIN
func_start	console_put_penguin,%a0-%a1/%d0-%d7
	/*
	 *	Get 'that_penguin' onto the screen in the upper right corner
	 *	penguin is 64 x 74 pixels, align against right edge of screen
	 */
	lea	%pc@(L(mac_dimensions)),%a0
	movel	%a0@,%d0
	andil	#0xffff,%d0
	subil	#64,%d0		/* snug up against the right edge */
	clrl	%d1		/* start at the top */
	movel	#73,%d7
	lea	%pc@(L(that_penguin)),%a1
L(console_penguin_row):
	movel	#31,%d6
L(console_penguin_pixel_pair):
	moveb	%a1@,%d2
	lsrb	#4,%d2
	console_plot_pixel %d0,%d1,%d2
	addq	#1,%d0
	moveb	%a1@+,%d2
	console_plot_pixel %d0,%d1,%d2
	addq	#1,%d0
	dbra	%d6,L(console_penguin_pixel_pair)

	subil	#64,%d0
	addq	#1,%d1
	dbra	%d7,L(console_penguin_row)

func_return	console_put_penguin

/* include penguin bitmap */
L(that_penguin):
#include "../mac/mac_penguin.S"
#endif

	/*
	 * Calculate source and destination addresses
	 *	output	a1 = dest
	 *		a2 = source
	 */

func_start	console_scroll,%a0-%a4/%d0-%d7
	lea	%pc@(L(mac_videobase)),%a0
	movel	%a0@,%a1
	movel	%a1,%a2
	lea	%pc@(L(mac_rowbytes)),%a0
	movel	%a0@,%d5
	movel	%pc@(L(console_font)),%a0
	tstl	%a0
	jeq	1f
	mulul	%a0@(FONT_DESC_HEIGHT),%d5	/* account for # scan lines per character */
	addal	%d5,%a2

	/*
	 * Get dimensions
	 */
	lea	%pc@(L(mac_dimensions)),%a0
	movel	%a0@,%d3
	movel	%d3,%d4
	swap	%d4
	andl	#0xffff,%d3	/* d3 = screen width in pixels */
	andl	#0xffff,%d4	/* d4 = screen height in pixels */

	/*
	 * Calculate number of bytes to move
	 */
	lea	%pc@(L(mac_rowbytes)),%a0
	movel	%a0@,%d6
	movel	%pc@(L(console_font)),%a0
	subl	%a0@(FONT_DESC_HEIGHT),%d4	/* we're not scrolling the top row! */
	mulul	%d4,%d6		/* scan line bytes x num scan lines */
	divul	#32,%d6		/* we'll move 8 longs at a time */
	subq	#1,%d6

L(console_scroll_loop):
	movel	%a2@+,%a1@+
	movel	%a2@+,%a1@+
	movel	%a2@+,%a1@+
	movel	%a2@+,%a1@+
	movel	%a2@+,%a1@+
	movel	%a2@+,%a1@+
	movel	%a2@+,%a1@+
	movel	%a2@+,%a1@+
	dbra	%d6,L(console_scroll_loop)

	lea	%pc@(L(mac_rowbytes)),%a0
	movel	%a0@,%d6
	movel	%pc@(L(console_font)),%a0
	mulul	%a0@(FONT_DESC_HEIGHT),%d6	/* scan line bytes x font height */
	divul	#32,%d6			/* we'll move 8 words at a time */
	subq	#1,%d6

	moveq	#-1,%d0
L(console_scroll_clear_loop):
	movel	%d0,%a1@+
	movel	%d0,%a1@+
	movel	%d0,%a1@+
	movel	%d0,%a1@+
	movel	%d0,%a1@+
	movel	%d0,%a1@+
	movel	%d0,%a1@+
	movel	%d0,%a1@+
	dbra	%d6,L(console_scroll_clear_loop)

1:
func_return	console_scroll


func_start	console_putc,%a0/%a1/%d0-%d7

	is_not_mac(L(console_exit))
	tstl	%pc@(L(console_font))
	jeq	L(console_exit)

	/* Output character in d7 on console.
	 */
	movel	ARG1,%d7
	cmpib	#'\n',%d7
	jbne	1f

	/* A little safe recursion is good for the soul */
	console_putc	#'\r'
1:
	lea	%pc@(L(console_globals)),%a0

	cmpib	#10,%d7
	jne	L(console_not_lf)
	movel	%a0@(Lconsole_struct_cur_row),%d0
	addil	#1,%d0
	movel	%d0,%a0@(Lconsole_struct_cur_row)
	movel	%a0@(Lconsole_struct_num_rows),%d1
	cmpl	%d1,%d0
	jcs	1f
	subil	#1,%d0
	movel	%d0,%a0@(Lconsole_struct_cur_row)
	console_scroll
1:
	jra	L(console_exit)

L(console_not_lf):
	cmpib	#13,%d7
	jne	L(console_not_cr)
	clrl	%a0@(Lconsole_struct_cur_column)
	jra	L(console_exit)

L(console_not_cr):
	cmpib	#1,%d7
	jne	L(console_not_home)
	clrl	%a0@(Lconsole_struct_cur_row)
	clrl	%a0@(Lconsole_struct_cur_column)
	jra	L(console_exit)

/*
 *	At this point we know that the %d7 character is going to be
 *	rendered on the screen.  Register usage is -
 *		a0 = pointer to console globals
 *		a1 = font data
 *		d0 = cursor column
 *		d1 = cursor row to draw the character
 *		d7 = character number
 */
L(console_not_home):
	movel	%a0@(Lconsole_struct_cur_column),%d0
	addql	#1,%a0@(Lconsole_struct_cur_column)
	movel	%a0@(Lconsole_struct_num_columns),%d1
	cmpl	%d1,%d0
	jcs	1f
	console_putc	#'\n'	/* recursion is OK! */
1:
	movel	%a0@(Lconsole_struct_cur_row),%d1

	/*
	 *	At this point we make a shift in register usage
	 *	a0 = address of pointer to font data (fbcon_font_desc)
	 */
	movel	%pc@(L(console_font)),%a0
	movel	%pc@(L(console_font_data)),%a1	/* Load fbcon_font_desc.data into a1 */
	andl	#0x000000ff,%d7
		/* ASSERT: a0 = contents of Lconsole_font */
	mulul	%a0@(FONT_DESC_HEIGHT),%d7	/* d7 = index into font data */
	addl	%d7,%a1			/* a1 = points to char image */

	/*
	 *	At this point we make a shift in register usage
	 *	d0 = pixel coordinate, x
	 *	d1 = pixel coordinate, y
	 *	d2 = (bit 0) 1/0 for white/black (!) pixel on screen
	 *	d3 = font scan line data (8 pixels)
	 *	d6 = count down for the font's pixel width (8)
	 *	d7 = count down for the font's pixel count in height
	 */
		/* ASSERT: a0 = contents of Lconsole_font */
	mulul	%a0@(FONT_DESC_WIDTH),%d0
	mulul	%a0@(FONT_DESC_HEIGHT),%d1
	movel	%a0@(FONT_DESC_HEIGHT),%d7	/* Load fbcon_font_desc.height into d7 */
	subq	#1,%d7
L(console_read_char_scanline):
	moveb	%a1@+,%d3

		/* ASSERT: a0 = contents of Lconsole_font */
	movel	%a0@(FONT_DESC_WIDTH),%d6	/* Load fbcon_font_desc.width into d6 */
	subql	#1,%d6

L(console_do_font_scanline):
	lslb	#1,%d3
	scsb	%d2		/* convert 1 bit into a byte */
	console_plot_pixel %d0,%d1,%d2
	addq	#1,%d0
	dbra	%d6,L(console_do_font_scanline)

		/* ASSERT: a0 = contents of Lconsole_font */
	subl	%a0@(FONT_DESC_WIDTH),%d0
	addq	#1,%d1
	dbra	%d7,L(console_read_char_scanline)

L(console_exit):
func_return	console_putc

	/*
	 *	Input:
	 *		d0 = x coordinate
	 *		d1 = y coordinate
	 *		d2 = (bit 0) 1/0 for white/black (!)
	 *	All registers are preserved
	 */
func_start	console_plot_pixel,%a0-%a1/%d0-%d4

	movel	%pc@(L(mac_videobase)),%a1
	movel	%pc@(L(mac_videodepth)),%d3
	movel	ARG1,%d0
	movel	ARG2,%d1
	mulul	%pc@(L(mac_rowbytes)),%d1
	movel	ARG3,%d2

	/*
	 *	Register usage:
	 *		d0 = x coord becomes byte offset into frame buffer
	 *		d1 = y coord
	 *		d2 = black or white (0/1)
	 *		d3 = video depth
	 *		d4 = temp of x (d0) for many bit depths
	 */
L(test_1bit):
	cmpb	#1,%d3
	jbne	L(test_2bit)
	movel	%d0,%d4		/* we need the low order 3 bits! */
	divul	#8,%d0
	addal	%d0,%a1
	addal	%d1,%a1
	andb	#7,%d4
	eorb	#7,%d4		/* reverse the x-coordinate w/ screen-bit # */
	andb	#1,%d2
	jbne	L(white_1)
	bsetb	%d4,%a1@
	jbra	L(console_plot_pixel_exit)
L(white_1):
	bclrb	%d4,%a1@
	jbra	L(console_plot_pixel_exit)

L(test_2bit):
	cmpb	#2,%d3
	jbne	L(test_4bit)
	movel	%d0,%d4		/* we need the low order 2 bits! */
	divul	#4,%d0
	addal	%d0,%a1
	addal	%d1,%a1
	andb	#3,%d4
	eorb	#3,%d4		/* reverse the x-coordinate w/ screen-bit # */
	lsll	#1,%d4		/* ! */
	andb	#1,%d2
	jbne	L(white_2)
	bsetb	%d4,%a1@
	addq	#1,%d4
	bsetb	%d4,%a1@
	jbra	L(console_plot_pixel_exit)
L(white_2):
	bclrb	%d4,%a1@
	addq	#1,%d4
	bclrb	%d4,%a1@
	jbra	L(console_plot_pixel_exit)

L(test_4bit):
	cmpb	#4,%d3
	jbne	L(test_8bit)
	movel	%d0,%d4		/* we need the low order bit! */
	divul	#2,%d0
	addal	%d0,%a1
	addal	%d1,%a1
	andb	#1,%d4
	eorb	#1,%d4
	lsll	#2,%d4		/* ! */
	andb	#1,%d2
	jbne	L(white_4)
	bsetb	%d4,%a1@
	addq	#1,%d4
	bsetb	%d4,%a1@
	addq	#1,%d4
	bsetb	%d4,%a1@
	addq	#1,%d4
	bsetb	%d4,%a1@
	jbra	L(console_plot_pixel_exit)
L(white_4):
	bclrb	%d4,%a1@
	addq	#1,%d4
	bclrb	%d4,%a1@
	addq	#1,%d4
	bclrb	%d4,%a1@
	addq	#1,%d4
	bclrb	%d4,%a1@
	jbra	L(console_plot_pixel_exit)

L(test_8bit):
	cmpb	#8,%d3
	jbne	L(test_16bit)
	addal	%d0,%a1
	addal	%d1,%a1
	andb	#1,%d2
	jbne	L(white_8)
	moveb	#0xff,%a1@
	jbra	L(console_plot_pixel_exit)
L(white_8):
	clrb	%a1@
	jbra	L(console_plot_pixel_exit)

L(test_16bit):
	cmpb	#16,%d3
	jbne	L(console_plot_pixel_exit)
	addal	%d0,%a1
	addal	%d0,%a1
	addal	%d1,%a1
	andb	#1,%d2
	jbne	L(white_16)
	clrw	%a1@
	jbra	L(console_plot_pixel_exit)
L(white_16):
	movew	#0x0fff,%a1@
	jbra	L(console_plot_pixel_exit)

L(console_plot_pixel_exit):
func_return	console_plot_pixel
#endif /* CONSOLE */

#if 0
/*
 * This is some old code lying around.  I don't believe
 * it's used or important anymore.  My guess is it contributed
 * to getting to this point, but it's done for now.
 * It was still in the 2.1.77 head.S, so it's still here.
 * (And still not used!)
 */
L(showtest):
	moveml	%a0/%d7,%sp@-
	puts	"A="
	putn	%a1

	.long	0xf0119f15		| ptestr	#5,%a1@,#7,%a0

	puts	"DA="
	putn	%a0

	puts	"D="
	putn	%a0@

	puts	"S="
	lea	%pc@(L(mmu)),%a0
	.long	0xf0106200		| pmove		%psr,%a0@
	clrl	%d7
	movew	%a0@,%d7
	putn	%d7

	putc	'\n'
	moveml	%sp@+,%a0/%d7
	rts
#endif	/* 0 */

__INITDATA
	.align	4

#if defined(CONFIG_ATARI) || defined(CONFIG_AMIGA) || \
    defined(CONFIG_HP300) || defined(CONFIG_APOLLO)
L(custom):
L(iobase):
	.long 0
#endif

#if defined(CONSOLE)
L(console_globals):
	.long	0		/* cursor column */
	.long	0		/* cursor row */
	.long	0		/* max num columns */
	.long	0		/* max num rows */
	.long	0		/* left edge */
	.long	0		/* mac putc */
L(console_font):
	.long	0		/* pointer to console font (struct font_desc) */
L(console_font_data):
	.long	0		/* pointer to console font data */
#endif /* CONSOLE */

#if defined(MMU_PRINT)
L(mmu_print_data):
	.long	0		/* valid flag */
	.long	0		/* start logical */
	.long	0		/* next logical */
	.long	0		/* start physical */
	.long	0		/* next physical */
#endif /* MMU_PRINT */

L(cputype):
	.long	0
L(mmu_cached_pointer_tables):
	.long	0
L(mmu_num_pointer_tables):
	.long	0
L(phys_kernel_start):
	.long	0
L(kernel_end):
	.long	0
L(memory_start):
	.long	0
L(kernel_pgdir_ptr):
	.long	0
L(temp_mmap_mem):
	.long	0

#if defined (CONFIG_MVME147)
M147_SCC_CTRL_A = 0xfffe3002
M147_SCC_DATA_A = 0xfffe3003
#endif

#if defined (CONFIG_MVME16x)
M162_SCC_CTRL_A = 0xfff45005
M167_CYCAR = 0xfff450ee
M167_CYIER = 0xfff45011
M167_CYLICR = 0xfff45026
M167_CYTEOIR = 0xfff45085
M167_CYTDR = 0xfff450f8
M167_PCSCCTICR = 0xfff4201e
M167_PCTPIACKR = 0xfff42025
#endif

#if defined (CONFIG_BVME6000)
BVME_SCC_CTRL_A	= 0xffb0000b
BVME_SCC_DATA_A	= 0xffb0000f
#endif

#if defined(CONFIG_MAC)
L(mac_booter_data):
	.long	0
L(mac_videobase):
	.long	0
L(mac_videodepth):
	.long	0
L(mac_dimensions):
	.long	0
L(mac_rowbytes):
	.long	0
#ifdef MAC_SERIAL_DEBUG
L(mac_sccbase):
	.long	0
#endif /* MAC_SERIAL_DEBUG */
#endif

#if defined (CONFIG_APOLLO)
LSRB0        = 0x10412
LTHRB0       = 0x10416
LCPUCTRL     = 0x10100
#endif

#if defined(CONFIG_HP300)
DCADATA	     = 0x11
DCALSR	     = 0x1b
APCIDATA     = 0x00
APCILSR      = 0x14
L(uartbase):
	.long	0
L(uart_scode):
	.long	-1
#endif

__FINIT
	.data
	.align	4

availmem:
	.long	0
m68k_pgtable_cachemode:
	.long	0
m68k_supervisor_cachemode:
	.long	0
#if defined(CONFIG_MVME16x)
mvme_bdid:
	.long	0,0,0,0,0,0,0,0
#endif
#if defined(CONFIG_Q40)
q40_mem_cptr:
	.long	0
L(q40_do_debug):
	.long	0
#endif
