/*
 * arch/xtensa/kernel/head.S
 *
 * Xtensa Processor startup code.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 * Marc Gauthier <marc@tensilica.com, marc@alumni.uwaterloo.ca>
 * Joe Taylor <joe@tensilica.com, joetylr@yahoo.com>
 * Kevin Chea
 */

#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cacheasm.h>
#include <asm/initialize_mmu.h>

#include <linux/init.h>
#include <linux/linkage.h>

/*
 * This module contains the entry code for kernel images. It performs the
 * minimal setup needed to call the generic C routines.
 *
 * Prerequisites:
 *
 * - The kernel image has been loaded to the actual address where it was
 *   compiled to.
 * - a2 contains either 0 or a pointer to a list of boot parameters.
 *   (see setup.c for more details)
 *
 */

/*
 *  _start
 *
 *  The bootloader passes a pointer to a list of boot parameters in a2.
 */

	/* The first bytes of the kernel image must be an instruction, so we
	 * manually allocate and define the literal constant we need for a jx
	 * instruction.
	 */

	__HEAD
ENTRY(_start)

	_j	2f
	.align	4
1:	.word	_startup
2:	l32r	a0, 1b
	jx	a0

ENDPROC(_start)

	.section .init.text, "ax"

ENTRY(_startup)

	/* Disable interrupts and exceptions. */

	movi	a0, LOCKLEVEL
	wsr	a0, ps

	/* Preserve the pointer to the boot parameter list in EXCSAVE_1 */

	wsr	a2, excsave1

	/* Start with a fresh windowbase and windowstart.  */

	movi	a1, 1
	movi	a0, 0
	wsr	a1, windowstart
	wsr	a0, windowbase
	rsync

	/* Set a0 to 0 for the remaining initialization. */

	movi	a0, 0

	/* Clear debugging registers. */

#if XCHAL_HAVE_DEBUG
	wsr	a0, ibreakenable
	wsr	a0, icount
	movi	a1, 15
	wsr	a0, icountlevel

	.set	_index, 0
	.rept	XCHAL_NUM_DBREAK - 1
	wsr	a0, SREG_DBREAKC + _index
	.set	_index, _index + 1
	.endr
#endif

	/* Clear CCOUNT (not really necessary, but nice) */

	wsr	a0, ccount	# not really necessary, but nice

	/* Disable zero-loops. */

#if XCHAL_HAVE_LOOPS
	wsr	a0, lcount
#endif

	/* Disable all timers. */

	.set	_index, 0
	.rept	XCHAL_NUM_TIMERS
	wsr	a0, SREG_CCOMPARE + _index
	.set	_index, _index + 1
	.endr

	/* Interrupt initialization. */

	movi	a2, XCHAL_INTTYPE_MASK_SOFTWARE | XCHAL_INTTYPE_MASK_EXTERN_EDGE
	wsr	a0, intenable
	wsr	a2, intclear

	/* Disable coprocessors. */

#if XCHAL_HAVE_CP
	wsr	a0, cpenable
#endif

	/* Set PS.INTLEVEL=1, PS.WOE=0, kernel stack, PS.EXCM=0
	 *
	 * Note: PS.EXCM must be cleared before using any loop
	 *	 instructions; otherwise, they are silently disabled, and
	 * 	 at most one iteration of the loop is executed.
	 */

	movi	a1, 1
	wsr	a1, ps
	rsync

	/*  Initialize the caches.
	 *  a2, a3 are just working registers (clobbered).
	 */

#if XCHAL_DCACHE_LINE_LOCKABLE
	___unlock_dcache_all a2 a3
#endif

#if XCHAL_ICACHE_LINE_LOCKABLE
	___unlock_icache_all a2 a3
#endif

	___invalidate_dcache_all a2 a3
	___invalidate_icache_all a2 a3

	isync

	initialize_mmu

	/* Unpack data sections
	 *
	 * The linker script used to build the Linux kernel image
	 * creates a table located at __boot_reloc_table_start
	 * that contans the information what data needs to be unpacked.
	 *
	 * Uses a2-a7.
	 */

	movi	a2, __boot_reloc_table_start
	movi	a3, __boot_reloc_table_end

1:	beq	a2, a3, 3f	# no more entries?
	l32i	a4, a2, 0	# start destination (in RAM)
	l32i	a5, a2, 4	# end desination (in RAM)
	l32i	a6, a2, 8	# start source (in ROM)
	addi	a2, a2, 12	# next entry
	beq	a4, a5, 1b	# skip, empty entry
	beq	a4, a6, 1b	# skip, source and dest. are the same

2:	l32i	a7, a6, 0	# load word
	addi	a6, a6, 4
	s32i	a7, a4, 0	# store word
	addi	a4, a4, 4
	bltu	a4, a5, 2b
	j	1b

3:
	/* All code and initialized data segments have been copied.
	 * Now clear the BSS segment.
	 */

	movi	a2, __bss_start	# start of BSS
	movi	a3, __bss_stop	# end of BSS

	__loopt	a2, a3, a4, 2
	s32i	a0, a2, 0
	__endla	a2, a4, 4

#if XCHAL_DCACHE_IS_WRITEBACK

	/* After unpacking, flush the writeback cache to memory so the
	 * instructions/data are available.
	 */

	___flush_dcache_all a2 a3
#endif

	/* Setup stack and enable window exceptions (keep irqs disabled) */

	movi	a1, init_thread_union
	addi	a1, a1, KERNEL_STACK_SIZE

	movi	a2, 0x00040001		# WOE=1, INTLEVEL=1, UM=0
	wsr	a2, ps			# (enable reg-windows; progmode stack)
	rsync

	/* Set up EXCSAVE[DEBUGLEVEL] to point to the Debug Exception Handler.*/

	movi	a2, debug_exception
	wsr	a2, SREG_EXCSAVE + XCHAL_DEBUGLEVEL

	/* Set up EXCSAVE[1] to point to the exc_table. */

	movi	a6, exc_table
	xsr	a6, excsave1

	/* init_arch kick-starts the linux kernel */

	movi	a4, init_arch
	callx4	a4

	movi	a4, start_kernel
	callx4	a4

should_never_return:
	j	should_never_return

ENDPROC(_startup)

/*
 * BSS section
 */
	
__PAGE_ALIGNED_BSS
#ifdef CONFIG_MMU
ENTRY(swapper_pg_dir)
	.fill	PAGE_SIZE, 1, 0
END(swapper_pg_dir)
#endif
ENTRY(empty_zero_page)
	.fill	PAGE_SIZE, 1, 0
END(empty_zero_page)
