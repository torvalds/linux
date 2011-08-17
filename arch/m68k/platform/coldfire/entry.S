/*
 *  linux/arch/m68knommu/platform/5307/entry.S
 *
 *  Copyright (C) 1999-2007, Greg Ungerer (gerg@snapgear.com)
 *  Copyright (C) 1998  D. Jeff Dionne <jeff@lineo.ca>,
 *                      Kenneth Albanowski <kjahds@kjahds.com>,
 *  Copyright (C) 2000  Lineo Inc. (www.lineo.com)
 *  Copyright (C) 2004-2006  Macq Electronique SA. (www.macqel.com)
 *
 * Based on:
 *
 *  linux/arch/m68k/kernel/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 *
 * Linux/m68k support by Hamish Macdonald
 *
 * 68060 fixes by Jesper Skov
 * ColdFire support by Greg Ungerer (gerg@snapgear.com)
 * 5307 fixes by David W. Miller
 * linux 2.4 support David McCullough <davidm@snapgear.com>
 * Bug, speed and maintainability fixes by Philippe De Muyter <phdm@macqel.be>
 */

#include <linux/linkage.h>
#include <asm/unistd.h>
#include <asm/thread_info.h>
#include <asm/errno.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/asm-offsets.h>
#include <asm/entry.h>

#ifdef CONFIG_COLDFIRE_SW_A7
/*
 *	Define software copies of the supervisor and user stack pointers.
 */
.bss
sw_ksp:
.long	0
sw_usp:
.long	0
#endif /* CONFIG_COLDFIRE_SW_A7 */

.text

.globl system_call
.globl resume
.globl ret_from_exception
.globl ret_from_signal
.globl sys_call_table
.globl inthandler
.globl fasthandler

enosys:
	mov.l	#sys_ni_syscall,%d3
	bra	1f

ENTRY(system_call)
	SAVE_ALL
	move	#0x2000,%sr		/* enable intrs again */

	cmpl	#NR_syscalls,%d0
	jcc	enosys
	lea	sys_call_table,%a0
	lsll	#2,%d0			/* movel %a0@(%d0:l:4),%d3 */
	movel	%a0@(%d0),%d3
	jeq	enosys

1:
	movel	%sp,%d2			/* get thread_info pointer */
	andl	#-THREAD_SIZE,%d2	/* at start of kernel stack */
	movel	%d2,%a0
	movel	%a0@,%a1		/* save top of frame */
	movel	%sp,%a1@(TASK_THREAD+THREAD_ESP0)
	btst	#(TIF_SYSCALL_TRACE%8),%a0@(TINFO_FLAGS+(31-TIF_SYSCALL_TRACE)/8)
	bnes	1f

	movel	%d3,%a0
	jbsr	%a0@
	movel	%d0,%sp@(PT_OFF_D0)	/* save the return value */
	jra	ret_from_exception
1:
	movel	#-ENOSYS,%d2		/* strace needs -ENOSYS in PT_OFF_D0 */
	movel	%d2,PT_OFF_D0(%sp)	/* on syscall entry */
	subql	#4,%sp
	SAVE_SWITCH_STACK
	jbsr	syscall_trace_enter
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	movel	%d3,%a0
	jbsr	%a0@
	movel	%d0,%sp@(PT_OFF_D0)		/* save the return value */
	subql	#4,%sp			/* dummy return address */
	SAVE_SWITCH_STACK
	jbsr	syscall_trace_leave

ret_from_signal:
	RESTORE_SWITCH_STACK
	addql	#4,%sp

ret_from_exception:
	move	#0x2700,%sr		/* disable intrs */
	btst	#5,%sp@(PT_OFF_SR)	/* check if returning to kernel */
	jeq	Luser_return		/* if so, skip resched, signals */

#ifdef CONFIG_PREEMPT
	movel	%sp,%d1			/* get thread_info pointer */
	andl	#-THREAD_SIZE,%d1	/* at base of kernel stack */
	movel	%d1,%a0
	movel	%a0@(TINFO_FLAGS),%d1	/* get thread_info->flags */
	andl	#(1<<TIF_NEED_RESCHED),%d1
	jeq	Lkernel_return

	movel	%a0@(TINFO_PREEMPT),%d1
	cmpl	#0,%d1
	jne	Lkernel_return

	pea	Lkernel_return
	jmp	preempt_schedule_irq	/* preempt the kernel */
#endif

Lkernel_return:
	moveml	%sp@,%d1-%d5/%a0-%a2
	lea	%sp@(32),%sp		/* space for 8 regs */
	movel	%sp@+,%d0
	addql	#4,%sp			/* orig d0 */
	addl	%sp@+,%sp		/* stk adj */
	rte

Luser_return:
	movel	%sp,%d1			/* get thread_info pointer */
	andl	#-THREAD_SIZE,%d1	/* at base of kernel stack */
	movel	%d1,%a0
	movel	%a0@(TINFO_FLAGS),%d1	/* get thread_info->flags */
	jne	Lwork_to_do		/* still work to do */

Lreturn:
	RESTORE_USER

Lwork_to_do:
	movel	%a0@(TINFO_FLAGS),%d1	/* get thread_info->flags */
	move	#0x2000,%sr		/* enable intrs again */
	btst	#TIF_NEED_RESCHED,%d1
	jne	reschedule

	/* GERG: do we need something here for TRACEing?? */

Lsignal_return:
	subql	#4,%sp			/* dummy return address */
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	jsr	do_signal
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	jmp	Luser_return

/*
 * This is the generic interrupt handler (for all hardware interrupt
 * sources). Calls up to high level code to do all the work.
 */
ENTRY(inthandler)
	SAVE_ALL
	moveq	#-1,%d0
	movel	%d0,%sp@(PT_OFF_ORIG_D0)

	movew	%sp@(PT_OFF_FORMATVEC),%d0 /* put exception # in d0 */
	andl	#0x03fc,%d0		/* mask out vector only */

	movel	%sp,%sp@-		/* push regs arg */
	lsrl	#2,%d0			/* calculate real vector # */
	movel	%d0,%sp@-		/* push vector number */
	jbsr	do_IRQ			/* call high level irq handler */
	lea	%sp@(8),%sp		/* pop args off stack */

	bra	ret_from_exception

/*
 * Beware - when entering resume, prev (the current task) is
 * in a0, next (the new task) is in a1, so don't change these
 * registers until their contents are no longer needed.
 */
ENTRY(resume)
	movew	%sr,%d1				 /* save current status */
	movew	%d1,%a0@(TASK_THREAD+THREAD_SR)
	movel	%a0,%d1				 /* get prev thread in d1 */
	SAVE_SWITCH_STACK
	movel	%sp,%a0@(TASK_THREAD+THREAD_KSP) /* save kernel stack pointer */
	RDUSP					 /* movel %usp,%a3 */
	movel	%a3,%a0@(TASK_THREAD+THREAD_USP) /* save thread user stack */

	movel	%a1@(TASK_THREAD+THREAD_USP),%a3 /* restore thread user stack */
	WRUSP					 /* movel %a3,%usp */
	movel	%a1@(TASK_THREAD+THREAD_KSP),%sp /* restore new kernel stack */
	movew	%a1@(TASK_THREAD+THREAD_SR),%d7	 /* restore new status */
	movew	%d7,%sr
	RESTORE_SWITCH_STACK
	rts

