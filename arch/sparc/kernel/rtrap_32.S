/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rtrap.S: Return from Sparc trap low-level code.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/psr.h>
#include <asm/asi.h>
#include <asm/smp.h>
#include <asm/contregs.h>
#include <asm/winmacro.h>
#include <asm/asmmacro.h>
#include <asm/thread_info.h>

#define t_psr     l0
#define t_pc      l1
#define t_npc     l2
#define t_wim     l3
#define twin_tmp1 l4
#define glob_tmp  g4
#define curptr    g6

	/* 7 WINDOW SPARC PATCH INSTRUCTIONS */
	.globl	rtrap_7win_patch1, rtrap_7win_patch2, rtrap_7win_patch3
	.globl	rtrap_7win_patch4, rtrap_7win_patch5
rtrap_7win_patch1:	srl	%t_wim, 0x6, %glob_tmp
rtrap_7win_patch2:	and	%glob_tmp, 0x7f, %glob_tmp
rtrap_7win_patch3:	srl	%g1, 7, %g2
rtrap_7win_patch4:	srl	%g2, 6, %g2
rtrap_7win_patch5:	and	%g1, 0x7f, %g1
	/* END OF PATCH INSTRUCTIONS */

	/* We need to check for a few things which are:
	 * 1) The need to call schedule() because this
	 *    processes quantum is up.
	 * 2) Pending signals for this process, if any
	 *    exist we need to call do_signal() to do
	 *    the needy.
	 *
	 * Else we just check if the rett would land us
	 * in an invalid window, if so we need to grab
	 * it off the user/kernel stack first.
	 */

	.globl	ret_trap_entry, rtrap_patch1, rtrap_patch2
	.globl	rtrap_patch3, rtrap_patch4, rtrap_patch5
	.globl	ret_trap_lockless_ipi
ret_trap_entry:
ret_trap_lockless_ipi:
	andcc	%t_psr, PSR_PS, %g0
	sethi	%hi(PSR_SYSCALL), %g1
	be	1f
	 andn	%t_psr, %g1, %t_psr

	wr	%t_psr, 0x0, %psr
	b	ret_trap_kernel
	 nop

1:
	ld	[%curptr + TI_FLAGS], %g2
	andcc	%g2, (_TIF_NEED_RESCHED), %g0
	be	signal_p
	 nop

	call	schedule
	 nop

	ld	[%curptr + TI_FLAGS], %g2
signal_p:
	andcc	%g2, _TIF_DO_NOTIFY_RESUME_MASK, %g0
	bz,a	ret_trap_continue
	 ld	[%sp + STACKFRAME_SZ + PT_PSR], %t_psr

	mov	%g2, %o2
	mov	%l6, %o1
	call	do_notify_resume
	 add	%sp, STACKFRAME_SZ, %o0	! pt_regs ptr

	b	signal_p
	 ld	[%curptr + TI_FLAGS], %g2

ret_trap_continue:
	sethi	%hi(PSR_SYSCALL), %g1
	andn	%t_psr, %g1, %t_psr
	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE

	ld	[%curptr + TI_W_SAVED], %twin_tmp1
	orcc	%g0, %twin_tmp1, %g0
	be	ret_trap_nobufwins
	 nop

	wr	%t_psr, PSR_ET, %psr
	WRITE_PAUSE

	mov	1, %o1
	call	try_to_clear_window_buffer
	 add	%sp, STACKFRAME_SZ, %o0

	b	signal_p
	 ld	[%curptr + TI_FLAGS], %g2

ret_trap_nobufwins:
	/* Load up the user's out registers so we can pull
	 * a window from the stack, if necessary.
	 */
	LOAD_PT_INS(sp)

	/* If there are already live user windows in the
	 * set we can return from trap safely.
	 */
	ld	[%curptr + TI_UWINMASK], %twin_tmp1
	orcc	%g0, %twin_tmp1, %g0
	bne	ret_trap_userwins_ok
	 nop

		/* Calculate new %wim, we have to pull a register
		 * window from the users stack.
		 */
ret_trap_pull_one_window:
		rd	%wim, %t_wim
		sll	%t_wim, 0x1, %twin_tmp1
rtrap_patch1:	srl	%t_wim, 0x7, %glob_tmp
		or	%glob_tmp, %twin_tmp1, %glob_tmp
rtrap_patch2:	and	%glob_tmp, 0xff, %glob_tmp

		wr	%glob_tmp, 0x0, %wim

	/* Here comes the architecture specific
	 * branch to the user stack checking routine
	 * for return from traps.
	 */
	b	srmmu_rett_stackchk
	 andcc	%fp, 0x7, %g0

