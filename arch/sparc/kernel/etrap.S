/* $Id: etrap.S,v 1.31 2000/01/08 16:38:18 anton Exp $
 * etrap.S: Sparc trap window preparation for entry into the
 *          Linux kernel.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/contregs.h>
#include <asm/page.h>
#include <asm/psr.h>
#include <asm/ptrace.h>
#include <asm/winmacro.h>
#include <asm/asmmacro.h>
#include <asm/thread_info.h>

/* Registers to not touch at all. */
#define t_psr        l0 /* Set by caller */
#define t_pc         l1 /* Set by caller */
#define t_npc        l2 /* Set by caller */
#define t_wim        l3 /* Set by caller */
#define t_twinmask   l4 /* Set at beginning of this entry routine. */
#define t_kstack     l5 /* Set right before pt_regs frame is built */
#define t_retpc      l6 /* If you change this, change winmacro.h header file */
#define t_systable   l7 /* Never touch this, could be the syscall table ptr. */
#define curptr       g6 /* Set after pt_regs frame is built */

	.text
	.align 4

	/* SEVEN WINDOW PATCH INSTRUCTIONS */
	.globl	tsetup_7win_patch1, tsetup_7win_patch2
	.globl	tsetup_7win_patch3, tsetup_7win_patch4
	.globl	tsetup_7win_patch5, tsetup_7win_patch6
tsetup_7win_patch1:	sll	%t_wim, 0x6, %t_wim
tsetup_7win_patch2:	and	%g2, 0x7f, %g2
tsetup_7win_patch3:	and	%g2, 0x7f, %g2
tsetup_7win_patch4:	and	%g1, 0x7f, %g1
tsetup_7win_patch5:	sll	%t_wim, 0x6, %t_wim
tsetup_7win_patch6:	and	%g2, 0x7f, %g2
	/* END OF PATCH INSTRUCTIONS */

	/* At trap time, interrupts and all generic traps do the
	 * following:
	 *
	 * rd	%psr, %l0
	 * b	some_handler
	 * rd	%wim, %l3
	 * nop
	 *
	 * Then 'some_handler' if it needs a trap frame (ie. it has
	 * to call c-code and the trap cannot be handled in-window)
	 * then it does the SAVE_ALL macro in entry.S which does
	 *
	 * sethi	%hi(trap_setup), %l4
	 * jmpl		%l4 + %lo(trap_setup), %l6
	 * nop
	 */

	/* 2 3 4  window number
	 * -----
	 * O T S  mnemonic
	 *
	 * O == Current window before trap
	 * T == Window entered when trap occurred
	 * S == Window we will need to save if (1<<T) == %wim
	 *
	 * Before execution gets here, it must be guaranteed that
	 * %l0 contains trap time %psr, %l1 and %l2 contain the
	 * trap pc and npc, and %l3 contains the trap time %wim.
	 */

	.globl	trap_setup, tsetup_patch1, tsetup_patch2
	.globl	tsetup_patch3, tsetup_patch4
	.globl	tsetup_patch5, tsetup_patch6
trap_setup:
	/* Calculate mask of trap window.  See if from user
	 * or kernel and branch conditionally.
	 */
	mov	1, %t_twinmask
	andcc	%t_psr, PSR_PS, %g0		 ! fromsupv_p = (psr & PSR_PS)
	be	trap_setup_from_user		 ! nope, from user mode
	 sll	%t_twinmask, %t_psr, %t_twinmask ! t_twinmask = (1 << psr)

	/* From kernel, allocate more kernel stack and
	 * build a pt_regs trap frame.
	 */
	sub	%fp, (STACKFRAME_SZ + TRACEREG_SZ), %t_kstack
	STORE_PT_ALL(t_kstack, t_psr, t_pc, t_npc, g2)

	/* See if we are in the trap window. */
	andcc	%t_twinmask, %t_wim, %g0
	bne	trap_setup_kernel_spill		! in trap window, clean up
	 nop

	/* Trap from kernel with a window available.
	 * Just do it...
	 */
	jmpl	%t_retpc + 0x8, %g0	! return to caller
	 mov	%t_kstack, %sp		! jump onto new stack

trap_setup_kernel_spill:
	ld	[%curptr + TI_UWINMASK], %g1
	orcc	%g0, %g1, %g0
	bne	trap_setup_user_spill	! there are some user windows, yuck
	/* Spill from kernel, but only kernel windows, adjust
	 * %wim and go.
	 */
	 srl	%t_wim, 0x1, %g2	! begin computation of new %wim
