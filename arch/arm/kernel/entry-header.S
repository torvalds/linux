/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/errno.h>
#include <asm/thread_info.h>
#include <asm/uaccess-asm.h>
#include <asm/v7m.h>

@ Bad Abort numbers
@ -----------------
@
#define BAD_PREFETCH	0
#define BAD_DATA	1
#define BAD_ADDREXCPTN	2
#define BAD_IRQ		3
#define BAD_UNDEFINSTR	4

@
@ Most of the stack format comes from struct pt_regs, but with
@ the addition of 8 bytes for storing syscall args 5 and 6.
@ This _must_ remain a multiple of 8 for EABI.
@
#define S_OFF		8

/* 
 * The SWI code relies on the fact that R0 is at the bottom of the stack
 * (due to slow/fast restore user regs).
 */
#if S_R0 != 0
#error "Please fix"
#endif

	.macro	zero_fp
#ifdef CONFIG_FRAME_POINTER
	mov	fp, #0
#endif
	.endm

#ifdef CONFIG_ALIGNMENT_TRAP
#define ATRAP(x...) x
#else
#define ATRAP(x...)
#endif

	.macro	alignment_trap, rtmp1, rtmp2, label
#ifdef CONFIG_ALIGNMENT_TRAP
	mrc	p15, 0, \rtmp2, c1, c0, 0
	ldr	\rtmp1, \label
	ldr	\rtmp1, [\rtmp1]
	teq	\rtmp1, \rtmp2
	mcrne	p15, 0, \rtmp1, c1, c0, 0
#endif
	.endm

