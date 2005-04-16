/* $Id: wuf.S,v 1.39 2000/01/08 16:38:18 anton Exp $
 * wuf.S: Window underflow trap handler for the Sparc.
 *
 * Copyright (C) 1995 David S. Miller
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

/* Just like the overflow handler we define macros for registers
 * with fixed meanings in this routine.
 */
#define t_psr       l0
#define t_pc        l1
#define t_npc       l2
#define t_wim       l3
/* Don't touch the above registers or else you die horribly... */

/* Now macros for the available scratch registers in this routine. */
#define twin_tmp1    l4
#define twin_tmp2    l5

#define curptr       g6

	.text
	.align	4

	/* The trap entry point has executed the following:
	 *
	 * rd    %psr, %l0
	 * rd    %wim, %l3
	 * b     fill_window_entry
	 * andcc %l0, PSR_PS, %g0
	 */

	/* Datum current_thread_info->uwinmask contains at all times a bitmask
	 * where if any user windows are active, at least one bit will
	 * be set in to mask.  If no user windows are active, the bitmask
	 * will be all zeroes.
	 */

	/* To get an idea of what has just happened to cause this
	 * trap take a look at this diagram:
	 *
	 *      1  2  3  4     <--  Window number
	 *      ----------
	 *      T  O  W  I     <--  Symbolic name
	 *
	 *      O == the window that execution was in when
	 *           the restore was attempted
	 *
	 *      T == the trap itself has save'd us into this
	 *           window
	 *
	 *      W == this window is the one which is now invalid
	 *           and must be made valid plus loaded from the
	 *           stack
	 *
	 *      I == this window will be the invalid one when we
	 *           are done and return from trap if successful
	 */

	/* BEGINNING OF PATCH INSTRUCTIONS */

	/* On 7-window Sparc the boot code patches fnwin_patch1
	 * with the following instruction.
	 */
	.globl	fnwin_patch1_7win, fnwin_patch2_7win
fnwin_patch1_7win:	srl	%t_wim, 6, %twin_tmp2
fnwin_patch2_7win:	and	%twin_tmp1, 0x7f, %twin_tmp1
	/* END OF PATCH INSTRUCTIONS */

	.globl	fill_window_entry, fnwin_patch1, fnwin_patch2
fill_window_entry:
	/* LOCATION: Window 'T' */

	/* Compute what the new %wim is going to be if we retrieve
	 * the proper window off of the stack.
	 */
		sll	%t_wim, 1, %twin_tmp1
fnwin_patch1:	srl	%t_wim, 7, %twin_tmp2
		or	%twin_tmp1, %twin_tmp2, %twin_tmp1
fnwin_patch2:	and	%twin_tmp1, 0xff, %twin_tmp1

	wr	%twin_tmp1, 0x0, %wim	/* Make window 'I' invalid */

	andcc	%t_psr, PSR_PS, %g0
	be	fwin_from_user
	 restore	%g0, %g0, %g0		/* Restore to window 'O' */

	/* Trapped from kernel, we trust that the kernel does not
	 * 'over restore' sorta speak and just grab the window
	 * from the stack and return.  Easy enough.
	 */
fwin_from_kernel:
	/* LOCATION: Window 'O' */

	restore %g0, %g0, %g0

	/* LOCATION: Window 'W' */

	LOAD_WINDOW(sp)	                /* Load it up */

	/* Spin the wheel... */
	save	%g0, %g0, %g0
	save	%g0, %g0, %g0
	/* I'd like to buy a vowel please... */

	/* LOCATION: Window 'T' */

	/* Now preserve the condition codes in %psr, pause, and
	 * return from trap.  This is the simplest case of all.
	 */
	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE

	jmp	%t_pc
	rett	%t_npc

fwin_from_user:
	/* LOCATION: Window 'O' */

	restore	%g0, %g0, %g0		/* Restore to window 'W' */

	/* LOCATION: Window 'W' */

	/* Branch to the architecture specific stack validation
	 * routine.  They can be found below...
	 */
	.globl	fwin_mmu_patchme
