/*
 * File:         arch/blackfin/mach-common/entry.S
 * Based on:
 * Author:       Linus Torvalds
 *
 * Created:      ?
 * Description:  contains the system-call and fault low-level handling routines.
 *               This also contains the timer-interrupt handler, as well as all
 *               interrupts and faults that can result in a task-switch.
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * 25-Dec-2004 - LG Soft India
 * 	1. Fix in return_from_int, to make sure any pending
 *	system call in ILAT for this process to get
 *	executed, otherwise in case context switch happens,
 *	system call of first process (i.e in ILAT) will be
 *	carried forward to the switched process.
 *	2. Removed Constant references for the following
 *		a.  IPEND
 *		b.  EXCAUSE mask
 *		c.  PAGE Mask
 */

/*
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 */


#include <linux/linkage.h>
#include <asm/blackfin.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/thread_info.h>  /* TIF_NEED_RESCHED */
#include <asm/asm-offsets.h>

#include <asm/mach-common/context.S>

#ifdef CONFIG_DEBUG_BFIN_NO_KERN_HWTRACE
	/*
	 * TODO: this should be proper save/restore, but for now
	 * we'll just cheat and use 0x1/0x13
	 */
# define DEBUG_START_HWTRACE \
	P5.l = LO(TBUFCTL); \
	P5.h = HI(TBUFCTL); \
	R7 = 0x13; \
	[P5] = R7;
# define DEBUG_STOP_HWTRACE \
	P5.l = LO(TBUFCTL); \
	P5.h = HI(TBUFCTL); \
	R7 = 0x01; \
	[P5] = R7;
#else
# define DEBUG_START_HWTRACE
# define DEBUG_STOP_HWTRACE
#endif

#ifdef CONFIG_EXCPT_IRQ_SYSC_L1
.section .l1.text
#else
.text
#endif

/* Slightly simplified and streamlined entry point for CPLB misses.
 * This one does not lower the level to IRQ5, and thus can be used to
 * patch up CPLB misses on the kernel stack.
 */
ENTRY(_ex_dcplb)
#if defined(ANOMALY_05000261)
	/*
	 * Work around an anomaly: if we see a new DCPLB fault, return
	 * without doing anything.  Then, if we get the same fault again,
	 * handle it.
	 */
	p5.l = _last_cplb_fault_retx;
	p5.h = _last_cplb_fault_retx;
	r7 = [p5];
	r6 = retx;
	[p5] = r6;
	cc = r6 == r7;
	if !cc jump _return_from_exception;
	/* fall through */
#endif

ENTRY(_ex_icplb)
	(R7:6,P5:4) = [sp++];
	ASTAT = [sp++];
	SAVE_ALL_SYS
	call __cplb_hdr;
	DEBUG_START_HWTRACE
	RESTORE_ALL_SYS
	SP = RETN;
	rtx;

ENTRY(_ex_spinlock)
	/* Transform this into a syscall - twiddle the syscall vector.  */
	p5.l = lo(EVT15);
	p5.h = hi(EVT15);
	r7.l = _spinlock_bh;
	r7.h = _spinlock_bh;
	[p5] = r7;
	csync;
	/* Fall through.  */

ENTRY(_ex_syscall)
	DEBUG_START_HWTRACE
	(R7:6,P5:4) = [sp++];
	ASTAT = [sp++];
	raise 15;		/* invoked by TRAP #0, for sys call */
	sp = retn;
	rtx

ENTRY(_spinlock_bh)
	SAVE_ALL_SYS
	/* To end up here, vector 15 was changed - so we have to change it
	 * back.
	 */
	p0.l = lo(EVT15);
	p0.h = hi(EVT15);
	p1.l = _evt_system_call;
	p1.h = _evt_system_call;
	[p0] = p1;
	csync;
	r0 = [sp + PT_R0];
	sp += -12;
	call _sys_bfin_spinlock;
	sp += 12;
	[SP + PT_R0] = R0;
	RESTORE_ALL_SYS
	rti;

ENTRY(_ex_soft_bp)
	r7 = retx;
	r7 += -2;
	retx = r7;
	jump.s _ex_trap_c;

ENTRY(_ex_single_step)
	r7 = retx;
	r6 = reti;
	cc = r7 == r6;
	if cc jump _return_from_exception
	r7 = syscfg;
	bitclr (r7, 0);
	syscfg = R7;

	p5.l = lo(IPEND);
	p5.h = hi(IPEND);
	r6 = [p5];
	cc = bittst(r6, 5);
	if !cc jump _ex_trap_c;
	p4.l = lo(EVT5);
	p4.h = hi(EVT5);
	r6.h = _exception_to_level5;
	r6.l = _exception_to_level5;
	r7 = [p4];
	cc = r6 == r7;
	if !cc jump _ex_trap_c;

_return_from_exception:
	DEBUG_START_HWTRACE
	(R7:6,P5:4) = [sp++];
	ASTAT = [sp++];
	sp = retn;
	rtx;

ENTRY(_handle_bad_cplb)
	/* To get here, we just tried and failed to change a CPLB
	 * so, handle things in trap_c (C code), by lowering to
	 * IRQ5, just like we normally do. Since this is not a
	 * "normal" return path, we have a do alot of stuff to
	 * the stack to get ready so, we can fall through - we
	 * need to make a CPLB exception look like a normal exception
	 */

	DEBUG_START_HWTRACE
	RESTORE_ALL_SYS
	[--sp] = ASTAT;
	[--sp] = (R7:6, P5:4);