#ifdef CONFIG_CPU_V7M
/*
 * ARMv7-M exception entry/exit macros.
 *
 * xPSR, ReturnAddress(), LR (R14), R12, R3, R2, R1, and R0 are
 * automatically saved on the current stack (32 words) before
 * switching to the exception stack (SP_main).
 *
 * If exception is taken while in user mode, SP_main is
 * empty. Otherwise, SP_main is aligned to 64 bit automatically
 * (CCR.STKALIGN set).
 *
 * Linux assumes that the interrupts are disabled when entering an
 * exception handler and it may BUG if this is not the case. Interrupts
 * are disabled during entry and reenabled in the exit macro.
 *
 * v7m_exception_slow_exit is used when returning from SVC or PendSV.
 * When returning to kernel mode, we don't return from exception.
 */
	.macro	v7m_exception_entry
	@ determine the location of the registers saved by the core during
	@ exception entry. Depending on the mode the cpu was in when the
	@ exception happend that is either on the main or the process stack.
	@ Bit 2 of EXC_RETURN stored in the lr register specifies which stack
	@ was used.
	tst	lr, #EXC_RET_STACK_MASK
	mrsne	r12, psp
	moveq	r12, sp

	@ we cannot rely on r0-r3 and r12 matching the value saved in the
	@ exception frame because of tail-chaining. So these have to be
	@ reloaded.
	ldmia	r12!, {r0-r3}

	@ Linux expects to have irqs off. Do it here before taking stack space
	cpsid	i

	sub	sp, #PT_REGS_SIZE-S_IP
	stmdb	sp!, {r0-r11}

	@ load saved r12, lr, return address and xPSR.
	@ r0-r7 are used for signals and never touched from now on. Clobbering
	@ r8-r12 is OK.
	mov	r9, r12
	ldmia	r9!, {r8, r10-r12}

	@ calculate the original stack pointer value.
	@ r9 currently points to the memory location just above the auto saved
	@ xPSR.
	@ The cpu might automatically 8-byte align the stack. Bit 9
	@ of the saved xPSR specifies if stack aligning took place. In this case
	@ another 32-bit value is included in the stack.

	tst	r12, V7M_xPSR_FRAMEPTRALIGN
	addne	r9, r9, #4

	@ store saved r12 using str to have a register to hold the base for stm
	str	r8, [sp, #S_IP]
	add	r8, sp, #S_SP
	@ store r13-r15, xPSR
	stmia	r8!, {r9-r12}
	@ store old_r0
	str	r0, [r8]
	.endm

        /*
	 * PENDSV and SVCALL are configured to have the same exception
	 * priorities. As a kernel thread runs at SVCALL execution priority it
	 * can never be preempted and so we will never have to return to a
	 * kernel thread here.
         */
	.macro	v7m_exception_slow_exit ret_r0
	cpsid	i
	ldr	lr, =exc_ret
	ldr	lr, [lr]

	@ read original r12, sp, lr, pc and xPSR
	add	r12, sp, #S_IP
	ldmia	r12, {r1-r5}

	@ an exception frame is always 8-byte aligned. To tell the hardware if
	@ the sp to be restored is aligned or not set bit 9 of the saved xPSR
	@ accordingly.
	tst	r2, #4
	subne	r2, r2, #4
	orrne	r5, V7M_xPSR_FRAMEPTRALIGN
	biceq	r5, V7M_xPSR_FRAMEPTRALIGN

	@ ensure bit 0 is cleared in the PC, otherwise behaviour is
	@ unpredictable
	bic	r4, #1

	@ write basic exception frame
	stmdb	r2!, {r1, r3-r5}
	ldmia	sp, {r1, r3-r5}
	.if	\ret_r0
	stmdb	r2!, {r0, r3-r5}
	.else
	stmdb	r2!, {r1, r3-r5}
	.endif

	@ restore process sp
	msr	psp, r2

	@ restore original r4-r11
	ldmia	sp!, {r0-r11}

	@ restore main sp
	add	sp, sp, #PT_REGS_SIZE-S_IP

	cpsie	i
	bx	lr
	.endm
#endif	/* CONFIG_CPU_V7M */

	@
	@ Store/load the USER SP and LR registers by switching to the SYS
	@ mode. Useful in Thumb-2 mode where "stm/ldm rd, {sp, lr}^" is not
	@ available. Should only be called from SVC mode
	@
	.macro	store_user_sp_lr, rd, rtemp, offset = 0
	mrs	\rtemp, cpsr
	eor	\rtemp, \rtemp, #(SVC_MODE ^ SYSTEM_MODE)
	msr	cpsr_c, \rtemp			@ switch to the SYS mode

	str	sp, [\rd, #\offset]		@ save sp_usr
	str	lr, [\rd, #\offset + 4]		@ save lr_usr

	eor	\rtemp, \rtemp, #(SVC_MODE ^ SYSTEM_MODE)
	msr	cpsr_c, \rtemp			@ switch back to the SVC mode
	.endm

	.macro	load_user_sp_lr, rd, rtemp, offset = 0
	mrs	\rtemp, cpsr
	eor	\rtemp, \rtemp, #(SVC_MODE ^ SYSTEM_MODE)
	msr	cpsr_c, \rtemp			@ switch to the SYS mode

	ldr	sp, [\rd, #\offset]		@ load sp_usr
	ldr	lr, [\rd, #\offset + 4]		@ load lr_usr

	eor	\rtemp, \rtemp, #(SVC_MODE ^ SYSTEM_MODE)
	msr	cpsr_c, \rtemp			@ switch back to the SVC mode
	.endm


	.macro	svc_exit, rpsr, irq = 0
	.if	\irq != 0
	@ IRQs already off
#ifdef CONFIG_TRACE_IRQFLAGS
	@ The parent context IRQs must have been enabled to get here in
	@ the first place, so there's no point checking the PSR I bit.
	bl	trace_hardirqs_on
#endif
	.else
	@ IRQs off again before pulling preserved data off the stack
	disable_irq_notrace
#ifdef CONFIG_TRACE_IRQFLAGS
	tst	\rpsr, #PSR_I_BIT
	bleq	trace_hardirqs_on
	tst	\rpsr, #PSR_I_BIT
	blne	trace_hardirqs_off
#endif
	.endif
	uaccess_exit tsk, r0, r1

#ifndef CONFIG_THUMB2_KERNEL
	@ ARM mode SVC restore
	msr	spsr_cxsf, \rpsr
#if defined(CONFIG_CPU_V6) || defined(CONFIG_CPU_32v6K)
	@ We must avoid clrex due to Cortex-A15 erratum #830321
	sub	r0, sp, #4			@ uninhabited address
	strex	r1, r2, [r0]			@ clear the exclusive monitor
#endif
	ldmia	sp, {r0 - pc}^			@ load r0 - pc, cpsr
#else
	@ Thumb mode SVC restore
	ldr	lr, [sp, #S_SP]			@ top of the stack
	ldrd	r0, r1, [sp, #S_LR]		@ calling lr and pc

	@ We must avoid clrex due to Cortex-A15 erratum #830321
	strex	r2, r1, [sp, #S_LR]		@ clear the exclusive monitor

	stmdb	lr!, {r0, r1, \rpsr}		@ calling lr and rfe context
	ldmia	sp, {r0 - r12}
	mov	sp, lr
	ldr	lr, [sp], #4
	rfeia	sp!
#endif
	.endm

	@
	@ svc_exit_via_fiq - like svc_exit but switches to FIQ mode before exit
	@
	@ This macro acts in a similar manner to svc_exit but switches to FIQ
	@ mode to restore the final part of the register state.
	@
	@ We cannot use the normal svc_exit procedure because that would
	@ clobber spsr_svc (FIQ could be delivered during the first few
	@ instructions of vector_swi meaning its contents have not been
	@ saved anywhere).
	@
	@ Note that, unlike svc_exit, this macro also does not allow a caller
	@ supplied rpsr. This is because the FIQ exceptions are not re-entrant
	@ and the handlers cannot call into the scheduler (meaning the value
	@ on the stack remains correct).
	@
	.macro  svc_exit_via_fiq
	uaccess_exit tsk, r0, r1
#ifndef CONFIG_THUMB2_KERNEL
	@ ARM mode restore
	mov	r0, sp
	ldmib	r0, {r1 - r14}	@ abort is deadly from here onward (it will
				@ clobber state restored below)
	msr	cpsr_c, #FIQ_MODE | PSR_I_BIT | PSR_F_BIT
	add	r8, r0, #S_PC
	ldr	r9, [r0, #S_PSR]
	msr	spsr_cxsf, r9
	ldr	r0, [r0, #S_R0]
	ldmia	r8, {pc}^
#else
	@ Thumb mode restore
	add	r0, sp, #S_R2
	ldr	lr, [sp, #S_LR]
	ldr	sp, [sp, #S_SP] @ abort is deadly from here onward (it will
			        @ clobber state restored below)
	ldmia	r0, {r2 - r12}
	mov	r1, #FIQ_MODE | PSR_I_BIT | PSR_F_BIT
	msr	cpsr_c, r1
	sub	r0, #S_R2
	add	r8, r0, #S_PC
	ldmia	r0, {r0 - r1}
	rfeia	r8
#endif
	.endm


	.macro	restore_user_regs, fast = 0, offset = 0
#if defined(CONFIG_CPU_32v6K) && !defined(CONFIG_CPU_V6)
	@ The TLS register update is deferred until return to user space so we
	@ can use it for other things while running in the kernel
	get_thread_info r1
	ldr	r1, [r1, #TI_TP_VALUE]
	mcr	p15, 0, r1, c13, c0, 3		@ set TLS register
#endif

	uaccess_enable r1, isb=0
#ifndef CONFIG_THUMB2_KERNEL
	@ ARM mode restore
	mov	r2, sp
	ldr	r1, [r2, #\offset + S_PSR]	@ get calling cpsr
	ldr	lr, [r2, #\offset + S_PC]!	@ get pc
	tst	r1, #PSR_I_BIT | 0x0f
	bne	1f
	msr	spsr_cxsf, r1			@ save in spsr_svc
#if defined(CONFIG_CPU_V6) || defined(CONFIG_CPU_32v6K)
	@ We must avoid clrex due to Cortex-A15 erratum #830321
	strex	r1, r2, [r2]			@ clear the exclusive monitor
#endif
	.if	\fast
	ldmdb	r2, {r1 - lr}^			@ get calling r1 - lr
	.else
	ldmdb	r2, {r0 - lr}^			@ get calling r0 - lr
	.endif
	mov	r0, r0				@ ARMv5T and earlier require a nop
						@ after ldm {}^
	add	sp, sp, #\offset + PT_REGS_SIZE
	movs	pc, lr				@ return & move spsr_svc into cpsr
1:	bug	"Returning to usermode but unexpected PSR bits set?", \@
#elif defined(CONFIG_CPU_V7M)
	@ V7M restore.
	@ Note that we don't need to do clrex here as clearing the local
	@ monitor is part of the exception entry and exit sequence.
	.if	\offset
	add	sp, #\offset
	.endif
	v7m_exception_slow_exit ret_r0 = \fast
#else
	@ Thumb mode restore
	mov	r2, sp
	load_user_sp_lr r2, r3, \offset + S_SP	@ calling sp, lr
	ldr	r1, [sp, #\offset + S_PSR]	@ get calling cpsr
	ldr	lr, [sp, #\offset + S_PC]	@ get pc
	add	sp, sp, #\offset + S_SP
	tst	r1, #PSR_I_BIT | 0x0f
	bne	1f
	msr	spsr_cxsf, r1			@ save in spsr_svc

	@ We must avoid clrex due to Cortex-A15 erratum #830321
	strex	r1, r2, [sp]			@ clear the exclusive monitor

	.if	\fast
	ldmdb	sp, {r1 - r12}			@ get calling r1 - r12
	.else
	ldmdb	sp, {r0 - r12}			@ get calling r0 - r12
	.endif
	add	sp, sp, #PT_REGS_SIZE - S_SP
	movs	pc, lr				@ return & move spsr_svc into cpsr
1:	bug	"Returning to usermode but unexpected PSR bits set?", \@
#endif	/* !CONFIG_THUMB2_KERNEL */
	.endm

/*
 * Context tracking subsystem.  Used to instrument transitions
 * between user and kernel mode.
 */
	.macro ct_user_exit, save = 1
#ifdef CONFIG_CONTEXT_TRACKING
	.if	\save
	stmdb   sp!, {r0-r3, ip, lr}
	bl	context_tracking_user_exit
	ldmia	sp!, {r0-r3, ip, lr}
	.else
	bl	context_tracking_user_exit
	.endif
#endif
	.endm

	.macro ct_user_enter, save = 1
#ifdef CONFIG_CONTEXT_TRACKING
	.if	\save
	stmdb   sp!, {r0-r3, ip, lr}
	bl	context_tracking_user_enter
	ldmia	sp!, {r0-r3, ip, lr}
	.else
	bl	context_tracking_user_enter
	.endif
#endif
	.endm

	.macro	invoke_syscall, table, nr, tmp, ret, reload=0
#ifdef CONFIG_CPU_SPECTRE
	mov	\tmp, \nr
	cmp	\tmp, #NR_syscalls		@ check upper syscall limit
	movcs	\tmp, #0
	csdb
	badr	lr, \ret			@ return address
	.if	\reload
	add	r1, sp, #S_R0 + S_OFF		@ pointer to regs
	ldmiacc	r1, {r0 - r6}			@ reload r0-r6
	stmiacc	sp, {r4, r5}			@ update stack arguments
	.endif
	ldrcc	pc, [\table, \tmp, lsl #2]	@ call sys_* routine
#else
	cmp	\nr, #NR_syscalls		@ check upper syscall limit
	badr	lr, \ret			@ return address
	.if	\reload
	add	r1, sp, #S_R0 + S_OFF		@ pointer to regs
	ldmiacc	r1, {r0 - r6}			@ reload r0-r6
	stmiacc	sp, {r4, r5}			@ update stack arguments
	.endif
	ldrcc	pc, [\table, \nr, lsl #2]	@ call sys_* routine
#endif
	.endm

/*
 * These are the registers used in the syscall handler, and allow us to
 * have in theory up to 7 arguments to a function - r0 to r6.
 *
 * r7 is reserved for the system call number for thumb mode.
 *
 * Note that tbl == why is intentional.
 *
 * We must set at least "tsk" and "why" when calling ret_with_reschedule.
 */
scno	.req	r7		@ syscall number
tbl	.req	r8		@ syscall table pointer
why	.req	r8		@ Linux syscall (!= 0)
tsk	.req	r9		@ current thread_info