tsetup_patch1:
	sll	%t_wim, 0x7, %t_wim	! patched on 7 window Sparcs
	or	%t_wim, %g2, %g2
tsetup_patch2:
	and	%g2, 0xff, %g2		! patched on 7 window Sparcs

	save	%g0, %g0, %g0

	/* Set new %wim value */
	wr	%g2, 0x0, %wim

	/* Save the kernel window onto the corresponding stack. */
	STORE_WINDOW(sp)

	restore	%g0, %g0, %g0

	jmpl	%t_retpc + 0x8, %g0	! return to caller
	 mov	%t_kstack, %sp		! and onto new kernel stack

#define STACK_OFFSET (THREAD_SIZE - TRACEREG_SZ - STACKFRAME_SZ)

trap_setup_from_user:
	/* We can't use %curptr yet. */
	LOAD_CURRENT(t_kstack, t_twinmask)

	sethi	%hi(STACK_OFFSET), %t_twinmask
	or	%t_twinmask, %lo(STACK_OFFSET), %t_twinmask
	add	%t_kstack, %t_twinmask, %t_kstack

	mov	1, %t_twinmask
	sll	%t_twinmask, %t_psr, %t_twinmask ! t_twinmask = (1 << psr)

	/* Build pt_regs frame. */
	STORE_PT_ALL(t_kstack, t_psr, t_pc, t_npc, g2)

#if 0
	/* If we're sure every task_struct is THREAD_SIZE aligned,
	   we can speed this up. */
	sethi	%hi(STACK_OFFSET), %curptr
	or	%curptr, %lo(STACK_OFFSET), %curptr
	sub	%t_kstack, %curptr, %curptr
#else
	sethi	%hi(~(THREAD_SIZE - 1)), %curptr
	and	%t_kstack, %curptr, %curptr
#endif

	/* Clear current_thread_info->w_saved */
	st	%g0, [%curptr + TI_W_SAVED]

	/* See if we are in the trap window. */
	andcc	%t_twinmask, %t_wim, %g0
	bne	trap_setup_user_spill		! yep we are
	 orn	%g0, %t_twinmask, %g1		! negate trap win mask into %g1

	/* Trap from user, but not into the invalid window.
	 * Calculate new umask.  The way this works is,
	 * any window from the %wim at trap time until
	 * the window right before the one we are in now,
	 * is a user window.  A diagram:
	 *
	 *      7 6 5 4 3 2 1 0    window number
	 *      ---------------
	 *        I     L T        mnemonic
	 *
	 * Window 'I' is the invalid window in our example,
	 * window 'L' is the window the user was in when
	 * the trap occurred, window T is the trap window
	 * we are in now.  So therefore, windows 5, 4 and
	 * 3 are user windows.  The following sequence
	 * computes the user winmask to represent this.
	 */
	subcc	%t_wim, %t_twinmask, %g2
	bneg,a	1f
	 sub	%g2, 0x1, %g2
1:
	andn	%g2, %t_twinmask, %g2
tsetup_patch3:
	and	%g2, 0xff, %g2			! patched on 7win Sparcs
	st	%g2, [%curptr + TI_UWINMASK]	! store new umask

	jmpl	%t_retpc + 0x8, %g0		! return to caller
	 mov	%t_kstack, %sp			! and onto kernel stack

trap_setup_user_spill:
	/* A spill occurred from either kernel or user mode
	 * and there exist some user windows to deal with.
	 * A mask of the currently valid user windows
	 * is in %g1 upon entry to here.
	 */

tsetup_patch4:
	and	%g1, 0xff, %g1		! patched on 7win Sparcs, mask
	srl	%t_wim, 0x1, %g2	! compute new %wim
tsetup_patch5:
	sll	%t_wim, 0x7, %t_wim	! patched on 7win Sparcs
	or	%t_wim, %g2, %g2	! %g2 is new %wim
tsetup_patch6:
	and	%g2, 0xff, %g2		! patched on 7win Sparcs
	andn	%g1, %g2, %g1		! clear this bit in %g1
	st	%g1, [%curptr + TI_UWINMASK]

	save	%g0, %g0, %g0

	wr	%g2, 0x0, %wim

	/* Call MMU-architecture dependent stack checking
	 * routine.
	 */
	.globl	tsetup_mmu_patchme