ENTRY(_ex_trap_c)
	/* Call C code (trap_c) to handle the exception, which most
	 * likely involves sending a signal to the current process.
	 * To avoid double faults, lower our priority to IRQ5 first.
	 */
	P5.h = _exception_to_level5;
	P5.l = _exception_to_level5;
	p4.l = lo(EVT5);
	p4.h = hi(EVT5);
	[p4] = p5;
	csync;

	/* Disable all interrupts, but make sure level 5 is enabled so
	 * we can switch to that level.  Save the old mask.  */
	cli r6;
	p4.l = _excpt_saved_imask;
	p4.h = _excpt_saved_imask;
	[p4] = r6;
	r6 = 0x3f;
	sti r6;

	/* Save the excause into a circular buffer, in case the instruction
	 * which caused this excecptions causes others.
	 */
	P5.l = _in_ptr_excause;
	P5.h = _in_ptr_excause;
	R7 = [P5];
	R7 += 4;
	R6 = 0xF;
	R7 = R7 & R6;
	[P5] = R7;
	R6.l = _excause_circ_buf;
	R6.h = _excause_circ_buf;
	R7 = R7 + R6;
	p5 = R7;
	R6 = SEQSTAT;
	[P5] = R6;

	DEBUG_START_HWTRACE
	(R7:6,P5:4) = [sp++];
	ASTAT = [sp++];
	SP = RETN;
	raise 5;
	rtx;

ENTRY(_exception_to_level5)
	SAVE_ALL_SYS

	/* Restore interrupt mask.  We haven't pushed RETI, so this
	 * doesn't enable interrupts until we return from this handler.  */
	p4.l = _excpt_saved_imask;
	p4.h = _excpt_saved_imask;
	r6 = [p4];
	sti r6;

	/* Restore the hardware error vector.  */
	P5.h = _evt_ivhw;
	P5.l = _evt_ivhw;
	p4.l = lo(EVT5);
	p4.h = hi(EVT5);
	[p4] = p5;
	csync;

	p2.l = lo(IPEND);
	p2.h = hi(IPEND);
	csync;
	r0 = [p2];              /* Read current IPEND */
	[sp + PT_IPEND] = r0;   /* Store IPEND */

	/* Pop the excause from the circular buffer and push it on the stack
	 * (in the right place - if you change the location of SEQSTAT, you
	 * must change this offset.
	 */
.L_excep_to_5_again:
	P5.l = _out_ptr_excause;
	P5.h = _out_ptr_excause;
	R7 = [P5];
	R7 += 4;
	R6 = 0xF;
	R7 = R7 & R6;
	[P5] = R7;
	R6.l = _excause_circ_buf;
	R6.h = _excause_circ_buf;
	R7 = R7 + R6;
	P5 = R7;
	R1 = [P5];
	[SP + 8] = r1;

	r0 = sp; 	/* stack frame pt_regs pointer argument ==> r0 */
	SP += -12;
	call _trap_c;
	SP += 12;

	/* See if anything else is in the exception buffer
	 * if there is, process it
	 */
	P5.l = _out_ptr_excause;
	P5.h = _out_ptr_excause;
	P4.l = _in_ptr_excause;
	P4.h = _in_ptr_excause;
	R6 = [P5];
	R7 = [P4];
	CC = R6 == R7;
	if ! CC JUMP .L_excep_to_5_again

	call _ret_from_exception;
	RESTORE_ALL_SYS
	rti;

ENTRY(_trap) /* Exception: 4th entry into system event table(supervisor mode)*/
	/* Since the kernel stack can be anywhere, it's not guaranteed to be
	 * covered by a CPLB.  Switch to an exception stack; use RETN as a
	 * scratch register (for want of a better option).
	 */
	retn = sp;
	sp.l = _exception_stack_top;
	sp.h = _exception_stack_top;
	/* Try to deal with syscalls quickly.  */
	[--sp] = ASTAT;
	[--sp] = (R7:6, P5:4);
	DEBUG_STOP_HWTRACE
	r7 = SEQSTAT;		/* reason code is in bit 5:0 */
	r6.l = lo(SEQSTAT_EXCAUSE);
	r6.h = hi(SEQSTAT_EXCAUSE);
	r7 = r7 & r6;
	p5.h = _extable;
	p5.l = _extable;
	p4 = r7;
	p5 = p5 + (p4 << 2);
	p4 = [p5];
	jump (p4);

.Lbadsys:
	r7 = -ENOSYS; 		/* signextending enough */
	[sp + PT_R0] = r7;	/* return value from system call */
	jump .Lsyscall_really_exit;

ENTRY(_kernel_execve)
	link SIZEOF_PTREGS;
	p0 = sp;
	r3 = SIZEOF_PTREGS / 4;
	r4 = 0(x);
