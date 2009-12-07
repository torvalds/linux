/*
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *  Adapted for Power Macintosh by Paul Mackerras.
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *  MPC8xx modifications Copyright (C) 1997 Dan Malek (dmalek@jlc.net).
 *
 *  This file contains the system call entry code, context switch
 *  code, and exception/interrupt return code for PowerPC.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <asm/unistd.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/cputable.h>
#include <asm/firmware.h>
#include <asm/bug.h>
#include <asm/ptrace.h>
#include <asm/irqflags.h>
#include <asm/ftrace.h>

/*
 * System calls.
 */
	.section	".toc","aw"
.SYS_CALL_TABLE:
	.tc .sys_call_table[TC],.sys_call_table

/* This value is used to mark exception frames on the stack. */
exception_marker:
	.tc	ID_EXC_MARKER[TC],STACK_FRAME_REGS_MARKER

	.section	".text"
	.align 7

#undef SHOW_SYSCALLS

	.globl system_call_common
system_call_common:
	andi.	r10,r12,MSR_PR
	mr	r10,r1
	addi	r1,r1,-INT_FRAME_SIZE
	beq-	1f
	ld	r1,PACAKSAVE(r13)
1:	std	r10,0(r1)
	std	r11,_NIP(r1)
	std	r12,_MSR(r1)
	std	r0,GPR0(r1)
	std	r10,GPR1(r1)
	ACCOUNT_CPU_USER_ENTRY(r10, r11)
	/*
	 * This "crclr so" clears CR0.SO, which is the error indication on
	 * return from this system call.  There must be no cmp instruction
	 * between it and the "mfcr r9" below, otherwise if XER.SO is set,
	 * CR0.SO will get set, causing all system calls to appear to fail.
	 */
	crclr	so
	std	r2,GPR2(r1)
	std	r3,GPR3(r1)
	std	r4,GPR4(r1)
	std	r5,GPR5(r1)
	std	r6,GPR6(r1)
	std	r7,GPR7(r1)
	std	r8,GPR8(r1)
	li	r11,0
	std	r11,GPR9(r1)
	std	r11,GPR10(r1)
	std	r11,GPR11(r1)
	std	r11,GPR12(r1)
	std	r9,GPR13(r1)
	mfcr	r9
	mflr	r10
	li	r11,0xc01
	std	r9,_CCR(r1)
	std	r10,_LINK(r1)
	std	r11,_TRAP(r1)
	mfxer	r9
	mfctr	r10
	std	r9,_XER(r1)
	std	r10,_CTR(r1)
	std	r3,ORIG_GPR3(r1)
	ld	r2,PACATOC(r13)
	addi	r9,r1,STACK_FRAME_OVERHEAD
	ld	r11,exception_marker@toc(r2)
	std	r11,-16(r9)		/* "regshere" marker */
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	.trace_hardirqs_on
	REST_GPR(0,r1)
	REST_4GPRS(3,r1)
	REST_2GPRS(7,r1)
	addi	r9,r1,STACK_FRAME_OVERHEAD
	ld	r12,_MSR(r1)
#endif /* CONFIG_TRACE_IRQFLAGS */
	li	r10,1
	stb	r10,PACASOFTIRQEN(r13)
	stb	r10,PACAHARDIRQEN(r13)
	std	r10,SOFTE(r1)
#ifdef CONFIG_PPC_ISERIES
BEGIN_FW_FTR_SECTION
	/* Hack for handling interrupts when soft-enabling on iSeries */
	cmpdi	cr1,r0,0x5555		/* syscall 0x5555 */
	andi.	r10,r12,MSR_PR		/* from kernel */
	crand	4*cr0+eq,4*cr1+eq,4*cr0+eq
	bne	2f
	b	hardware_interrupt_entry
2:
END_FW_FTR_SECTION_IFSET(FW_FEATURE_ISERIES)
#endif /* CONFIG_PPC_ISERIES */

	/* Hard enable interrupts */
#ifdef CONFIG_PPC_BOOK3E
	wrteei	1
#else
	mfmsr	r11
	ori	r11,r11,MSR_EE
	mtmsrd	r11,1
#endif /* CONFIG_PPC_BOOK3E */

#ifdef SHOW_SYSCALLS
	bl	.do_show_syscall
	REST_GPR(0,r1)
	REST_4GPRS(3,r1)
	REST_2GPRS(7,r1)
	addi	r9,r1,STACK_FRAME_OVERHEAD
#endif
	clrrdi	r11,r1,THREAD_SHIFT
	ld	r10,TI_FLAGS(r11)
	andi.	r11,r10,_TIF_SYSCALL_T_OR_A
	bne-	syscall_dotrace