tsetup_mmu_patchme:
	b	tsetup_sun4c_stackchk
	 andcc	%sp, 0x7, %g0

	/* Architecture specific stack checking routines.  When either
	 * of these routines are called, the globals are free to use
	 * as they have been safely stashed on the new kernel stack
	 * pointer.  Thus the definition below for simplicity.
	 */
#define glob_tmp     g1

	.globl	tsetup_sun4c_stackchk
tsetup_sun4c_stackchk:
	/* Done by caller: andcc %sp, 0x7, %g0 */
	bne	trap_setup_user_stack_is_bolixed
	 sra	%sp, 29, %glob_tmp

	add	%glob_tmp, 0x1, %glob_tmp
	andncc	%glob_tmp, 0x1, %g0
	bne	trap_setup_user_stack_is_bolixed
	 and	%sp, 0xfff, %glob_tmp		! delay slot

	/* See if our dump area will be on more than one
	 * page.
	 */
	add	%glob_tmp, 0x38, %glob_tmp
	andncc	%glob_tmp, 0xff8, %g0
	be	tsetup_sun4c_onepage		! only one page to check
	 lda	[%sp] ASI_PTE, %glob_tmp	! have to check first page anyways

tsetup_sun4c_twopages:
	/* Is first page ok permission wise? */
	srl	%glob_tmp, 29, %glob_tmp
	cmp	%glob_tmp, 0x6
	bne	trap_setup_user_stack_is_bolixed
	 add	%sp, 0x38, %glob_tmp		/* Is second page in vma hole? */

	sra	%glob_tmp, 29, %glob_tmp
	add	%glob_tmp, 0x1, %glob_tmp
	andncc	%glob_tmp, 0x1, %g0
	bne	trap_setup_user_stack_is_bolixed
	 add	%sp, 0x38, %glob_tmp

	lda	[%glob_tmp] ASI_PTE, %glob_tmp

tsetup_sun4c_onepage:
	srl	%glob_tmp, 29, %glob_tmp
	cmp	%glob_tmp, 0x6				! can user write to it?
	bne	trap_setup_user_stack_is_bolixed	! failure
	 nop

	STORE_WINDOW(sp)

	restore %g0, %g0, %g0

	jmpl	%t_retpc + 0x8, %g0
	 mov	%t_kstack, %sp

	.globl	tsetup_srmmu_stackchk
tsetup_srmmu_stackchk:
	/* Check results of callers andcc %sp, 0x7, %g0 */
	bne	trap_setup_user_stack_is_bolixed
	 sethi   %hi(PAGE_OFFSET), %glob_tmp

	cmp	%glob_tmp, %sp
	bleu,a	1f
	 lda	[%g0] ASI_M_MMUREGS, %glob_tmp		! read MMU control

trap_setup_user_stack_is_bolixed:
	/* From user/kernel into invalid window w/bad user
	 * stack. Save bad user stack, and return to caller.
	 */
	SAVE_BOLIXED_USER_STACK(curptr, g3)
	restore	%g0, %g0, %g0

	jmpl	%t_retpc + 0x8, %g0
	 mov	%t_kstack, %sp

1:
	/* Clear the fault status and turn on the no_fault bit. */
	or	%glob_tmp, 0x2, %glob_tmp		! or in no_fault bit
	sta	%glob_tmp, [%g0] ASI_M_MMUREGS		! set it

	/* Dump the registers and cross fingers. */
	STORE_WINDOW(sp)

	/* Clear the no_fault bit and check the status. */
	andn	%glob_tmp, 0x2, %glob_tmp
	sta	%glob_tmp, [%g0] ASI_M_MMUREGS
	mov	AC_M_SFAR, %glob_tmp
	lda	[%glob_tmp] ASI_M_MMUREGS, %g0
	mov	AC_M_SFSR, %glob_tmp
	lda	[%glob_tmp] ASI_M_MMUREGS, %glob_tmp	! save away status of winstore
	andcc	%glob_tmp, 0x2, %g0			! did we fault?
	bne	trap_setup_user_stack_is_bolixed	! failure
	 nop

	restore %g0, %g0, %g0

	jmpl	%t_retpc + 0x8, %g0
	 mov	%t_kstack, %sp

