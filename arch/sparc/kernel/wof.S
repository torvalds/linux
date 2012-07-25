/*
 * wof.S: Sparc window overflow handler.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/contregs.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/psr.h>
#include <asm/smp.h>
#include <asm/asi.h>
#include <asm/winmacro.h>
#include <asm/asmmacro.h>
#include <asm/thread_info.h>

/* WARNING: This routine is hairy and _very_ complicated, but it
 *          must be as fast as possible as it handles the allocation
 *          of register windows to the user and kernel.  If you touch
 *          this code be _very_ careful as many other pieces of the
 *          kernel depend upon how this code behaves.  You have been
 *          duly warned...
 */

/* We define macro's for registers which have a fixed
 * meaning throughout this entire routine.  The 'T' in
 * the comments mean that the register can only be
 * accessed when in the 'trap' window, 'G' means
 * accessible in any window.  Do not change these registers
 * after they have been set, until you are ready to return
 * from the trap.
 */
#define t_psr       l0 /* %psr at trap time                     T */
#define t_pc        l1 /* PC for trap return                    T */
#define t_npc       l2 /* NPC for trap return                   T */
#define t_wim       l3 /* %wim at trap time                     T */
#define saved_g5    l5 /* Global save register                  T */
#define saved_g6    l6 /* Global save register                  T */
#define curptr      g6 /* Gets set to 'current' then stays      G */

/* Now registers whose values can change within the handler.      */
#define twin_tmp    l4 /* Temp reg, only usable in trap window  T */
#define glob_tmp    g5 /* Global temporary reg, usable anywhere G */

	.text
	.align	4
	/* BEGINNING OF PATCH INSTRUCTIONS */
	/* On a 7-window Sparc the boot code patches spnwin_*
	 * instructions with the following ones.
	 */
	.globl	spnwin_patch1_7win, spnwin_patch2_7win, spnwin_patch3_7win
spnwin_patch1_7win:	sll	%t_wim, 6, %glob_tmp
spnwin_patch2_7win:	and	%glob_tmp, 0x7f, %glob_tmp
spnwin_patch3_7win:	and	%twin_tmp, 0x7f, %twin_tmp
	/* END OF PATCH INSTRUCTIONS */

	/* The trap entry point has done the following:
	 *
	 * rd    %psr, %l0
	 * rd    %wim, %l3
	 * b     spill_window_entry
	 * andcc %l0, PSR_PS, %g0
	 */

	/* Datum current_thread_info->uwinmask contains at all times a bitmask
	 * where if any user windows are active, at least one bit will
	 * be set in to mask.  If no user windows are active, the bitmask
	 * will be all zeroes.
	 */
	.globl	spill_window_entry 
	.globl	spnwin_patch1, spnwin_patch2, spnwin_patch3
spill_window_entry:
	/* LOCATION: Trap Window */

	mov	%g5, %saved_g5		! save away global temp register
	mov	%g6, %saved_g6		! save away 'current' ptr register

	/* Compute what the new %wim will be if we save the
	 * window properly in this trap handler.
	 *
	 * newwim = ((%wim>>1) | (%wim<<(nwindows - 1)));
	 */
		srl	%t_wim, 0x1, %twin_tmp
spnwin_patch1:	sll	%t_wim, 7, %glob_tmp
		or	%glob_tmp, %twin_tmp, %glob_tmp
spnwin_patch2:	and	%glob_tmp, 0xff, %glob_tmp

	/* The trap entry point has set the condition codes
	 * up for us to see if this is from user or kernel.
	 * Get the load of 'curptr' out of the way.
	 */
	LOAD_CURRENT(curptr, twin_tmp)

	andcc	%t_psr, PSR_PS, %g0
	be,a	spwin_fromuser				! all user wins, branch
	 save	%g0, %g0, %g0				! Go where saving will occur
	
	/* See if any user windows are active in the set. */
	ld	[%curptr + TI_UWINMASK], %twin_tmp	! grab win mask
	orcc	%g0, %twin_tmp, %g0			! check for set bits
	bne	spwin_exist_uwins			! yep, there are some
	 andn	%twin_tmp, %glob_tmp, %twin_tmp		! compute new uwinmask

	/* Save into the window which must be saved and do it.
	 * Basically if we are here, this means that we trapped
	 * from kernel mode with only kernel windows in the register
	 * file.
	 */
	save	%g0, %g0, %g0		! save into the window to stash away
	wr	%glob_tmp, 0x0, %wim	! set new %wim, this is safe now

spwin_no_userwins_from_kernel:
	/* LOCATION: Window to be saved */

	STORE_WINDOW(sp)		! stash the window
	restore	%g0, %g0, %g0		! go back into trap window

	/* LOCATION: Trap window */
	mov	%saved_g5, %g5		! restore %glob_tmp
	mov	%saved_g6, %g6		! restore %curptr
	wr	%t_psr, 0x0, %psr	! restore condition codes in %psr
	WRITE_PAUSE			! waste some time
	jmp	%t_pc			! Return from trap
	rett	%t_npc			! we are done