syscall_dotrace_cont:
	cmpldi	0,r0,NR_syscalls
	bge-	syscall_enosys

system_call:			/* label this so stack traces look sane */
/*
 * Need to vector to 32 Bit or default sys_call_table here,
 * based on caller's run-mode / personality.
 */
	ld	r11,.SYS_CALL_TABLE@toc(2)
	andi.	r10,r10,_TIF_32BIT
	beq	15f
	addi	r11,r11,8	/* use 32-bit syscall entries */
	clrldi	r3,r3,32
	clrldi	r4,r4,32
	clrldi	r5,r5,32
	clrldi	r6,r6,32
	clrldi	r7,r7,32
	clrldi	r8,r8,32
15:
	slwi	r0,r0,4
	ldx	r10,r11,r0	/* Fetch system call handler [ptr] */
	mtctr   r10
	bctrl			/* Call handler */

syscall_exit:
	std	r3,RESULT(r1)
#ifdef SHOW_SYSCALLS
	bl	.do_show_syscall_exit
	ld	r3,RESULT(r1)
#endif
	clrrdi	r12,r1,THREAD_SHIFT

	ld	r8,_MSR(r1)
#ifdef CONFIG_PPC_BOOK3S
	/* No MSR:RI on BookE */
	andi.	r10,r8,MSR_RI
	beq-	unrecov_restore
#endif

	/* Disable interrupts so current_thread_info()->flags can't change,
	 * and so that we don't get interrupted after loading SRR0/1.
	 */
#ifdef CONFIG_PPC_BOOK3E
	wrteei	0
#else
	mfmsr	r10
	rldicl	r10,r10,48,1
	rotldi	r10,r10,16
	mtmsrd	r10,1
#endif /* CONFIG_PPC_BOOK3E */

	ld	r9,TI_FLAGS(r12)
	li	r11,-_LAST_ERRNO
	andi.	r0,r9,(_TIF_SYSCALL_T_OR_A|_TIF_SINGLESTEP|_TIF_USER_WORK_MASK|_TIF_PERSYSCALL_MASK)
	bne-	syscall_exit_work
	cmpld	r3,r11
	ld	r5,_CCR(r1)
	bge-	syscall_error
syscall_error_cont:
	ld	r7,_NIP(r1)
	stdcx.	r0,0,r1			/* to clear the reservation */
	andi.	r6,r8,MSR_PR
	ld	r4,_LINK(r1)
	/*
	 * Clear RI before restoring r13.  If we are returning to
	 * userspace and we take an exception after restoring r13,
	 * we end up corrupting the userspace r13 value.
	 */
#ifdef CONFIG_PPC_BOOK3S
	/* No MSR:RI on BookE */
	li	r12,MSR_RI
	andc	r11,r10,r12
	mtmsrd	r11,1			/* clear MSR.RI */
#endif /* CONFIG_PPC_BOOK3S */

	beq-	1f
	ACCOUNT_CPU_USER_EXIT(r11, r12)
	ld	r13,GPR13(r1)	/* only restore r13 if returning to usermode */
1:	ld	r2,GPR2(r1)
	ld	r1,GPR1(r1)
	mtlr	r4
	mtcr	r5
	mtspr	SPRN_SRR0,r7
	mtspr	SPRN_SRR1,r8
	RFI
	b	.	/* prevent speculative execution */

syscall_error:	
	oris	r5,r5,0x1000	/* Set SO bit in CR */
	neg	r3,r3
	std	r5,_CCR(r1)
	b	syscall_error_cont
	
/* Traced system call support */
syscall_dotrace:
	bl	.save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.do_syscall_trace_enter
	/*
	 * Restore argument registers possibly just changed.
	 * We use the return value of do_syscall_trace_enter
	 * for the call number to look up in the table (r0).
	 */
	mr	r0,r3
	ld	r3,GPR3(r1)
	ld	r4,GPR4(r1)
	ld	r5,GPR5(r1)
	ld	r6,GPR6(r1)
	ld	r7,GPR7(r1)
	ld	r8,GPR8(r1)
	addi	r9,r1,STACK_FRAME_OVERHEAD
	clrrdi	r10,r1,THREAD_SHIFT
	ld	r10,TI_FLAGS(r10)
	b	syscall_dotrace_cont

syscall_enosys:
	li	r3,-ENOSYS
	b	syscall_exit
	
