/*
 * Copyright (C) 2009 Wind River Systems Inc
 *   Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 * Copyright (C) 2004 Microtronix Datacom Ltd
 * Copyright (C) 2001 Vic Phillips, Microtronix Datacom Ltd.
 *
 * Based on head.S for Altera's Excalibur development board with nios processor
 *
 * Based on the following from the Excalibur sdk distribution:
 *	NA_MemoryMap.s, NR_JumpToStart.s, NR_Setup.s, NR_CWPManager.s
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/page.h>
#include <asm/asm-offsets.h>
#include <asm/asm-macros.h>

#ifdef CONFIG_MMU
/*
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
.data
.global empty_zero_page
.align 12
empty_zero_page:
	.space	PAGE_SIZE
#endif /* CONFIG_MMU */

/*
 * This global variable is used as an extension to the nios'
 * STATUS register to emulate a user/supervisor mode.
 */
	.data
	.align	2
	.set noat
#ifndef CONFIG_MMU
	.global	status_extension
status_extension:
	.long	0
#endif /* CONFIG_MMU */

	.global _current_thread
_current_thread:
	.long	0
/*
 * Input(s): passed from u-boot
 *   r4 - Optional pointer to a board information structure.
 *   r5 - Optional pointer to the physical starting address of the init RAM
 *        disk.
 *   r6 - Optional pointer to the physical ending address of the init RAM
 *        disk.
 *   r7 - Optional pointer to the physical starting address of any kernel
 *        command-line parameters.
 */

/*
 * First executable code - detected and jumped to by the ROM bootstrap
 * if the code resides in flash (looks for "Nios" at offset 0x0c from
 * the potential executable image).
 */
	__HEAD
ENTRY(_start)
	wrctl	status, r0		/* Disable interrupts */

	/* Flush all cache lines within the instruction cache */
	movia	r1, NIOS2_ICACHE_SIZE
	movui	r2, NIOS2_ICACHE_LINE_SIZE

text_flush:
	flushi	r1
	sub	r1, r1, r2
	bgt	r1, r0, text_flush
	br	1f

	/*
	 * This is the default location for the exception handler. Code in jump
	 * to our handler
	 */
ENTRY(exception_handler_hook)
	movia	r24, inthandler
	jmp	r24

#ifdef CONFIG_MMU

ENTRY(fast_handler)
	nextpc et
helper:
	stw	r3, r3save - helper(et)

	rdctl	r3 , pteaddr
	srli	r3, r3, 12
	slli	r3, r3, 2
	movia	et, pgd_current

	ldw	et, 0(et)
	add	r3, et, r3
	ldw	et, 0(r3)

	rdctl	r3, pteaddr
	andi	r3, r3, 0xfff
	add	et, r3, et
	ldw	et, 0(et)
	wrctl	tlbacc, et
	nextpc	et
helper2:
	ldw	r3, r3save - helper2(et)
	subi	ea, ea, 4
	eret
r3save:
	.word 0x0
ENTRY(fast_handler_end)

#endif /* CONFIG_MMU */

1:
	/*
	 * After flushing the instruction cache, we must flush the data
	 * cache.
	 */
	movia	r1, NIOS2_DCACHE_SIZE
	movui	r2, NIOS2_DCACHE_LINE_SIZE

data_flush:
	flushd	0(r1)
	sub	r1, r1, r2
	bgt	r1, r0, data_flush

#ifdef CONFIG_MMU
	nextpc	r1			/* Find out where we are */
chkadr:
	movia	r2, chkadr
	beq	r1, r2,finish_move	/* We are running in RAM done */
	addi	r1, r1,(_start - chkadr)	/* Source */
	movia	r2, _start		/* Destination */
	movia	r3, __bss_start		/* End of copy */

loop_move:				/* r1: src, r2: dest, r3: last dest */
	ldw	r8, 0(r1)		/* load a word from [r1] */
	stw	r8, 0(r2)		/* store a word to dest [r2] */
	flushd	0(r2)			/* Flush cache for safety */
	addi 	r1, r1, 4		/* inc the src addr */
	addi	r2, r2, 4		/* inc the dest addr */
	blt	r2, r3, loop_move

	movia	r1, finish_move		/* VMA(_start)->l1 */
	jmp	r1			/* jmp to _start */

finish_move:
#endif /* CONFIG_MMU */

	/* Mask off all possible interrupts */
	wrctl	ienable, r0

	/* Clear .bss */
	movia	r2, __bss_start
	movia	r1, __bss_stop
1:
	stb	r0, 0(r2)
	addi	r2, r2, 1
	bne	r1, r2, 1b

#ifndef CONFIG_MMU
	movia	r1, status_extension	/* get the STATUS extension address */
	movi	r2, PS_S		/* set initial mode = supervisor */
	stw	r2, 0(r1)
#endif /* CONFIG_MMU */

	movia	r1, init_thread_union	/* set stack at top of the task union */
	addi	sp, r1, THREAD_SIZE
	movia	r2, _current_thread	/* Remember current thread */
	stw	r1, 0(r2)

	movia	r1, nios2_boot_init	/* save args r4-r7 passed from u-boot */
	callr	r1

	movia	r1, start_kernel	/* call start_kernel as a subroutine */
	callr	r1

	/* If we return from start_kernel, break to the oci debugger and
	 * buggered we are.
	 */
	break

	/* End of startup code */
.set at