spwin_exist_uwins:
	/* LOCATION: Trap window */

	/* Wow, user windows have to be dealt with, this is dirty
	 * and messy as all hell.  And difficult to follow if you
	 * are approaching the infamous register window trap handling
	 * problem for the first time. DON'T LOOK!
	 *
	 * Note that how the execution path works out, the new %wim
	 * will be left for us in the global temporary register,
	 * %glob_tmp.  We cannot set the new %wim first because we
	 * need to save into the appropriate window without inducing
	 * a trap (traps are off, we'd get a watchdog wheee)...
	 * But first, store the new user window mask calculated
	 * above.
	 */
	st	%twin_tmp, [%curptr + TI_UWINMASK]
	save	%g0, %g0, %g0		! Go to where the saving will occur

spwin_fromuser:
	/* LOCATION: Window to be saved */
	wr	%glob_tmp, 0x0, %wim	! Now it is safe to set new %wim

	/* LOCATION: Window to be saved */

	/* This instruction branches to a routine which will check
	 * to validity of the users stack pointer by whatever means
	 * are necessary.  This means that this is architecture
	 * specific and thus this branch instruction will need to
	 * be patched at boot time once the machine type is known.
	 * This routine _shall not_ touch %curptr under any
	 * circumstances whatsoever!  It will branch back to the
	 * label 'spwin_good_ustack' if the stack is ok but still
	 * needs to be dumped (SRMMU for instance will not need to
	 * do this) or 'spwin_finish_up' if the stack is ok and the
	 * registers have already been saved.  If the stack is found
	 * to be bogus for some reason the routine shall branch to
	 * the label 'spwin_user_stack_is_bolixed' which will take
	 * care of things at that point.
	 */
	b	spwin_srmmu_stackchk
	 andcc	%sp, 0x7, %g0

spwin_good_ustack:
	/* LOCATION: Window to be saved */

	/* The users stack is ok and we can safely save it at
	 * %sp.
	 */
	STORE_WINDOW(sp)

spwin_finish_up:
	restore	%g0, %g0, %g0		/* Back to trap window. */

	/* LOCATION: Trap window */

	/* We have spilled successfully, and we have properly stored
	 * the appropriate window onto the stack.
	 */

	/* Restore saved globals */
	mov	%saved_g5, %g5
	mov	%saved_g6, %g6

	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE
	jmp	%t_pc
	rett	%t_npc

spwin_user_stack_is_bolixed:
	/* LOCATION: Window to be saved */

	/* Wheee, user has trashed his/her stack.  We have to decide
	 * how to proceed based upon whether we came from kernel mode
	 * or not.  If we came from kernel mode, toss the window into
	 * a special buffer and proceed, the kernel _needs_ a window
	 * and we could be in an interrupt handler so timing is crucial.
	 * If we came from user land we build a full stack frame and call
	 * c-code to gun down the process.
	 */
	rd	%psr, %glob_tmp
	andcc	%glob_tmp, PSR_PS, %g0
	bne	spwin_bad_ustack_from_kernel
	 nop

	/* Oh well, throw this one window into the per-task window
	 * buffer, the first one.
	 */
	st	%sp, [%curptr + TI_RWIN_SPTRS]
	STORE_WINDOW(curptr + TI_REG_WINDOW)
	restore	%g0, %g0, %g0

	/* LOCATION: Trap Window */

	/* Back in the trap window, update winbuffer save count. */
	mov	1, %twin_tmp
	st	%twin_tmp, [%curptr + TI_W_SAVED]

		/* Compute new user window mask.  What we are basically
		 * doing is taking two windows, the invalid one at trap
		 * time and the one we attempted to throw onto the users
		 * stack, and saying that everything else is an ok user
		 * window.  umask = ((~(%t_wim | %wim)) & valid_wim_bits)
		 */
		rd	%wim, %twin_tmp
		or	%twin_tmp, %t_wim, %twin_tmp
		not	%twin_tmp
spnwin_patch3:	and	%twin_tmp, 0xff, %twin_tmp	! patched on 7win Sparcs
		st	%twin_tmp, [%curptr + TI_UWINMASK]

#define STACK_OFFSET (THREAD_SIZE - TRACEREG_SZ - STACKFRAME_SZ)

	sethi	%hi(STACK_OFFSET), %sp
	or	%sp, %lo(STACK_OFFSET), %sp
	add	%curptr, %sp, %sp

	/* Restore the saved globals and build a pt_regs frame. */
	mov	%saved_g5, %g5
	mov	%saved_g6, %g6
	STORE_PT_ALL(sp, t_psr, t_pc, t_npc, g1)

	sethi	%hi(STACK_OFFSET), %g6
	or	%g6, %lo(STACK_OFFSET), %g6
	sub	%sp, %g6, %g6		! curptr

	/* Turn on traps and call c-code to deal with it. */
	wr	%t_psr, PSR_ET, %psr
	nop
	call	window_overflow_fault
	 nop

	/* Return from trap if C-code actually fixes things, if it
	 * doesn't then we never get this far as the process will
	 * be given the look of death from Commander Peanut.
	 */
	b	ret_trap_entry
	 clr	%l6