syscall_exit_work:
	/* If TIF_RESTOREALL is set, don't scribble on either r3 or ccr.
	 If TIF_NOERROR is set, just save r3 as it is. */

	andi.	r0,r9,_TIF_RESTOREALL
	beq+	0f
	REST_NVGPRS(r1)
	b	2f
0:	cmpld	r3,r11		/* r10 is -LAST_ERRNO */
	blt+	1f
	andi.	r0,r9,_TIF_NOERROR
	bne-	1f
	ld	r5,_CCR(r1)
	neg	r3,r3
	oris	r5,r5,0x1000	/* Set SO bit in CR */
	std	r5,_CCR(r1)
1:	std	r3,GPR3(r1)
2:	andi.	r0,r9,(_TIF_PERSYSCALL_MASK)
	beq	4f

	/* Clear per-syscall TIF flags if any are set.  */

	li	r11,_TIF_PERSYSCALL_MASK
	addi	r12,r12,TI_FLAGS
3:	ldarx	r10,0,r12
	andc	r10,r10,r11
	stdcx.	r10,0,r12
	bne-	3b
	subi	r12,r12,TI_FLAGS

4:	/* Anything else left to do? */
	andi.	r0,r9,(_TIF_SYSCALL_T_OR_A|_TIF_SINGLESTEP)
	beq	.ret_from_except_lite

	/* Re-enable interrupts */
#ifdef CONFIG_PPC_BOOK3E
	wrteei	1
#else
	mfmsr	r10
	ori	r10,r10,MSR_EE
	mtmsrd	r10,1
#endif /* CONFIG_PPC_BOOK3E */

	bl	.save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.do_syscall_trace_leave
	b	.ret_from_except

/* Save non-volatile GPRs, if not already saved. */
_GLOBAL(save_nvgprs)
	ld	r11,_TRAP(r1)
	andi.	r0,r11,1
	beqlr-
	SAVE_NVGPRS(r1)
	clrrdi	r0,r11,1
	std	r0,_TRAP(r1)
	blr

	
/*
 * The sigsuspend and rt_sigsuspend system calls can call do_signal
 * and thus put the process into the stopped state where we might
 * want to examine its user state with ptrace.  Therefore we need
 * to save all the nonvolatile registers (r14 - r31) before calling
 * the C code.  Similarly, fork, vfork and clone need the full
 * register state on the stack so that it can be copied to the child.
 */

_GLOBAL(ppc_fork)
	bl	.save_nvgprs
	bl	.sys_fork
	b	syscall_exit

_GLOBAL(ppc_vfork)
	bl	.save_nvgprs
	bl	.sys_vfork
	b	syscall_exit

_GLOBAL(ppc_clone)
	bl	.save_nvgprs
	bl	.sys_clone
	b	syscall_exit

_GLOBAL(ppc32_swapcontext)
	bl	.save_nvgprs
	bl	.compat_sys_swapcontext
	b	syscall_exit

_GLOBAL(ppc64_swapcontext)
	bl	.save_nvgprs
	bl	.sys_swapcontext
	b	syscall_exit

_GLOBAL(ret_from_fork)
	bl	.schedule_tail
	REST_NVGPRS(r1)
	li	r3,0
	b	syscall_exit

/*
 * This routine switches between two different tasks.  The process
 * state of one is saved on its kernel stack.  Then the state
 * of the other is restored from its kernel stack.  The memory
 * management hardware is updated to the second process's state.
 * Finally, we can return to the second process, via ret_from_except.
 * On entry, r3 points to the THREAD for the current task, r4
 * points to the THREAD for the new task.
 *
 * Note: there are two ways to get to the "going out" portion
 * of this code; either by coming in via the entry (_switch)
 * or via "fork" which must set up an environment equivalent
 * to the "_switch" path.  If you change this you'll have to change
 * the fork code also.
 *
 * The code which creates the new task context is in 'copy_thread'
 * in arch/powerpc/kernel/process.c 
 */
	.align	7
_GLOBAL(_switch)
	mflr	r0
	std	r0,16(r1)
	stdu	r1,-SWITCH_FRAME_SIZE(r1)
	/* r3-r13 are caller saved -- Cort */
	SAVE_8GPRS(14, r1)
	SAVE_10GPRS(22, r1)
	mflr	r20		/* Return to switch caller */
	mfmsr	r22
	li	r0, MSR_FP
#ifdef CONFIG_VSX
BEGIN_FTR_SECTION
	oris	r0,r0,MSR_VSX@h	/* Disable VSX */