ret_trap_userwins_ok:
	LOAD_PT_PRIV(sp, t_psr, t_pc, t_npc)
	or	%t_pc, %t_npc, %g2
	andcc	%g2, 0x3, %g0
	sethi	%hi(PSR_SYSCALL), %g2
	be	1f
	 andn	%t_psr, %g2, %t_psr

	b	ret_trap_unaligned_pc
	 add	%sp, STACKFRAME_SZ, %o0

1:
	LOAD_PT_YREG(sp, g1)
	LOAD_PT_GLOBALS(sp)

	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE

	jmp	%t_pc
	rett	%t_npc
	
ret_trap_unaligned_pc:
	ld	[%sp + STACKFRAME_SZ + PT_PC], %o1
	ld	[%sp + STACKFRAME_SZ + PT_NPC], %o2
	ld	[%sp + STACKFRAME_SZ + PT_PSR], %o3

	wr	%t_wim, 0x0, %wim		! or else...

	wr	%t_psr, PSR_ET, %psr
	WRITE_PAUSE

	call	do_memaccess_unaligned
	 nop

	b	signal_p
	 ld	[%curptr + TI_FLAGS], %g2

ret_trap_kernel:
		/* Will the rett land us in the invalid window? */
		mov	2, %g1
		sll	%g1, %t_psr, %g1
rtrap_patch3:	srl	%g1, 8, %g2
		or	%g1, %g2, %g1
		rd	%wim, %g2
		andcc	%g2, %g1, %g0
		be	1f		! Nope, just return from the trap
		 sll	%g2, 0x1, %g1

		/* We have to grab a window before returning. */
rtrap_patch4:	srl	%g2, 7,  %g2
		or	%g1, %g2, %g1
rtrap_patch5:	and	%g1, 0xff, %g1

	wr	%g1, 0x0, %wim

	/* Grrr, make sure we load from the right %sp... */
	LOAD_PT_ALL(sp, t_psr, t_pc, t_npc, g1)

	restore	%g0, %g0, %g0
	LOAD_WINDOW(sp)
	b	2f
	 save	%g0, %g0, %g0

	/* Reload the entire frame in case this is from a
	 * kernel system call or whatever...
	 */
1:
	LOAD_PT_ALL(sp, t_psr, t_pc, t_npc, g1)
2:
	sethi	%hi(PSR_SYSCALL), %twin_tmp1
	andn	%t_psr, %twin_tmp1, %t_psr
	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE

	jmp	%t_pc
	rett	%t_npc

ret_trap_user_stack_is_bolixed:
	wr	%t_wim, 0x0, %wim

	wr	%t_psr, PSR_ET, %psr
	WRITE_PAUSE

	call	window_ret_fault
	 add	%sp, STACKFRAME_SZ, %o0

	b	signal_p
	 ld	[%curptr + TI_FLAGS], %g2

	.globl	srmmu_rett_stackchk
srmmu_rett_stackchk:
	bne	ret_trap_user_stack_is_bolixed
	 sethi   %hi(PAGE_OFFSET), %g1
	cmp	%g1, %fp
	bleu	ret_trap_user_stack_is_bolixed
	 mov	AC_M_SFSR, %g1
LEON_PI(lda	[%g1] ASI_LEON_MMUREGS, %g0)
SUN_PI_(lda	[%g1] ASI_M_MMUREGS, %g0)

LEON_PI(lda	[%g0] ASI_LEON_MMUREGS, %g1)
SUN_PI_(lda	[%g0] ASI_M_MMUREGS, %g1)
	or	%g1, 0x2, %g1
LEON_PI(sta	%g1, [%g0] ASI_LEON_MMUREGS)
SUN_PI_(sta	%g1, [%g0] ASI_M_MMUREGS)

	restore	%g0, %g0, %g0

	LOAD_WINDOW(sp)

	save	%g0, %g0, %g0

	andn	%g1, 0x2, %g1
LEON_PI(sta	%g1, [%g0] ASI_LEON_MMUREGS)
SUN_PI_(sta	%g1, [%g0] ASI_M_MMUREGS)

	mov	AC_M_SFAR, %g2
LEON_PI(lda	[%g2] ASI_LEON_MMUREGS, %g2)
SUN_PI_(lda	[%g2] ASI_M_MMUREGS, %g2)

	mov	AC_M_SFSR, %g1
LEON_PI(lda	[%g1] ASI_LEON_MMUREGS, %g1)
SUN_PI_(lda	[%g1] ASI_M_MMUREGS, %g1)
	andcc	%g1, 0x2, %g0
	be	ret_trap_userwins_ok
	 nop

	b,a	ret_trap_user_stack_is_bolixed