0:
	[p0++] = r4;
	r3 += -1;
	cc = r3 == 0;
	if !cc jump 0b (bp);

	p0 = sp;
	sp += -16;
	[sp + 12] = p0;
	call _do_execve;
	SP += 16;
	cc = r0 == 0;
	if ! cc jump 1f;
	/* Success.  Copy our temporary pt_regs to the top of the kernel
	 * stack and do a normal exception return.
	 */
	r1 = sp;
	r0 = (-KERNEL_STACK_SIZE) (x);
	r1 = r1 & r0;
	p2 = r1;
	p3 = [p2];
	r0 = KERNEL_STACK_SIZE - 4 (z);
	p1 = r0;
	p1 = p1 + p2;

	p0 = fp;
	r4 = [p0--];
	r3 = SIZEOF_PTREGS / 4;
0:
	r4 = [p0--];
	[p1--] = r4;
	r3 += -1;
	cc = r3 == 0;
	if ! cc jump 0b (bp);

	r0 = (KERNEL_STACK_SIZE - SIZEOF_PTREGS) (z);
	p1 = r0;
	p1 = p1 + p2;
	sp = p1;
	r0 = syscfg;
	[SP + PT_SYSCFG] = r0;
	[p3 + (TASK_THREAD + THREAD_KSP)] = sp;

	RESTORE_CONTEXT;
	rti;
1:
	unlink;
	rts;

ENTRY(_system_call)
	/* Store IPEND */
	p2.l = lo(IPEND);
	p2.h = hi(IPEND);
	csync;
	r0 = [p2];
	[sp + PT_IPEND] = r0;

	/* Store RETS for now */
	r0 = rets;
	[sp + PT_RESERVED] = r0;
	/* Set the stack for the current process */
	r7 = sp;
	r6.l = lo(ALIGN_PAGE_MASK);
	r6.h = hi(ALIGN_PAGE_MASK);
	r7 = r7 & r6;  		/* thread_info */
	p2 = r7;
	p2 = [p2];

	[p2+(TASK_THREAD+THREAD_KSP)] = sp;

	/* Check the System Call */
	r7 = __NR_syscall;
	/* System call number is passed in P0 */
	r6 = p0;
	cc = r6 < r7;
	if ! cc jump .Lbadsys;

	/* are we tracing syscalls?*/
	r7 = sp;
	r6.l = lo(ALIGN_PAGE_MASK);
	r6.h = hi(ALIGN_PAGE_MASK);
	r7 = r7 & r6;
	p2 = r7;
	r7 = [p2+TI_FLAGS];
	CC = BITTST(r7,TIF_SYSCALL_TRACE);
	if CC JUMP _sys_trace;

	/* Execute the appropriate system call */

	p4 = p0;
	p5.l = _sys_call_table;
	p5.h = _sys_call_table;
	p5 = p5 + (p4 << 2);
	r0 = [sp + PT_R0];
	r1 = [sp + PT_R1];
	r2 = [sp + PT_R2];
	p5 = [p5];

	[--sp] = r5;
	[--sp] = r4;
	[--sp] = r3;
	SP += -12;
	call (p5);
	SP += 24;
	[sp + PT_R0] = r0;

.Lresume_userspace:
	r7 = sp;
	r4.l = lo(ALIGN_PAGE_MASK);
	r4.h = hi(ALIGN_PAGE_MASK);
	r7 = r7 & r4;		/* thread_info->flags */
	p5 = r7;
.Lresume_userspace_1:
	/* Disable interrupts.  */
	[--sp] = reti;
	reti = [sp++];

	r7 = [p5 + TI_FLAGS];
	r4.l = lo(_TIF_WORK_MASK);
	r4.h = hi(_TIF_WORK_MASK);
	r7 =  r7 & r4;

.Lsyscall_resched:
	cc = BITTST(r7, TIF_NEED_RESCHED);
	if !cc jump .Lsyscall_sigpending;

	/* Reenable interrupts.  */
	[--sp] = reti;
	r0 = [sp++];

	SP += -12;
	call _schedule;
	SP += 12;

	jump .Lresume_userspace_1;

.Lsyscall_sigpending:
	cc = BITTST(r7, TIF_RESTORE_SIGMASK);
	if cc jump .Lsyscall_do_signals;
	cc = BITTST(r7, TIF_SIGPENDING);
	if !cc jump .Lsyscall_really_exit;
.Lsyscall_do_signals:
	/* Reenable interrupts.  */
	[--sp] = reti;
	r0 = [sp++];

	r0 = sp;
	SP += -12;
	call _do_signal;
	SP += 12;

.Lsyscall_really_exit:
	r5 = [sp + PT_RESERVED];
	rets = r5;
	rts;

_sys_trace:
	call _syscall_trace;

	/* Execute the appropriate system call */

	p4 = [SP + PT_P0];
	p5.l = _sys_call_table;
	p5.h = _sys_call_table;
	p5 = p5 + (p4 << 2);
	r0 = [sp + PT_R0];
	r1 = [sp + PT_R1];
	r2 = [sp + PT_R2];
	r3 = [sp + PT_R3];
	r4 = [sp + PT_R4];
	r5 = [sp + PT_R5];
	p5 = [p5];

	[--sp] = r5;
	[--sp] = r4;
	[--sp] = r3;
	SP += -12;
	call (p5);
	SP += 24;
	[sp + PT_R0] = r0;

	call _syscall_trace;
	jump .Lresume_userspace;