END_FTR_SECTION_IFSET(CPU_FTR_VSX)
#endif /* CONFIG_VSX */
#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	oris	r0,r0,MSR_VEC@h	/* Disable altivec */
	mfspr	r24,SPRN_VRSAVE	/* save vrsave register value */
	std	r24,THREAD_VRSAVE(r3)
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif /* CONFIG_ALTIVEC */
	and.	r0,r0,r22
	beq+	1f
	andc	r22,r22,r0
	MTMSRD(r22)
	isync
1:	std	r20,_NIP(r1)
	mfcr	r23
	std	r23,_CCR(r1)
	std	r1,KSP(r3)	/* Set old stack pointer */

#ifdef CONFIG_SMP
	/* We need a sync somewhere here to make sure that if the
	 * previous task gets rescheduled on another CPU, it sees all
	 * stores it has performed on this one.
	 */
	sync
#endif /* CONFIG_SMP */

	addi	r6,r4,-THREAD	/* Convert THREAD to 'current' */
	std	r6,PACACURRENT(r13)	/* Set new 'current' */

	ld	r8,KSP(r4)	/* new stack pointer */
#ifdef CONFIG_PPC_BOOK3S
BEGIN_FTR_SECTION
  BEGIN_FTR_SECTION_NESTED(95)
	clrrdi	r6,r8,28	/* get its ESID */
	clrrdi	r9,r1,28	/* get current sp ESID */
  FTR_SECTION_ELSE_NESTED(95)
	clrrdi	r6,r8,40	/* get its 1T ESID */
	clrrdi	r9,r1,40	/* get current sp 1T ESID */
  ALT_FTR_SECTION_END_NESTED_IFCLR(CPU_FTR_1T_SEGMENT, 95)
FTR_SECTION_ELSE
	b	2f
ALT_FTR_SECTION_END_IFSET(CPU_FTR_SLB)
	clrldi.	r0,r6,2		/* is new ESID c00000000? */
	cmpd	cr1,r6,r9	/* or is new ESID the same as current ESID? */
	cror	eq,4*cr1+eq,eq
	beq	2f		/* if yes, don't slbie it */

	/* Bolt in the new stack SLB entry */
	ld	r7,KSP_VSID(r4)	/* Get new stack's VSID */
	oris	r0,r6,(SLB_ESID_V)@h
	ori	r0,r0,(SLB_NUM_BOLTED-1)@l
BEGIN_FTR_SECTION
	li	r9,MMU_SEGSIZE_1T	/* insert B field */
	oris	r6,r6,(MMU_SEGSIZE_1T << SLBIE_SSIZE_SHIFT)@h
	rldimi	r7,r9,SLB_VSID_SSIZE_SHIFT,0
END_FTR_SECTION_IFSET(CPU_FTR_1T_SEGMENT)

	/* Update the last bolted SLB.  No write barriers are needed
	 * here, provided we only update the current CPU's SLB shadow
	 * buffer.
	 */
	ld	r9,PACA_SLBSHADOWPTR(r13)
	li	r12,0
	std	r12,SLBSHADOW_STACKESID(r9) /* Clear ESID */
	std	r7,SLBSHADOW_STACKVSID(r9)  /* Save VSID */
	std	r0,SLBSHADOW_STACKESID(r9)  /* Save ESID */

	/* No need to check for CPU_FTR_NO_SLBIE_B here, since when
	 * we have 1TB segments, the only CPUs known to have the errata
	 * only support less than 1TB of system memory and we'll never
	 * actually hit this code path.
	 */

	slbie	r6
	slbie	r6		/* Workaround POWER5 < DD2.1 issue */
	slbmte	r7,r0
	isync
2:
#endif /* !CONFIG_PPC_BOOK3S */

	clrrdi	r7,r8,THREAD_SHIFT	/* base of new stack */
	/* Note: this uses SWITCH_FRAME_SIZE rather than INT_FRAME_SIZE
	   because we don't need to leave the 288-byte ABI gap at the
	   top of the kernel stack. */
	addi	r7,r7,THREAD_SIZE-SWITCH_FRAME_SIZE

	mr	r1,r8		/* start using new stack pointer */
	std	r7,PACAKSAVE(r13)

	ld	r6,_CCR(r1)
	mtcrf	0xFF,r6

#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	ld	r0,THREAD_VRSAVE(r4)
	mtspr	SPRN_VRSAVE,r0		/* if G4, restore VRSAVE reg */
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif /* CONFIG_ALTIVEC */

	/* r3-r13 are destroyed -- Cort */
	REST_8GPRS(14, r1)
	REST_10GPRS(22, r1)

	/* convert old thread to its task_struct for return value */
	addi	r3,r3,-THREAD
	ld	r7,_NIP(r1)	/* Return to _switch caller in new task */
	mtlr	r7
	addi	r1,r1,SWITCH_FRAME_SIZE
	blr

	.align	7