fwin_mmu_patchme:	b	sun4c_fwin_stackchk
				 andcc	%sp, 0x7, %g0

#define STACK_OFFSET (THREAD_SIZE - TRACEREG_SZ - STACKFRAME_SZ)

fwin_user_stack_is_bolixed:
	/* LOCATION: Window 'W' */

	/* Place a pt_regs frame on the kernel stack, save back
	 * to the trap window and call c-code to deal with this.
	 */
	LOAD_CURRENT(l4, l5)

	sethi	%hi(STACK_OFFSET), %l5
	or	%l5, %lo(STACK_OFFSET), %l5
	add	%l4, %l5, %l5

	/* Store globals into pt_regs frame. */
	STORE_PT_GLOBALS(l5)
	STORE_PT_YREG(l5, g3)

	/* Save current in a global while we change windows. */
	mov	%l4, %curptr

	save	%g0, %g0, %g0

	/* LOCATION: Window 'O' */

	rd	%psr, %g3		/* Read %psr in live user window */
	mov	%fp, %g4		/* Save bogus frame pointer. */

	save	%g0, %g0, %g0

	/* LOCATION: Window 'T' */

	sethi	%hi(STACK_OFFSET), %l5
	or	%l5, %lo(STACK_OFFSET), %l5
	add	%curptr, %l5, %sp

	/* Build rest of pt_regs. */
	STORE_PT_INS(sp)
	STORE_PT_PRIV(sp, t_psr, t_pc, t_npc)

	/* re-set trap time %wim value */
	wr	%t_wim, 0x0, %wim

	/* Fix users window mask and buffer save count. */
	mov	0x1, %g5
	sll	%g5, %g3, %g5
	st	%g5, [%curptr + TI_UWINMASK]		! one live user window still
	st	%g0, [%curptr + TI_W_SAVED]		! no windows in the buffer

	wr	%t_psr, PSR_ET, %psr			! enable traps
	nop
	call	window_underflow_fault
	 mov	%g4, %o0

	b	ret_trap_entry
	 clr	%l6

fwin_user_stack_is_ok:
	/* LOCATION: Window 'W' */

	/* The users stack area is kosher and mapped, load the
	 * window and fall through to the finish up routine.
	 */
	LOAD_WINDOW(sp)

	/* Round and round she goes... */
	save	%g0, %g0, %g0		/* Save to window 'O' */
	save	%g0, %g0, %g0		/* Save to window 'T' */
	/* Where she'll trap nobody knows... */

	/* LOCATION: Window 'T' */

fwin_user_finish_up:
	/* LOCATION: Window 'T' */

	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE	

	jmp	%t_pc
	rett	%t_npc

	/* Here come the architecture specific checks for stack.
	 * mappings.  Note that unlike the window overflow handler
	 * we only need to check whether the user can read from
	 * the appropriate addresses.  Also note that we are in
	 * an invalid window which will be loaded, and this means
	 * that until we actually load the window up we are free
	 * to use any of the local registers contained within.
	 *
	 * On success these routine branch to fwin_user_stack_is_ok
	 * if the area at %sp is user readable and the window still
	 * needs to be loaded, else fwin_user_finish_up if the
	 * routine has done the loading itself.  On failure (bogus
	 * user stack) the routine shall branch to the label called
	 * fwin_user_stack_is_bolixed.
	 *
	 * Contrary to the arch-specific window overflow stack
	 * check routines in wof.S, these routines are free to use
	 * any of the local registers they want to as this window
	 * does not belong to anyone at this point, however the
	 * outs and ins are still verboten as they are part of
	 * 'someone elses' window possibly.
	 */

	.align	4
	.globl	sun4c_fwin_stackchk
sun4c_fwin_stackchk:
	/* LOCATION: Window 'W' */

	/* Caller did 'andcc %sp, 0x7, %g0' */
	be	1f
	 and	%sp, 0xfff, %l0		! delay slot

	b,a	fwin_user_stack_is_bolixed

	/* See if we have to check the sanity of one page or two */