ENTRY(_resume)
	/*
	 * Beware - when entering resume, prev (the current task) is
	 * in r0, next (the new task) is in r1.
	 */
	p0 = r0;
	p1 = r1;
	[--sp] = rets;
	[--sp] = fp;
	[--sp] = (r7:4, p5:3);

	/* save usp */
	p2 = usp;
	[p0+(TASK_THREAD+THREAD_USP)] = p2;

	/* save current kernel stack pointer */
	[p0+(TASK_THREAD+THREAD_KSP)] = sp;

	/* save program counter */
	r1.l = _new_old_task;
	r1.h = _new_old_task;
	[p0+(TASK_THREAD+THREAD_PC)] = r1;

	/* restore the kernel stack pointer */
	sp = [p1+(TASK_THREAD+THREAD_KSP)];

	/* restore user stack pointer */
	p0 = [p1+(TASK_THREAD+THREAD_USP)];
	usp = p0;

	/* restore pc */
	p0 = [p1+(TASK_THREAD+THREAD_PC)];
	jump (p0);

	/*
	 * Following code actually lands up in a new (old) task.
	 */

_new_old_task:
	(r7:4, p5:3) = [sp++];
	fp = [sp++];
	rets = [sp++];

	/*
	 * When we come out of resume, r0 carries "old" task, becuase we are
	 * in "new" task.
	 */
	rts;

ENTRY(_ret_from_exception)
	p2.l = lo(IPEND);
	p2.h = hi(IPEND);

	csync;
	r0 = [p2];
	[sp + PT_IPEND] = r0;

1:
	r1 = 0x37(Z);
	r2 = ~r1;
	r2.h = 0;
	r0 = r2 & r0;
	cc = r0 == 0;
	if !cc jump 4f;	/* if not return to user mode, get out */

	/* Make sure any pending system call or deferred exception
	 * return in ILAT for this process to get executed, otherwise
	 * in case context switch happens, system call of
	 * first process (i.e in ILAT) will be carried
	 * forward to the switched process
	 */

	p2.l = lo(ILAT);
	p2.h = hi(ILAT);
	r0 = [p2];
	r1 = (EVT_IVG14 | EVT_IVG15) (z);
	r0 = r0 & r1;
	cc = r0 == 0;
	if !cc jump 5f;

	/* Set the stack for the current process */
	r7 = sp;
	r4.l = lo(ALIGN_PAGE_MASK);
	r4.h = hi(ALIGN_PAGE_MASK);
	r7 = r7 & r4;		/* thread_info->flags */
	p5 = r7;
	r7 = [p5 + TI_FLAGS];
	r4.l = lo(_TIF_WORK_MASK);
	r4.h = hi(_TIF_WORK_MASK);
	r7 =  r7 & r4;
	cc = r7 == 0;
	if cc jump 4f;

	p0.l = lo(EVT15);
	p0.h = hi(EVT15);
	p1.l = _schedule_and_signal;
	p1.h = _schedule_and_signal;
	[p0] = p1;
	csync;
	raise 15;		/* raise evt14 to do signal or reschedule */
4:
	r0 = syscfg;
	bitclr(r0, 0);
	syscfg = r0;
5:
	rts;

ENTRY(_return_from_int)
	/* If someone else already raised IRQ 15, do nothing.  */
	csync;
	p2.l = lo(ILAT);
	p2.h = hi(ILAT);
	r0 = [p2];
	cc = bittst (r0, EVT_IVG15_P);
	if cc jump 2f;

	/* if not return to user mode, get out */
	p2.l = lo(IPEND);
	p2.h = hi(IPEND);
	r0 = [p2];
	r1 = 0x17(Z);
	r2 = ~r1;
	r2.h = 0;
	r0 = r2 & r0;
	r1 = 1;
	r1 = r0 - r1;
	r2 = r0 & r1;
	cc = r2 == 0;
	if !cc jump 2f;

	/* Lower the interrupt level to 15.  */
	p0.l = lo(EVT15);
	p0.h = hi(EVT15);
	p1.l = _schedule_and_signal_from_int;
	p1.h = _schedule_and_signal_from_int;
	[p0] = p1;
	csync;
#if defined(ANOMALY_05000281)
	r0.l = lo(CONFIG_BOOT_LOAD);
	r0.h = hi(CONFIG_BOOT_LOAD);
	reti = r0;
#endif
	r0 = 0x801f (z);
	STI r0;
	raise 15;	/* raise evt15 to do signal or reschedule */
	rti;
2:
	rts;

ENTRY(_lower_to_irq14)
#if defined(ANOMALY_05000281)
	r0.l = lo(CONFIG_BOOT_LOAD);
	r0.h = hi(CONFIG_BOOT_LOAD);
	reti = r0;
#endif
	r0 = 0x401f;
	sti r0;
	raise 14;
	rti;
ENTRY(_evt14_softirq)
#ifdef CONFIG_DEBUG_HWERR
	r0 = 0x3f;
	sti r0;
#else
	cli r0;
#endif
	[--sp] = RETI;
	SP += 4;
	rts;

_schedule_and_signal_from_int:
	/* To end up here, vector 15 was changed - so we have to change it
	 * back.
	 */
	p0.l = lo(EVT15);
	p0.h = hi(EVT15);
	p1.l = _evt_system_call;
	p1.h = _evt_system_call;
	[p0] = p1;
	csync;
	p1 = rets;
	[sp + PT_RESERVED] = p1;

	p0.l = _irq_flags;
	p0.h = _irq_flags;
	r0 = [p0];
	sti r0;

	jump.s .Lresume_userspace;