spwin_bad_ustack_from_kernel:
	/* LOCATION: Window to be saved */

	/* The kernel provoked a spill window trap, but the window we
	 * need to save is a user one and the process has trashed its
	 * stack pointer.  We need to be quick, so we throw it into
	 * a per-process window buffer until we can properly handle
	 * this later on.
	 */
	SAVE_BOLIXED_USER_STACK(curptr, glob_tmp)
	restore	%g0, %g0, %g0

	/* LOCATION: Trap window */

	/* Restore globals, condition codes in the %psr and
	 * return from trap.  Note, restoring %g6 when returning
	 * to kernel mode is not necessarily these days. ;-)
	 */
	mov	%saved_g5, %g5
	mov	%saved_g6, %g6

	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE

	jmp	%t_pc
	rett	%t_npc

/* Undefine the register macros which would only cause trouble
 * if used below.  This helps find 'stupid' coding errors that
 * produce 'odd' behavior.  The routines below are allowed to
 * make usage of glob_tmp and t_psr so we leave them defined.
 */
#undef twin_tmp
#undef curptr
#undef t_pc
#undef t_npc
#undef t_wim
#undef saved_g5
#undef saved_g6

/* Now come the per-architecture window overflow stack checking routines.
 * As noted above %curptr cannot be touched by this routine at all.
 */

	/* This is a generic SRMMU routine.  As far as I know this
	 * works for all current v8/srmmu implementations, we'll
	 * see...
	 */
	.globl	spwin_srmmu_stackchk
spwin_srmmu_stackchk:
	/* LOCATION: Window to be saved on the stack */

	/* Because of SMP concerns and speed we play a trick.
	 * We disable fault traps in the MMU control register,
	 * Execute the stores, then check the fault registers
	 * to see what happens.  I can hear Linus now
	 * "disgusting... broken hardware...".
	 *
	 * But first, check to see if the users stack has ended
	 * up in kernel vma, then we would succeed for the 'wrong'
	 * reason... ;(  Note that the 'sethi' below assumes the
	 * kernel is page aligned, which should always be the case.
	 */
	/* Check results of callers andcc %sp, 0x7, %g0 */
	bne	spwin_user_stack_is_bolixed
	 sethi   %hi(PAGE_OFFSET), %glob_tmp
	cmp	%glob_tmp, %sp
	bleu	spwin_user_stack_is_bolixed
	 mov	AC_M_SFSR, %glob_tmp

	/* Clear the fault status and turn on the no_fault bit. */
LEON_PI(lda	[%glob_tmp] ASI_LEON_MMUREGS, %g0)	! eat SFSR
SUN_PI_(lda	[%glob_tmp] ASI_M_MMUREGS, %g0)		! eat SFSR

LEON_PI(lda	[%g0] ASI_LEON_MMUREGS, %glob_tmp)	! read MMU control
SUN_PI_(lda	[%g0] ASI_M_MMUREGS, %glob_tmp)		! read MMU control
	or	%glob_tmp, 0x2, %glob_tmp		! or in no_fault bit
LEON_PI(sta	%glob_tmp, [%g0] ASI_LEON_MMUREGS)	! set it
SUN_PI_(sta	%glob_tmp, [%g0] ASI_M_MMUREGS)		! set it

	/* Dump the registers and cross fingers. */
	STORE_WINDOW(sp)

	/* Clear the no_fault bit and check the status. */
	andn	%glob_tmp, 0x2, %glob_tmp
LEON_PI(sta	%glob_tmp, [%g0] ASI_LEON_MMUREGS)
SUN_PI_(sta	%glob_tmp, [%g0] ASI_M_MMUREGS)

	mov	AC_M_SFAR, %glob_tmp
LEON_PI(lda	[%glob_tmp] ASI_LEON_MMUREGS, %g0)
SUN_PI_(lda	[%glob_tmp] ASI_M_MMUREGS, %g0)

	mov	AC_M_SFSR, %glob_tmp
LEON_PI(lda	[%glob_tmp] ASI_LEON_MMUREGS, %glob_tmp)
SUN_PI_(lda	[%glob_tmp] ASI_M_MMUREGS, %glob_tmp)
	andcc	%glob_tmp, 0x2, %g0			! did we fault?
	be,a	spwin_finish_up + 0x4			! cool beans, success
	 restore %g0, %g0, %g0

	rd	%psr, %glob_tmp
	b	spwin_user_stack_is_bolixed + 0x4	! we faulted, ugh
	 nop