_GLOBAL(ret_from_except)
	ld	r11,_TRAP(r1)
	andi.	r0,r11,1
	bne	.ret_from_except_lite
	REST_NVGPRS(r1)

_GLOBAL(ret_from_except_lite)
	/*
	 * Disable interrupts so that current_thread_info()->flags
	 * can't change between when we test it and when we return
	 * from the interrupt.
	 */
#ifdef CONFIG_PPC_BOOK3E
	wrteei	0
#else
	mfmsr	r10		/* Get current interrupt state */
	rldicl	r9,r10,48,1	/* clear MSR_EE */
	rotldi	r9,r9,16
	mtmsrd	r9,1		/* Update machine state */
#endif /* CONFIG_PPC_BOOK3E */

#ifdef CONFIG_PREEMPT
	clrrdi	r9,r1,THREAD_SHIFT	/* current_thread_info() */
	li	r0,_TIF_NEED_RESCHED	/* bits to check */
	ld	r3,_MSR(r1)
	ld	r4,TI_FLAGS(r9)
	/* Move MSR_PR bit in r3 to _TIF_SIGPENDING position in r0 */
	rlwimi	r0,r3,32+TIF_SIGPENDING-MSR_PR_LG,_TIF_SIGPENDING
	and.	r0,r4,r0	/* check NEED_RESCHED and maybe SIGPENDING */
	bne	do_work

#else /* !CONFIG_PREEMPT */
	ld	r3,_MSR(r1)	/* Returning to user mode? */
	andi.	r3,r3,MSR_PR
	beq	restore		/* if not, just restore regs and return */

	/* Check current_thread_info()->flags */
	clrrdi	r9,r1,THREAD_SHIFT
	ld	r4,TI_FLAGS(r9)
	andi.	r0,r4,_TIF_USER_WORK_MASK
	bne	do_work
#endif

restore:
BEGIN_FW_FTR_SECTION
	ld	r5,SOFTE(r1)
FW_FTR_SECTION_ELSE
	b	.Liseries_check_pending_irqs
ALT_FW_FTR_SECTION_END_IFCLR(FW_FEATURE_ISERIES)
2:
	TRACE_AND_RESTORE_IRQ(r5);

#ifdef CONFIG_PERF_EVENTS
	/* check paca->perf_event_pending if we're enabling ints */
	lbz	r3,PACAPERFPEND(r13)
	and.	r3,r3,r5
	beq	27f
	bl	.perf_event_do_pending
27:
#endif /* CONFIG_PERF_EVENTS */

	/* extract EE bit and use it to restore paca->hard_enabled */
	ld	r3,_MSR(r1)
	rldicl	r4,r3,49,63		/* r0 = (r3 >> 15) & 1 */
	stb	r4,PACAHARDIRQEN(r13)

#ifdef CONFIG_PPC_BOOK3E
	b	.exception_return_book3e
#else
	ld	r4,_CTR(r1)
	ld	r0,_LINK(r1)
	mtctr	r4
	mtlr	r0
	ld	r4,_XER(r1)
	mtspr	SPRN_XER,r4

	REST_8GPRS(5, r1)

	andi.	r0,r3,MSR_RI
	beq-	unrecov_restore

	stdcx.	r0,0,r1		/* to clear the reservation */

	/*
	 * Clear RI before restoring r13.  If we are returning to
	 * userspace and we take an exception after restoring r13,
	 * we end up corrupting the userspace r13 value.
	 */
	mfmsr	r4
	andc	r4,r4,r0	/* r0 contains MSR_RI here */
	mtmsrd	r4,1

	/*
	 * r13 is our per cpu area, only restore it if we are returning to
	 * userspace
	 */
	andi.	r0,r3,MSR_PR
	beq	1f
	ACCOUNT_CPU_USER_EXIT(r2, r4)
	REST_GPR(13, r1)
1:
	mtspr	SPRN_SRR1,r3

	ld	r2,_CCR(r1)
	mtcrf	0xFF,r2
	ld	r2,_NIP(r1)
	mtspr	SPRN_SRR0,r2

	ld	r0,GPR0(r1)
	ld	r2,GPR2(r1)
	ld	r3,GPR3(r1)
	ld	r4,GPR4(r1)
	ld	r1,GPR1(r1)

	rfid
	b	.	/* prevent speculative execution */

#endif /* CONFIG_PPC_BOOK3E */