_schedule_and_signal:
	SAVE_CONTEXT_SYSCALL
	/* To end up here, vector 15 was changed - so we have to change it
	 * back.
	 */
	p0.l = lo(EVT15);
	p0.h = hi(EVT15);
	p1.l = _evt_system_call;
	p1.h = _evt_system_call;
	[p0] = p1;
	csync;
	p0.l = 1f;
	p0.h = 1f;
	[sp + PT_RESERVED] = P0;
	call .Lresume_userspace;
1:
	RESTORE_CONTEXT
	rti;

/* Make sure when we start, that the circular buffer is initialized properly
 * R0 and P0 are call clobbered, so we can use them here.
 */
ENTRY(_init_exception_buff)
	r0 = 0;
	p0.h = _in_ptr_excause;
	p0.l = _in_ptr_excause;
	[p0] = r0;
	p0.h = _out_ptr_excause;
	p0.l = _out_ptr_excause;
	[p0] = r0;
	rts;

/*
 * Put these in the kernel data section - that should always be covered by
 * a CPLB. This is needed to ensure we don't get double fault conditions
 */

#ifdef CONFIG_SYSCALL_TAB_L1
.section .l1.data
#else
.data
#endif
ALIGN
_extable:
	/* entry for each EXCAUSE[5:0]
	 * This table bmust be in sync with the table in ./kernel/traps.c
	 * EXCPT instruction can provide 4 bits of EXCAUSE, allowing 16 to be user defined
	 */
	.long _ex_syscall;      /* 0x00 - User Defined - Linux Syscall */
	.long _ex_soft_bp       /* 0x01 - User Defined - Software breakpoint */
	.long _ex_trap_c        /* 0x02 - User Defined */
	.long _ex_trap_c        /* 0x03 - User Defined  - Atomic test and set service */
	.long _ex_spinlock      /* 0x04 - User Defined */
	.long _ex_trap_c        /* 0x05 - User Defined */
	.long _ex_trap_c        /* 0x06 - User Defined */
	.long _ex_trap_c        /* 0x07 - User Defined */
	.long _ex_trap_c        /* 0x08 - User Defined */
	.long _ex_trap_c        /* 0x09 - User Defined */
	.long _ex_trap_c        /* 0x0A - User Defined */
	.long _ex_trap_c        /* 0x0B - User Defined */
	.long _ex_trap_c        /* 0x0C - User Defined */
	.long _ex_trap_c        /* 0x0D - User Defined */
	.long _ex_trap_c        /* 0x0E - User Defined */
	.long _ex_trap_c        /* 0x0F - User Defined */
	.long _ex_single_step   /* 0x10 - HW Single step */
	.long _ex_trap_c        /* 0x11 - Trace Buffer Full */
	.long _ex_trap_c        /* 0x12 - Reserved */
	.long _ex_trap_c        /* 0x13 - Reserved */
	.long _ex_trap_c        /* 0x14 - Reserved */
	.long _ex_trap_c        /* 0x15 - Reserved */
	.long _ex_trap_c        /* 0x16 - Reserved */
	.long _ex_trap_c        /* 0x17 - Reserved */
	.long _ex_trap_c        /* 0x18 - Reserved */
	.long _ex_trap_c        /* 0x19 - Reserved */
	.long _ex_trap_c        /* 0x1A - Reserved */
	.long _ex_trap_c        /* 0x1B - Reserved */
	.long _ex_trap_c        /* 0x1C - Reserved */
	.long _ex_trap_c        /* 0x1D - Reserved */
	.long _ex_trap_c        /* 0x1E - Reserved */
	.long _ex_trap_c        /* 0x1F - Reserved */
	.long _ex_trap_c        /* 0x20 - Reserved */
	.long _ex_trap_c        /* 0x21 - Undefined Instruction */
	.long _ex_trap_c        /* 0x22 - Illegal Instruction Combination */
	.long _ex_dcplb         /* 0x23 - Data CPLB Protection Violation */
	.long _ex_trap_c        /* 0x24 - Data access misaligned */
	.long _ex_trap_c        /* 0x25 - Unrecoverable Event */
	.long _ex_dcplb         /* 0x26 - Data CPLB Miss */
	.long _ex_trap_c        /* 0x27 - Data CPLB Multiple Hits - Linux Trap Zero */
	.long _ex_trap_c        /* 0x28 - Emulation Watchpoint */
	.long _ex_trap_c        /* 0x29 - Instruction fetch access error (535 only) */
	.long _ex_trap_c        /* 0x2A - Instruction fetch misaligned */
	.long _ex_icplb         /* 0x2B - Instruction CPLB protection Violation */
	.long _ex_icplb         /* 0x2C - Instruction CPLB miss */
	.long _ex_trap_c        /* 0x2D - Instruction CPLB Multiple Hits */
	.long _ex_trap_c        /* 0x2E - Illegal use of Supervisor Resource */
	.long _ex_trap_c        /* 0x2E - Illegal use of Supervisor Resource */
	.long _ex_trap_c        /* 0x2F - Reserved */
	.long _ex_trap_c        /* 0x30 - Reserved */
	.long _ex_trap_c        /* 0x31 - Reserved */
	.long _ex_trap_c        /* 0x32 - Reserved */
	.long _ex_trap_c        /* 0x33 - Reserved */
	.long _ex_trap_c        /* 0x34 - Reserved */
	.long _ex_trap_c        /* 0x35 - Reserved */
	.long _ex_trap_c        /* 0x36 - Reserved */
	.long _ex_trap_c        /* 0x37 - Reserved */
	.long _ex_trap_c        /* 0x38 - Reserved */
	.long _ex_trap_c        /* 0x39 - Reserved */
	.long _ex_trap_c        /* 0x3A - Reserved */
	.long _ex_trap_c        /* 0x3B - Reserved */
	.long _ex_trap_c        /* 0x3C - Reserved */
	.long _ex_trap_c        /* 0x3D - Reserved */
	.long _ex_trap_c        /* 0x3E - Reserved */
	.long _ex_trap_c        /* 0x3F - Reserved */