1:
	add	%l0, 0x38, %l0
	sra	%sp, 29, %l5
	add	%l5, 0x1, %l5
	andncc	%l5, 0x1, %g0
	be	1f
	 andncc	%l0, 0xff8, %g0

	b,a	fwin_user_stack_is_bolixed	/* %sp is in vma hole, yuck */

1:
	be	sun4c_fwin_onepage	/* Only one page to check */
	 lda	[%sp] ASI_PTE, %l1
sun4c_fwin_twopages:
	add	%sp, 0x38, %l0
	sra	%l0, 29, %l5
	add	%l5, 0x1, %l5
	andncc	%l5, 0x1, %g0
	be	1f
	 lda	[%l0] ASI_PTE, %l1

	b,a	fwin_user_stack_is_bolixed	/* Second page in vma hole */

1:
	srl	%l1, 29, %l1
	andcc	%l1, 0x4, %g0
	bne	sun4c_fwin_onepage
	 lda	[%sp] ASI_PTE, %l1	

	b,a	fwin_user_stack_is_bolixed	/* Second page has bad perms */

sun4c_fwin_onepage:
	srl	%l1, 29, %l1
	andcc	%l1, 0x4, %g0
	bne	fwin_user_stack_is_ok
	 nop

	/* A page had bad page permissions, losing... */
	b,a	fwin_user_stack_is_bolixed

	.globl	srmmu_fwin_stackchk
srmmu_fwin_stackchk:
	/* LOCATION: Window 'W' */

	/* Caller did 'andcc %sp, 0x7, %g0' */
	bne	fwin_user_stack_is_bolixed
	 sethi   %hi(PAGE_OFFSET), %l5

	/* Check if the users stack is in kernel vma, then our
	 * trial and error technique below would succeed for
	 * the 'wrong' reason.
	 */
	mov	AC_M_SFSR, %l4
	cmp	%l5, %sp
	bleu	fwin_user_stack_is_bolixed
	 lda	[%l4] ASI_M_MMUREGS, %g0	! clear fault status

	/* The technique is, turn off faults on this processor,
	 * just let the load rip, then check the sfsr to see if
	 * a fault did occur.  Then we turn on fault traps again
	 * and branch conditionally based upon what happened.
	 */
	lda	[%g0] ASI_M_MMUREGS, %l5	! read mmu-ctrl reg
	or	%l5, 0x2, %l5			! turn on no-fault bit
	sta	%l5, [%g0] ASI_M_MMUREGS	! store it

	/* Cross fingers and go for it. */
	LOAD_WINDOW(sp)

	/* A penny 'saved'... */
	save	%g0, %g0, %g0
	save	%g0, %g0, %g0
	/* Is a BADTRAP earned... */

	/* LOCATION: Window 'T' */

	lda	[%g0] ASI_M_MMUREGS, %twin_tmp1	! load mmu-ctrl again
	andn	%twin_tmp1, 0x2, %twin_tmp1	! clear no-fault bit
	sta	%twin_tmp1, [%g0] ASI_M_MMUREGS	! store it

	mov	AC_M_SFAR, %twin_tmp2
	lda	[%twin_tmp2] ASI_M_MMUREGS, %g0	! read fault address

	mov	AC_M_SFSR, %twin_tmp2
	lda	[%twin_tmp2] ASI_M_MMUREGS, %twin_tmp2	! read fault status
	andcc	%twin_tmp2, 0x2, %g0			! did fault occur?

	bne	1f					! yep, cleanup
	 nop

	wr	%t_psr, 0x0, %psr
	nop
	b	fwin_user_finish_up + 0x4
	 nop

	/* Did I ever tell you about my window lobotomy?
	 * anyways... fwin_user_stack_is_bolixed expects
	 * to be in window 'W' so make it happy or else
	 * we watchdog badly.
	 */
1:
	restore	%g0, %g0, %g0
	b	fwin_user_stack_is_bolixed	! oh well
	 restore	%g0, %g0, %g0