.Liseries_check_pending_irqs:
#ifdef CONFIG_PPC_ISERIES
	ld	r5,SOFTE(r1)
	cmpdi	0,r5,0
	beq	2b
	/* Check for pending interrupts (iSeries) */
	ld	r3,PACALPPACAPTR(r13)
	ld	r3,LPPACAANYINT(r3)
	cmpdi	r3,0
	beq+	2b			/* skip do_IRQ if no interrupts */

	li	r3,0
	stb	r3,PACASOFTIRQEN(r13)	/* ensure we are soft-disabled */
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	.trace_hardirqs_off
	mfmsr	r10
#endif
	ori	r10,r10,MSR_EE
	mtmsrd	r10			/* hard-enable again */
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.do_IRQ
	b	.ret_from_except_lite		/* loop back and handle more */
#endif

do_work:
#ifdef CONFIG_PREEMPT
	andi.	r0,r3,MSR_PR	/* Returning to user mode? */
	bne	user_work
	/* Check that preempt_count() == 0 and interrupts are enabled */
	lwz	r8,TI_PREEMPT(r9)
	cmpwi	cr1,r8,0
	ld	r0,SOFTE(r1)
	cmpdi	r0,0
	crandc	eq,cr1*4+eq,eq
	bne	restore

	/* Here we are preempting the current task.
	 *
	 * Ensure interrupts are soft-disabled. We also properly mark
	 * the PACA to reflect the fact that they are hard-disabled
	 * and trace the change
	 */
	li	r0,0
	stb	r0,PACASOFTIRQEN(r13)
	stb	r0,PACAHARDIRQEN(r13)
	TRACE_DISABLE_INTS

	/* Call the scheduler with soft IRQs off */
1:	bl	.preempt_schedule_irq

	/* Hard-disable interrupts again (and update PACA) */
#ifdef CONFIG_PPC_BOOK3E
	wrteei	0
#else
	mfmsr	r10
	rldicl	r10,r10,48,1
	rotldi	r10,r10,16
	mtmsrd	r10,1
#endif /* CONFIG_PPC_BOOK3E */
	li	r0,0
	stb	r0,PACAHARDIRQEN(r13)

	/* Re-test flags and eventually loop */
	clrrdi	r9,r1,THREAD_SHIFT
	ld	r4,TI_FLAGS(r9)
	andi.	r0,r4,_TIF_NEED_RESCHED
	bne	1b
	b	restore

user_work:
#endif /* CONFIG_PREEMPT */

	/* Enable interrupts */
#ifdef CONFIG_PPC_BOOK3E
	wrteei	1
#else
	ori	r10,r10,MSR_EE
	mtmsrd	r10,1
#endif /* CONFIG_PPC_BOOK3E */

	andi.	r0,r4,_TIF_NEED_RESCHED
	beq	1f
	bl	.schedule
	b	.ret_from_except_lite

1:	bl	.save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.do_signal
	b	.ret_from_except

unrecov_restore:
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.unrecoverable_exception
	b	unrecov_restore

#ifdef CONFIG_PPC_RTAS
/*
 * On CHRP, the Run-Time Abstraction Services (RTAS) have to be
 * called with the MMU off.
 *
 * In addition, we need to be in 32b mode, at least for now.
 * 
 * Note: r3 is an input parameter to rtas, so don't trash it...
 */
_GLOBAL(enter_rtas)
	mflr	r0
	std	r0,16(r1)
        stdu	r1,-RTAS_FRAME_SIZE(r1)	/* Save SP and create stack space. */

	/* Because RTAS is running in 32b mode, it clobbers the high order half
	 * of all registers that it saves.  We therefore save those registers
	 * RTAS might touch to the stack.  (r0, r3-r13 are caller saved)
   	 */
	SAVE_GPR(2, r1)			/* Save the TOC */
	SAVE_GPR(13, r1)		/* Save paca */
	SAVE_8GPRS(14, r1)		/* Save the non-volatiles */
	SAVE_10GPRS(22, r1)		/* ditto */

	mfcr	r4
	std	r4,_CCR(r1)
	mfctr	r5
	std	r5,_CTR(r1)
	mfspr	r6,SPRN_XER
	std	r6,_XER(r1)
	mfdar	r7
	std	r7,_DAR(r1)
	mfdsisr	r8
	std	r8,_DSISR(r1)

	/* Temporary workaround to clear CR until RTAS can be modified to
	 * ignore all bits.
	 */
	li	r0,0
	mtcr	r0

#ifdef CONFIG_BUG	
	/* There is no way it is acceptable to get here with interrupts enabled,
	 * check it with the asm equivalent of WARN_ON
	 */
	lbz	r0,PACASOFTIRQEN(r13)
