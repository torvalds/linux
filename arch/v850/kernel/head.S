/*
 * arch/v850/kernel/head.S -- Lowest-level startup code
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <asm/clinkage.h>
#include <asm/current.h>
#include <asm/entry.h>
#include <asm/thread_info.h>
#include <asm/irq.h>


/* Make a slightly more convenient alias for C_SYMBOL_NAME.  */
#define CSYM	C_SYMBOL_NAME


	.text

	// Define `mach_early_init' as a weak symbol
	.global	CSYM(mach_early_init)
	.weak	CSYM(mach_early_init)

C_ENTRY(start):
	// Make sure interrupts are turned off, just in case
	di

#ifdef CONFIG_RESET_GUARD
	// See if we got here via an unexpected reset
	ld.w	RESET_GUARD, r19	// Check current value of reset guard
	mov	RESET_GUARD_ACTIVE, r20
	cmp	r19, r20
	bne	1f			// Guard was not active

	// If we get here, the reset guard was active.  Load up some
	// interesting values as arguments, and jump to the handler.
	st.w	r0, RESET_GUARD		// Allow further resets to succeed
	mov	lp, r6			// Arg 0: return address
	ld.b	KM, r7			// Arg 1: kernel mode
	mov	sp, r9			// Arg 3: stack pointer
	ld.w	KSP, r19		// maybe switch to kernel stack
	cmp	r7, r0			// see if already in kernel mode
	cmov	z, r19, sp, sp		//  and switch to kernel stack if not
	GET_CURRENT_TASK(r8)		// Arg 2: task pointer
	jr	CSYM(unexpected_reset)

1:	st.w	r20, RESET_GUARD	// Turn on reset guard
#endif /* CONFIG_RESET_GUARD */

	// Setup a temporary stack for doing pre-initialization function calls.
	// 
	// We can't use the initial kernel stack, because (1) it may be
	// located in memory we're not allowed to touch, and (2) since
	// it's in the data segment, calling memcpy to initialize that
	// area from ROM will overwrite memcpy's return address.
	mov	hilo(CSYM(_init_stack_end) - 4), sp

	// See if there's a platform-specific early-initialization routine
	// defined; it's a weak symbol, so it will have an address of zero if
	// there's not.
	mov	hilo(CSYM(mach_early_init)), r6
	cmp	r6, r0
	bz	3f

	// There is one, so call it.  If this function is written in C, it
	// should be very careful -- the stack pointer is valid, but very
	// little else is (e.g., bss is not zeroed yet, and initialized data
	// hasn't been).
	jarl	2f, lp			// first figure out return address
2:	add	3f - ., lp
	jmp	[r6]			// do call
3:

#ifdef CONFIG_ROM_KERNEL
	// Copy the data area from ROM to RAM
	mov	hilo(CSYM(_rom_copy_dst_start)), r6
	mov	hilo(CSYM(_rom_copy_src_start)), r7
	mov	hilo(CSYM(_rom_copy_dst_end)), r8
	sub	r6, r8
	jarl	CSYM(memcpy), lp
#endif

	// Load the initial thread's stack, and current task pointer (in r16)
	mov	hilo(CSYM(init_thread_union)), r19
	movea	THREAD_SIZE, r19, sp
	ld.w	TI_TASK[r19], CURRENT_TASK

#ifdef CONFIG_TIME_BOOTUP
	/* This stuff must come after mach_early_init, because interrupts may
	   not work until after its been called.  */
	jarl	CSYM(highres_timer_reset), lp
	jarl	CSYM(highres_timer_start), lp
#endif

	// Kernel stack pointer save location
	st.w	sp, KSP

	// Assert that we're in `kernel mode'
	mov	1, r19
	st.w	r19, KM

#ifdef CONFIG_ZERO_BSS
	// Zero bss area, since we can't rely upon any loader to do so
	mov	hilo(CSYM(_sbss)), r6
	mov	r0, r7
	mov	hilo(CSYM(_ebss)), r8
	sub	r6, r8
	jarl	CSYM(memset), lp
#endif

	// What happens if the main kernel function returns (it shouldn't)
	mov	hilo(CSYM(machine_halt)), lp

	// Start the linux kernel.  We use an indirect jump to get extra
	// range, because on some platforms this initial startup code
	// (and the associated platform-specific code in mach_early_init)
	// are located far away from the main kernel, e.g. so that they
	// can initialize RAM first and copy the kernel or something.
	mov	hilo(CSYM(start_kernel)), r12
	jmp	[r12]
C_END(start)