ALIGN
ENTRY(_sys_call_table)
	.long _sys_ni_syscall	/* 0  -  old "setup()" system call*/
	.long _sys_exit
	.long _sys_fork
	.long _sys_read
	.long _sys_write
	.long _sys_open		/* 5 */
	.long _sys_close
	.long _sys_ni_syscall	/* old waitpid */
	.long _sys_creat
	.long _sys_link
	.long _sys_unlink	/* 10 */
	.long _sys_execve
	.long _sys_chdir
	.long _sys_time
	.long _sys_mknod
	.long _sys_chmod		/* 15 */
	.long _sys_chown	/* chown16 */
	.long _sys_ni_syscall	/* old break syscall holder */
	.long _sys_ni_syscall	/* old stat */
	.long _sys_lseek
	.long _sys_getpid	/* 20 */
	.long _sys_mount
	.long _sys_ni_syscall	/* old umount */
	.long _sys_setuid
	.long _sys_getuid
	.long _sys_stime		/* 25 */
	.long _sys_ptrace
	.long _sys_alarm
	.long _sys_ni_syscall	/* old fstat */
	.long _sys_pause
	.long _sys_ni_syscall	/* old utime */ /* 30 */
	.long _sys_ni_syscall	/* old stty syscall holder */
	.long _sys_ni_syscall	/* old gtty syscall holder */
	.long _sys_access
	.long _sys_nice
	.long _sys_ni_syscall	/* 35 */ /* old ftime syscall holder */
	.long _sys_sync
	.long _sys_kill
	.long _sys_rename
	.long _sys_mkdir
	.long _sys_rmdir		/* 40 */
	.long _sys_dup
	.long _sys_pipe
	.long _sys_times
	.long _sys_ni_syscall	/* old prof syscall holder */
	.long _sys_brk		/* 45 */
	.long _sys_setgid
	.long _sys_getgid
	.long _sys_ni_syscall	/* old sys_signal */
	.long _sys_geteuid	/* geteuid16 */
	.long _sys_getegid	/* getegid16 */	/* 50 */
	.long _sys_acct
	.long _sys_umount	/* recycled never used phys() */
	.long _sys_ni_syscall	/* old lock syscall holder */
	.long _sys_ioctl
	.long _sys_fcntl		/* 55 */
	.long _sys_ni_syscall	/* old mpx syscall holder */
	.long _sys_setpgid
	.long _sys_ni_syscall	/* old ulimit syscall holder */
	.long _sys_ni_syscall	/* old old uname */
	.long _sys_umask		/* 60 */
	.long _sys_chroot
	.long _sys_ustat
	.long _sys_dup2
	.long _sys_getppid
	.long _sys_getpgrp	/* 65 */
	.long _sys_setsid
	.long _sys_ni_syscall	/* old sys_sigaction */
	.long _sys_sgetmask
	.long _sys_ssetmask
	.long _sys_setreuid	/* setreuid16 */	/* 70 */
	.long _sys_setregid	/* setregid16 */
	.long _sys_ni_syscall	/* old sys_sigsuspend */
	.long _sys_ni_syscall	/* old sys_sigpending */
	.long _sys_sethostname
	.long _sys_setrlimit	/* 75 */
	.long _sys_ni_syscall	/* old getrlimit */
	.long _sys_getrusage
	.long _sys_gettimeofday
	.long _sys_settimeofday
	.long _sys_getgroups	/* getgroups16 */	/* 80 */
	.long _sys_setgroups	/* setgroups16 */
	.long _sys_ni_syscall	/* old_select */
	.long _sys_symlink
	.long _sys_ni_syscall	/* old lstat */
	.long _sys_readlink	/* 85 */
	.long _sys_uselib
	.long _sys_ni_syscall	/* sys_swapon */
	.long _sys_reboot
	.long _sys_ni_syscall	/* old_readdir */
	.long _sys_ni_syscall	/* sys_mmap */	/* 90 */
	.long _sys_munmap
	.long _sys_truncate
	.long _sys_ftruncate
	.long _sys_fchmod
	.long _sys_fchown	/* fchown16 */	/* 95 */
	.long _sys_getpriority
	.long _sys_setpriority
	.long _sys_ni_syscall	/* old profil syscall holder */
	.long _sys_statfs
	.long _sys_fstatfs	/* 100 */
	.long _sys_ni_syscall
	.long _sys_ni_syscall	/* old sys_socketcall */
	.long _sys_syslog
	.long _sys_setitimer
	.long _sys_getitimer	/* 105 */
	.long _sys_newstat
	.long _sys_newlstat
	.long _sys_newfstat
	.long _sys_ni_syscall	/* old uname */
	.long _sys_ni_syscall	/* iopl for i386 */ /* 110 */
	.long _sys_vhangup
	.long _sys_ni_syscall	/* obsolete idle() syscall */
	.long _sys_ni_syscall	/* vm86old for i386 */
	.long _sys_wait4
	.long _sys_ni_syscall	/* 115 */ /* sys_swapoff */
	.long _sys_sysinfo
	.long _sys_ni_syscall	/* old sys_ipc */
	.long _sys_fsync
	.long _sys_ni_syscall	/* old sys_sigreturn */
	.long _sys_clone		/* 120 */
	.long _sys_setdomainname
	.long _sys_newuname
	.long _sys_ni_syscall	/* old sys_modify_ldt */
	.long _sys_adjtimex
	.long _sys_ni_syscall	/* 125 */ /* sys_mprotect */
	.long _sys_ni_syscall	/* old sys_sigprocmask */
	.long _sys_ni_syscall	/* old "creat_module" */
	.long _sys_init_module
	.long _sys_delete_module
	.long _sys_ni_syscall	/* 130: old "get_kernel_syms" */
	.long _sys_quotactl
	.long _sys_getpgid
	.long _sys_fchdir
	.long _sys_bdflush
	.long _sys_ni_syscall	/* 135 */ /* sys_sysfs */
	.long _sys_personality
	.long _sys_ni_syscall	/* for afs_syscall */
	.long _sys_setfsuid	/* setfsuid16 */
	.long _sys_setfsgid	/* setfsgid16 */
	.long _sys_llseek	/* 140 */
	.long _sys_getdents
	.long _sys_ni_syscall	/* sys_select */
	.long _sys_flock
	.long _sys_ni_syscall	/* sys_msync */
	.long _sys_readv		/* 145 */
	.long _sys_writev
	.long _sys_getsid
	.long _sys_fdatasync
	.long _sys_sysctl
	.long _sys_ni_syscall	/* 150 */ /* sys_mlock */
	.long _sys_ni_syscall	/* sys_munlock */
	.long _sys_ni_syscall	/* sys_mlockall */
	.long _sys_ni_syscall	/* sys_munlockall */
	.long _sys_sched_setparam
	.long _sys_sched_getparam /* 155 */
	.long _sys_sched_setscheduler
	.long _sys_sched_getscheduler
	.long _sys_sched_yield
	.long _sys_sched_get_priority_max
	.long _sys_sched_get_priority_min  /* 160 */
	.long _sys_sched_rr_get_interval
	.long _sys_nanosleep
	.long _sys_ni_syscall	/* sys_mremap */
	.long _sys_setresuid	/* setresuid16 */
	.long _sys_getresuid	/* getresuid16 */	/* 165 */
	.long _sys_ni_syscall	/* for vm86 */
	.long _sys_ni_syscall	/* old "query_module" */
	.long _sys_ni_syscall	/* sys_poll */
	.long _sys_ni_syscall	/* sys_nfsservctl */
	.long _sys_setresgid	/* setresgid16 */	/* 170 */
	.long _sys_getresgid	/* getresgid16 */
	.long _sys_prctl
	.long _sys_rt_sigreturn
	.long _sys_rt_sigaction
	.long _sys_rt_sigprocmask /* 175 */
	.long _sys_rt_sigpending
	.long _sys_rt_sigtimedwait
	.long _sys_rt_sigqueueinfo
	.long _sys_rt_sigsuspend
	.long _sys_pread64	/* 180 */
	.long _sys_pwrite64
	.long _sys_lchown	/* lchown16 */
	.long _sys_getcwd
	.long _sys_capget
	.long _sys_capset	/* 185 */
	.long _sys_sigaltstack
	.long _sys_sendfile
	.long _sys_ni_syscall	/* streams1 */
	.long _sys_ni_syscall	/* streams2 */
	.long _sys_vfork		/* 190 */
	.long _sys_getrlimit
	.long _sys_mmap2
	.long _sys_truncate64
	.long _sys_ftruncate64
	.long _sys_stat64	/* 195 */
	.long _sys_lstat64
	.long _sys_fstat64
	.long _sys_chown
	.long _sys_getuid
	.long _sys_getgid	/* 200 */
	.long _sys_geteuid
	.long _sys_getegid
	.long _sys_setreuid
	.long _sys_setregid
	.long _sys_getgroups	/* 205 */
	.long _sys_setgroups
	.long _sys_fchown
	.long _sys_setresuid
	.long _sys_getresuid
	.long _sys_setresgid	/* 210 */
	.long _sys_getresgid
	.long _sys_lchown
	.long _sys_setuid
	.long _sys_setgid
	.long _sys_setfsuid	/* 215 */
	.long _sys_setfsgid
	.long _sys_pivot_root
	.long _sys_ni_syscall	/* sys_mincore */
	.long _sys_ni_syscall	/* sys_madvise */
	.long _sys_getdents64	/* 220 */
	.long _sys_fcntl64
	.long _sys_ni_syscall	/* reserved for TUX */
	.long _sys_ni_syscall
	.long _sys_gettid
	.long _sys_ni_syscall	/* 225 */ /* sys_readahead */
	.long _sys_setxattr
	.long _sys_lsetxattr
	.long _sys_fsetxattr
	.long _sys_getxattr
	.long _sys_lgetxattr	/* 230 */
	.long _sys_fgetxattr
	.long _sys_listxattr
	.long _sys_llistxattr
	.long _sys_flistxattr
	.long _sys_removexattr	/* 235 */
	.long _sys_lremovexattr
	.long _sys_fremovexattr
	.long _sys_tkill
	.long _sys_sendfile64
	.long _sys_futex		/* 240 */
	.long _sys_sched_setaffinity
	.long _sys_sched_getaffinity
	.long _sys_ni_syscall	/* sys_set_thread_area */
	.long _sys_ni_syscall	/* sys_get_thread_area */
	.long _sys_io_setup	/* 245 */
	.long _sys_io_destroy
	.long _sys_io_getevents
	.long _sys_io_submit
	.long _sys_io_cancel
	.long _sys_ni_syscall	/* 250 */ /* sys_alloc_hugepages */
	.long _sys_ni_syscall	/* sys_freec_hugepages */
	.long _sys_exit_group
	.long _sys_lookup_dcookie
	.long _sys_bfin_spinlock
	.long _sys_epoll_create	/* 255 */
	.long _sys_epoll_ctl
	.long _sys_epoll_wait
	.long _sys_ni_syscall /* remap_file_pages */
	.long _sys_set_tid_address
	.long _sys_timer_create	/* 260 */
	.long _sys_timer_settime
	.long _sys_timer_gettime
	.long _sys_timer_getoverrun
	.long _sys_timer_delete
	.long _sys_clock_settime /* 265 */
	.long _sys_clock_gettime
	.long _sys_clock_getres
	.long _sys_clock_nanosleep
	.long _sys_statfs64
	.long _sys_fstatfs64	/* 270 */
	.long _sys_tgkill
	.long _sys_utimes
	.long _sys_fadvise64_64
	.long _sys_ni_syscall /* vserver */
	.long _sys_ni_syscall /* 275, mbind */
	.long _sys_ni_syscall /* get_mempolicy */
	.long _sys_ni_syscall /* set_mempolicy */
	.long _sys_mq_open
	.long _sys_mq_unlink
	.long _sys_mq_timedsend	/* 280 */
	.long _sys_mq_timedreceive
	.long _sys_mq_notify
	.long _sys_mq_getsetattr
	.long _sys_ni_syscall /* kexec_load */
	.long _sys_waitid	/* 285 */
	.long _sys_add_key
	.long _sys_request_key
	.long _sys_keyctl
	.long _sys_ioprio_set
	.long _sys_ioprio_get	/* 290 */
	.long _sys_inotify_init
	.long _sys_inotify_add_watch
	.long _sys_inotify_rm_watch
	.long _sys_ni_syscall /* migrate_pages */
	.long _sys_openat	/* 295 */
	.long _sys_mkdirat
	.long _sys_mknodat
	.long _sys_fchownat
	.long _sys_futimesat
	.long _sys_fstatat64	/* 300 */
	.long _sys_unlinkat
	.long _sys_renameat
	.long _sys_linkat
	.long _sys_symlinkat
	.long _sys_readlinkat	/* 305 */
	.long _sys_fchmodat
	.long _sys_faccessat
	.long _sys_pselect6
	.long _sys_ppoll
	.long _sys_unshare	/* 310 */
	.long _sys_sram_alloc
	.long _sys_sram_free
	.long _sys_dma_memcpy
	.long _sys_accept
	.long _sys_bind		/* 315 */
	.long _sys_connect
	.long _sys_getpeername
	.long _sys_getsockname
	.long _sys_getsockopt
	.long _sys_listen	/* 320 */
	.long _sys_recv
	.long _sys_recvfrom
	.long _sys_recvmsg
	.long _sys_send
	.long _sys_sendmsg	/* 325 */
	.long _sys_sendto
	.long _sys_setsockopt
	.long _sys_shutdown
	.long _sys_socket
	.long _sys_socketpair	/* 330 */
	.long _sys_semctl
	.long _sys_semget
	.long _sys_semop
	.long _sys_msgctl
	.long _sys_msgget	/* 335 */
	.long _sys_msgrcv
	.long _sys_msgsnd
	.long _sys_shmat
	.long _sys_shmctl
	.long _sys_shmdt	/* 340 */
	.long _sys_shmget
	.rept NR_syscalls-(.-_sys_call_table)/4
	.long _sys_ni_syscall
	.endr
_excpt_saved_imask:
	.long 0;

_exception_stack:
	.rept 1024
	.long 0;
	.endr
_exception_stack_top:

#if defined(ANOMALY_05000261)
/* Used by the assembly entry point to work around an anomaly.  */
_last_cplb_fault_retx:
	.long 0;
#endif
/*
 * Single instructions can have multiple faults, which need to be
 * handled by traps.c, in irq5. We store the exception cause to ensure
 * we don't miss a double fault condition
 */
ENTRY(_in_ptr_excause)
	.long 0;
ENTRY(_out_ptr_excause)
	.long 0;
ALIGN
ENTRY(_excause_circ_buf)
	.rept 4
	.long 0
	.endr