1:	tdnei	r0,0
	EMIT_BUG_ENTRY 1b,__FILE__,__LINE__,BUGFLAG_WARNING
#endif
	
	/* Hard-disable interrupts */
	mfmsr	r6
	rldicl	r7,r6,48,1
	rotldi	r7,r7,16
	mtmsrd	r7,1

	/* Unfortunately, the stack pointer and the MSR are also clobbered,
	 * so they are saved in the PACA which allows us to restore
	 * our original state after RTAS returns.
         */
	std	r1,PACAR1(r13)
        std	r6,PACASAVEDMSR(r13)

	/* Setup our real return addr */	
	LOAD_REG_ADDR(r4,.rtas_return_loc)
	clrldi	r4,r4,2			/* convert to realmode address */
       	mtlr	r4

	li	r0,0
	ori	r0,r0,MSR_EE|MSR_SE|MSR_BE|MSR_RI
	andc	r0,r6,r0
	
        li      r9,1
        rldicr  r9,r9,MSR_SF_LG,(63-MSR_SF_LG)
	ori	r9,r9,MSR_IR|MSR_DR|MSR_FE0|MSR_FE1|MSR_FP
	andc	r6,r0,r9
	ori	r6,r6,MSR_RI
	sync				/* disable interrupts so SRR0/1 */
	mtmsrd	r0			/* don't get trashed */

	LOAD_REG_ADDR(r4, rtas)
	ld	r5,RTASENTRY(r4)	/* get the rtas->entry value */
	ld	r4,RTASBASE(r4)		/* get the rtas->base value */
	
	mtspr	SPRN_SRR0,r5
	mtspr	SPRN_SRR1,r6
	rfid
	b	.	/* prevent speculative execution */

_STATIC(rtas_return_loc)
	/* relocation is off at this point */
	mfspr	r4,SPRN_SPRG_PACA	/* Get PACA */
	clrldi	r4,r4,2			/* convert to realmode address */

	bcl	20,31,$+4
0:	mflr	r3
	ld	r3,(1f-0b)(r3)		/* get &.rtas_restore_regs */

	mfmsr   r6
	li	r0,MSR_RI
	andc	r6,r6,r0
	sync	
	mtmsrd  r6
        
        ld	r1,PACAR1(r4)           /* Restore our SP */
        ld	r4,PACASAVEDMSR(r4)     /* Restore our MSR */

	mtspr	SPRN_SRR0,r3
	mtspr	SPRN_SRR1,r4
	rfid
	b	.	/* prevent speculative execution */

	.align	3
1:	.llong	.rtas_restore_regs

_STATIC(rtas_restore_regs)
	/* relocation is on at this point */
	REST_GPR(2, r1)			/* Restore the TOC */
	REST_GPR(13, r1)		/* Restore paca */
	REST_8GPRS(14, r1)		/* Restore the non-volatiles */
	REST_10GPRS(22, r1)		/* ditto */

	mfspr	r13,SPRN_SPRG_PACA

	ld	r4,_CCR(r1)
	mtcr	r4
	ld	r5,_CTR(r1)
	mtctr	r5
	ld	r6,_XER(r1)
	mtspr	SPRN_XER,r6
	ld	r7,_DAR(r1)
	mtdar	r7
	ld	r8,_DSISR(r1)
	mtdsisr	r8

        addi	r1,r1,RTAS_FRAME_SIZE	/* Unstack our frame */
	ld	r0,16(r1)		/* get return address */

	mtlr    r0
        blr				/* return to caller */

#endif /* CONFIG_PPC_RTAS */

_GLOBAL(enter_prom)
	mflr	r0
	std	r0,16(r1)
        stdu	r1,-PROM_FRAME_SIZE(r1)	/* Save SP and create stack space */

	/* Because PROM is running in 32b mode, it clobbers the high order half
	 * of all registers that it saves.  We therefore save those registers
	 * PROM might touch to the stack.  (r0, r3-r13 are caller saved)
   	 */
	SAVE_GPR(2, r1)
	SAVE_GPR(13, r1)
	SAVE_8GPRS(14, r1)
	SAVE_10GPRS(22, r1)
	mfcr	r10
	mfmsr	r11
	std	r10,_CCR(r1)
	std	r11,_MSR(r1)

	/* Get the PROM entrypoint */
	mtlr	r4

	/* Switch MSR to 32 bits mode
	 */
#ifdef CONFIG_PPC_BOOK3E
	rlwinm	r11,r11,0,1,31
	mtmsr	r11
