/*
 *  linux/arch/m68knommu/platform/68360/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2001 SED Systems, a Division of Calian Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 *
 * Linux/m68k support by Hamish Macdonald
 * M68360 Port by SED Systems, and Lineo.
 */

#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/traps.h>
#include <asm/asm-offsets.h>
#include <asm/entry.h>

.text

.globl system_call
.globl resume
.globl ret_from_exception
.globl ret_from_signal
.globl sys_call_table
.globl ret_from_interrupt
.globl bad_interrupt
.globl inthandler

badsys:
	movel	#-ENOSYS,%sp@(PT_OFF_D0)
	jra	ret_from_exception

do_trace:
	movel	#-ENOSYS,%sp@(PT_OFF_D0) /* needed for strace*/
	subql	#4,%sp
	SAVE_SWITCH_STACK
	jbsr	syscall_trace_enter
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	movel	%sp@(PT_OFF_ORIG_D0),%d1
	movel	#-ENOSYS,%d0
	cmpl	#NR_syscalls,%d1
	jcc	1f
	lsl	#2,%d1
	lea	sys_call_table, %a0
	jbsr	%a0@(%d1)

1:	movel	%d0,%sp@(PT_OFF_D0)	/* save the return value */
	subql	#4,%sp			/* dummy return address */
	SAVE_SWITCH_STACK
	jbsr	syscall_trace_leave

ret_from_signal:
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	jra	ret_from_exception

ENTRY(system_call)
	SAVE_ALL_SYS

	/* save top of frame*/
	pea	%sp@
	jbsr	set_esp0
	addql	#4,%sp

	movel	%sp@(PT_OFF_ORIG_D0),%d0

	movel	%sp,%d1			/* get thread_info pointer */
	andl	#-THREAD_SIZE,%d1
	movel	%d1,%a2
	btst	#(TIF_SYSCALL_TRACE%8),%a2@(TINFO_FLAGS+(31-TIF_SYSCALL_TRACE)/8)
	jne	do_trace
	cmpl	#NR_syscalls,%d0
	jcc	badsys
	lsl	#2,%d0
	lea	sys_call_table,%a0
	movel	%a0@(%d0), %a0
	jbsr	%a0@
	movel	%d0,%sp@(PT_OFF_D0)	/* save the return value*/

ret_from_exception:
	btst	#5,%sp@(PT_OFF_SR)	/* check if returning to kernel*/
	jeq	Luser_return		/* if so, skip resched, signals*/

Lkernel_return:
	RESTORE_ALL

Luser_return:
	/* only allow interrupts when we are really the last one on the*/
	/* kernel stack, otherwise stack overflow can occur during*/
	/* heavy interrupt load*/
	andw	#ALLOWINT,%sr

	movel	%sp,%d1			/* get thread_info pointer */
	andl	#-THREAD_SIZE,%d1
	movel	%d1,%a2
1:
	move	%a2@(TINFO_FLAGS),%d1	/* thread_info->flags */
	jne	Lwork_to_do
	RESTORE_ALL

Lwork_to_do:
	movel	%a2@(TINFO_FLAGS),%d1	/* thread_info->flags */
	btst	#TIF_NEED_RESCHED,%d1
	jne	reschedule

Lsignal_return:
	subql	#4,%sp			/* dummy return address*/
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	bsrw	do_signal
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	jra	1b

/*
 * This is the main interrupt handler, responsible for calling do_IRQ()
 */
inthandler:
	SAVE_ALL_INT
	movew	%sp@(PT_OFF_FORMATVEC), %d0
	and.l	#0x3ff, %d0
	lsr.l   #0x02,  %d0

	movel	%sp,%sp@-
	movel	%d0,%sp@- 		/*  put vector # on stack*/
	jbsr	do_IRQ			/*  process the IRQ*/
3:     	addql	#8,%sp			/*  pop parameters off stack*/
	bra	ret_from_interrupt

ret_from_interrupt:
	jeq	1f
2:
	RESTORE_ALL
1:
	moveb	%sp@(PT_OFF_SR), %d0
	and	#7, %d0
	jhi	2b
	/* check if we need to do software interrupts */

	movel	irq_stat+CPUSTAT_SOFTIRQ_PENDING,%d0
	jeq	ret_from_exception

	pea	ret_from_exception
	jra	do_softirq


/*
 * Handler for uninitialized and spurious interrupts.
 */
bad_interrupt:
	addql	#1,irq_err_count
	rte

/*
 * Beware - when entering resume, prev (the current task) is
 * in a0, next (the new task) is in a1, so don't change these
 * registers until their contents are no longer needed.
 */
ENTRY(resume)
	movel	%a0,%d1				/* save prev thread in d1 */
	movew	%sr,%a0@(TASK_THREAD+THREAD_SR)	/* save sr */
	SAVE_SWITCH_STACK
	movel	%sp,%a0@(TASK_THREAD+THREAD_KSP) /* save kernel stack */
	movel	%usp,%a3			/* save usp */
	movel	%a3,%a0@(TASK_THREAD+THREAD_USP)

	movel	%a1@(TASK_THREAD+THREAD_USP),%a3 /* restore user stack */
	movel	%a3,%usp
	movel	%a1@(TASK_THREAD+THREAD_KSP),%sp /* restore new thread stack */
	RESTORE_SWITCH_STACK
	movew	%a1@(TASK_THREAD+THREAD_SR),%sr	/* restore thread status reg */
	rts

