/*
 * arch/xtensa/kernel/head.S
 *
 * Xtensa Processor startup code.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2008 Tensilica Inc.
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
#include <asm/mxregs.h>

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
	.begin	no-absolute-literals

ENTRY(_start)

	/* Preserve the pointer to the boot parameter list in EXCSAVE_1 */
	wsr     a2, excsave1
	_j	_SetupOCD

	.align	4
	.literal_position
.Lstartup:
	.word	_startup

	.align	4
_SetupOCD:
	/*
	 * Initialize WB, WS, and clear PS.EXCM (to allow loop instructions).
	 * Set Interrupt Level just below XCHAL_DEBUGLEVEL to allow
	 * xt-gdb to single step via DEBUG exceptions received directly
	 * by ocd.
	 */
	movi	a1, 1
	movi	a0, 0
	wsr	a1, windowstart
	wsr	a0, windowbase
	rsync

	movi	a1, LOCKLEVEL
	wsr	a1, ps
	rsync

	.global _SetupMMU
_SetupMMU:
	Offset = _SetupMMU - _start

#ifdef CONFIG_INITIALIZE_XTENSA_MMU_INSIDE_VMLINUX
	initialize_mmu
#if defined(CONFIG_MMU) && XCHAL_HAVE_PTP_MMU && XCHAL_HAVE_SPANNING_WAY
	rsr	a2, excsave1
	movi	a3, 0x08000000
	bgeu	a2, a3, 1f
	movi	a3, 0xd0000000
	add	a2, a2, a3
	wsr	a2, excsave1
1:
#endif
#endif
	.end	no-absolute-literals

	l32r	a0, .Lstartup
	jx	a0

ENDPROC(_start)

	__REF
	.literal_position

ENTRY(_startup)

	/* Set a0 to 0 for the remaining initialization. */

	movi	a0, 0

#if XCHAL_HAVE_VECBASE
	movi    a2, VECBASE_VADDR
	wsr	a2, vecbase
#endif

	/* Clear debugging registers. */

#if XCHAL_HAVE_DEBUG
#if XCHAL_NUM_IBREAK > 0
	wsr	a0, ibreakenable
#endif
	wsr	a0, icount
	movi	a1, 15
	wsr	a0, icountlevel

	.set	_index, 0
	.rept	XCHAL_NUM_DBREAK
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

#ifdef CONFIG_HAVE_SMP
	movi	a2, CCON	# MX External Register to Configure Cache
	movi	a3, 1
	wer	a3, a2
#endif

	/* Setup stack and enable window exceptions (keep irqs disabled) */

	movi	a1, start_info
	l32i	a1, a1, 0

	movi	a2, (1 << PS_WOE_BIT) | LOCKLEVEL
					# WOE=1, INTLEVEL=LOCKLEVEL, UM=0
	wsr	a2, ps			# (enable reg-windows; progmode stack)
	rsync

#ifdef CONFIG_SMP
	/*
	 * Notice that we assume with SMP that cores have PRID
	 * supported by the cores.
	 */
	rsr	a2, prid
	bnez	a2, .Lboot_secondary

#endif  /* CONFIG_SMP */

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
	__endla	a2, a3, 4

#if XCHAL_DCACHE_IS_WRITEBACK

	/* After unpacking, flush the writeback cache to memory so the
	 * instructions/data are available.
	 */

	___flush_dcache_all a2 a3
#endif
	memw
	isync
	___invalidate_icache_all a2 a3
	isync

	movi	a6, 0
	xsr	a6, excsave1

	/* init_arch kick-starts the linux kernel */

	call4	init_arch
	call4	start_kernel

should_never_return:
	j	should_never_return

#ifdef CONFIG_SMP
.Lboot_secondary:

	movi	a2, cpu_start_ccount
1:
	l32i	a3, a2, 0
	beqi	a3, 0, 1b
	movi	a3, 0
	s32i	a3, a2, 0
	memw
1:
	l32i	a3, a2, 0
	beqi	a3, 0, 1b
	wsr	a3, ccount
	movi	a3, 0
	s32i	a3, a2, 0
	memw

	movi	a6, 0
	wsr	a6, excsave1

	call4	secondary_start_kernel
	j	should_never_return

#endif  /* CONFIG_SMP */

ENDPROC(_startup)

#ifdef CONFIG_HOTPLUG_CPU

ENTRY(cpu_restart)

#if XCHAL_DCACHE_IS_WRITEBACK
	___flush_invalidate_dcache_all a2 a3
#else
	___invalidate_dcache_all a2 a3
#endif
	memw
	movi	a2, CCON	# MX External Register to Configure Cache
	movi	a3, 0
	wer	a3, a2
	extw

	rsr	a0, prid
	neg	a2, a0
	movi	a3, cpu_start_id
	s32i	a2, a3, 0
#if XCHAL_DCACHE_IS_WRITEBACK
	dhwbi	a3, 0
#endif
1:
	l32i	a2, a3, 0
	dhi	a3, 0
	bne	a2, a0, 1b

	/*
	 * Initialize WB, WS, and clear PS.EXCM (to allow loop instructions).
	 * Set Interrupt Level just below XCHAL_DEBUGLEVEL to allow
	 * xt-gdb to single step via DEBUG exceptions received directly
	 * by ocd.
	 */
	movi	a1, 1
	movi	a0, 0
	wsr	a1, windowstart
	wsr	a0, windowbase
	rsync

	movi	a1, LOCKLEVEL
	wsr	a1, ps
	rsync

	j	_startup

ENDPROC(cpu_restart)

#endif  /* CONFIG_HOTPLUG_CPU */

/*
 * DATA section
 */

        .section ".data.init.refok"
        .align  4
ENTRY(start_info)
        .long   init_thread_union + KERNEL_STACK_SIZE

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