#else /* CONFIG_PPC_BOOK3E */
        mfmsr   r11
        li      r12,1
        rldicr  r12,r12,MSR_SF_LG,(63-MSR_SF_LG)
        andc    r11,r11,r12
        li      r12,1
        rldicr  r12,r12,MSR_ISF_LG,(63-MSR_ISF_LG)
        andc    r11,r11,r12
        mtmsrd  r11
#endif /* CONFIG_PPC_BOOK3E */
        isync

	/* Enter PROM here... */
	blrl

	/* Just make sure that r1 top 32 bits didn't get
	 * corrupt by OF
	 */
	rldicl	r1,r1,0,32

	/* Restore the MSR (back to 64 bits) */
	ld	r0,_MSR(r1)
	MTMSRD(r0)
        isync

	/* Restore other registers */
	REST_GPR(2, r1)
	REST_GPR(13, r1)
	REST_8GPRS(14, r1)
	REST_10GPRS(22, r1)
	ld	r4,_CCR(r1)
	mtcr	r4
	
        addi	r1,r1,PROM_FRAME_SIZE
	ld	r0,16(r1)
	mtlr    r0
        blr

#ifdef CONFIG_FUNCTION_TRACER
#ifdef CONFIG_DYNAMIC_FTRACE
_GLOBAL(mcount)
_GLOBAL(_mcount)
	blr

_GLOBAL(ftrace_caller)
	/* Taken from output of objdump from lib64/glibc */
	mflr	r3
	ld	r11, 0(r1)
	stdu	r1, -112(r1)
	std	r3, 128(r1)
	ld	r4, 16(r11)
	subi	r3, r3, MCOUNT_INSN_SIZE
.globl ftrace_call
ftrace_call:
	bl	ftrace_stub
	nop
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
.globl ftrace_graph_call
ftrace_graph_call:
	b	ftrace_graph_stub
_GLOBAL(ftrace_graph_stub)
#endif
	ld	r0, 128(r1)
	mtlr	r0
	addi	r1, r1, 112
_GLOBAL(ftrace_stub)
	blr
#else
_GLOBAL(mcount)
	blr

_GLOBAL(_mcount)
	/* Taken from output of objdump from lib64/glibc */
	mflr	r3
	ld	r11, 0(r1)
	stdu	r1, -112(r1)
	std	r3, 128(r1)
	ld	r4, 16(r11)

	subi	r3, r3, MCOUNT_INSN_SIZE
	LOAD_REG_ADDR(r5,ftrace_trace_function)
	ld	r5,0(r5)
	ld	r5,0(r5)
	mtctr	r5
	bctrl
	nop


#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	b	ftrace_graph_caller
#endif
	ld	r0, 128(r1)
	mtlr	r0
	addi	r1, r1, 112
_GLOBAL(ftrace_stub)
	blr

#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
_GLOBAL(ftrace_graph_caller)
	/* load r4 with local address */
	ld	r4, 128(r1)
	subi	r4, r4, MCOUNT_INSN_SIZE

	/* get the parent address */
	ld	r11, 112(r1)
	addi	r3, r11, 16

	bl	.prepare_ftrace_return
	nop

	ld	r0, 128(r1)
	mtlr	r0
	addi	r1, r1, 112
	blr

_GLOBAL(return_to_handler)
	/* need to save return values */
	std	r4,  -24(r1)
	std	r3,  -16(r1)
	std	r31, -8(r1)
	mr	r31, r1
	stdu	r1, -112(r1)

	bl	.ftrace_return_to_handler
	nop

	/* return value has real return address */
	mtlr	r3

	ld	r1, 0(r1)
	ld	r4,  -24(r1)
	ld	r3,  -16(r1)
	ld	r31, -8(r1)

	/* Jump back to real return address */
	blr

_GLOBAL(mod_return_to_handler)
	/* need to save return values */
	std	r4,  -32(r1)
	std	r3,  -24(r1)
	/* save TOC */
	std	r2,  -16(r1)
	std	r31, -8(r1)
	mr	r31, r1
	stdu	r1, -112(r1)

	/*
	 * We are in a module using the module's TOC.
	 * Switch to our TOC to run inside the core kernel.
	 */
	ld	r2, PACATOC(r13)

	bl	.ftrace_return_to_handler
	nop

	/* return value has real return address */
	mtlr	r3

	ld	r1, 0(r1)
	ld	r4,  -32(r1)
	ld	r3,  -24(r1)
	ld	r2,  -16(r1)
	ld	r31, -8(r1)

	/* Jump back to real return address */
	blr
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
#endif /* CONFIG_FUNCTION_TRACER */
