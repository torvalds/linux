/*
 *  linux/arch/arm/kernel/entry-armv.S
 *
 *  Copyright (C) 1996,1997,1998 Russell King.
 *  ARM700 fix by Matthew Godbolt (linux-user@willothewisp.demon.co.uk)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Low-level vector interface routines
 *
 *  Note:  there is a StrongARM bug in the STMIA rn, {regs}^ instruction that causes
 *  it to save wrong values...  Be aware!
 */
#include <linux/config.h>

#include <asm/memory.h>
#include <asm/glue.h>
#include <asm/vfpmacros.h>
#include <asm/hardware.h>		/* should be moved into entry-macro.S */
#include <asm/arch/irqs.h>		/* should be moved into entry-macro.S */
#include <asm/arch/entry-macro.S>

#include "entry-header.S"

/*
 * Interrupt handling.  Preserves r7, r8, r9
 */
	.macro	irq_handler
1:	get_irqnr_and_base r0, r6, r5, lr
	movne	r1, sp
	@
	@ routine called with r0 = irq number, r1 = struct pt_regs *
	@
	adrne	lr, 1b
	bne	asm_do_IRQ

#ifdef CONFIG_SMP
	/*
	 * XXX
	 *
	 * this macro assumes that irqstat (r6) and base (r5) are
	 * preserved from get_irqnr_and_base above
	 */
	test_for_ipi r0, r6, r5, lr
	movne	r0, sp
	adrne	lr, 1b
	bne	do_IPI

#ifdef CONFIG_LOCAL_TIMERS
	test_for_ltirq r0, r6, r5, lr
	movne	r0, sp
	adrne	lr, 1b
	bne	do_local_timer
#endif
#endif

	.endm

/*
 * Invalid mode handlers
 */
	.macro	inv_entry, reason
	sub	sp, sp, #S_FRAME_SIZE
	stmib	sp, {r1 - lr}
	mov	r1, #\reason
	.endm

__pabt_invalid:
	inv_entry BAD_PREFETCH
	b	common_invalid

__dabt_invalid:
	inv_entry BAD_DATA
	b	common_invalid

__irq_invalid:
	inv_entry BAD_IRQ
	b	common_invalid

__und_invalid:
	inv_entry BAD_UNDEFINSTR

	@
	@ XXX fall through to common_invalid
	@

@
@ common_invalid - generic code for failed exception (re-entrant version of handlers)
@
common_invalid:
	zero_fp

	ldmia	r0, {r4 - r6}
	add	r0, sp, #S_PC		@ here for interlock avoidance
	mov	r7, #-1			@  ""   ""    ""        ""
	str	r4, [sp]		@ save preserved r0
	stmia	r0, {r5 - r7}		@ lr_<exception>,
					@ cpsr_<exception>, "old_r0"

	mov	r0, sp
	and	r2, r6, #0x1f
	b	bad_mode

/*
 * SVC mode handlers
 */
	.macro	svc_entry
	sub	sp, sp, #S_FRAME_SIZE
	stmib	sp, {r1 - r12}

	ldmia	r0, {r1 - r3}
	add	r5, sp, #S_SP		@ here for interlock avoidance
	mov	r4, #-1			@  ""  ""      ""       ""
	add	r0, sp, #S_FRAME_SIZE   @  ""  ""      ""       ""
	str	r1, [sp]		@ save the "real" r0 copied
					@ from the exception stack

	mov	r1, lr

	@
	@ We are now ready to fill in the remaining blanks on the stack:
	@
	@  r0 - sp_svc
	@  r1 - lr_svc
	@  r2 - lr_<exception>, already fixed up for correct return/restart
	@  r3 - spsr_<exception>
	@  r4 - orig_r0 (see pt_regs definition in ptrace.h)
	@
	stmia	r5, {r0 - r4}
	.endm

	.align	5
__dabt_svc:
	svc_entry

	@
	@ get ready to re-enable interrupts if appropriate
	@
	mrs	r9, cpsr
	tst	r3, #PSR_I_BIT
	biceq	r9, r9, #PSR_I_BIT

	@
	@ Call the processor-specific abort handler:
	@
	@  r2 - aborted context pc
	@  r3 - aborted context cpsr
	@
	@ The abort handler must return the aborted address in r0, and
	@ the fault status register in r1.  r9 must be preserved.
	@
#ifdef MULTI_ABORT
	ldr	r4, .LCprocfns
	mov	lr, pc
	ldr	pc, [r4]
#else
	bl	CPU_ABORT_HANDLER
#endif

	@
	@ set desired IRQ state, then call main handler
	@
	msr	cpsr_c, r9
	mov	r2, sp
	bl	do_DataAbort

	@
	@ IRQs off again before pulling preserved data off the stack
	@
	disable_irq

	@
	@ restore SPSR and restart the instruction
	@
	ldr	r0, [sp, #S_PSR]
	msr	spsr_cxsf, r0
	ldmia	sp, {r0 - pc}^			@ load r0 - pc, cpsr

	.align	5
__irq_svc:
	svc_entry

#ifdef CONFIG_PREEMPT
	get_thread_info tsk
	ldr	r8, [tsk, #TI_PREEMPT]		@ get preempt count
	add	r7, r8, #1			@ increment it
	str	r7, [tsk, #TI_PREEMPT]
#endif

	irq_handler
#ifdef CONFIG_PREEMPT
	ldr	r0, [tsk, #TI_FLAGS]		@ get flags
	tst	r0, #_TIF_NEED_RESCHED
	blne	svc_preempt
preempt_return:
	ldr	r0, [tsk, #TI_PREEMPT]		@ read preempt value
	str	r8, [tsk, #TI_PREEMPT]		@ restore preempt count
	teq	r0, r7
	strne	r0, [r0, -r0]			@ bug()
#endif
	ldr	r0, [sp, #S_PSR]		@ irqs are already disabled
	msr	spsr_cxsf, r0
	ldmia	sp, {r0 - pc}^			@ load r0 - pc, cpsr

	.ltorg

#ifdef CONFIG_PREEMPT
svc_preempt:
	teq	r8, #0				@ was preempt count = 0
	ldreq	r6, .LCirq_stat
	movne	pc, lr				@ no
	ldr	r0, [r6, #4]			@ local_irq_count
	ldr	r1, [r6, #8]			@ local_bh_count
	adds	r0, r0, r1
	movne	pc, lr
	mov	r7, #0				@ preempt_schedule_irq
	str	r7, [tsk, #TI_PREEMPT]		@ expects preempt_count == 0
1:	bl	preempt_schedule_irq		@ irq en/disable is done inside
	ldr	r0, [tsk, #TI_FLAGS]		@ get new tasks TI_FLAGS
	tst	r0, #_TIF_NEED_RESCHED
	beq	preempt_return			@ go again
	b	1b
#endif

	.align	5
__und_svc:
	svc_entry

	@
	@ call emulation code, which returns using r9 if it has emulated
	@ the instruction, or the more conventional lr if we are to treat
	@ this as a real undefined instruction
	@
	@  r0 - instruction
	@
	ldr	r0, [r2, #-4]
	adr	r9, 1f
	bl	call_fpe

	mov	r0, sp				@ struct pt_regs *regs
	bl	do_undefinstr

	@
	@ IRQs off again before pulling preserved data off the stack
	@
1:	disable_irq

	@
	@ restore SPSR and restart the instruction
	@
	ldr	lr, [sp, #S_PSR]		@ Get SVC cpsr
	msr	spsr_cxsf, lr
	ldmia	sp, {r0 - pc}^			@ Restore SVC registers

	.align	5
__pabt_svc:
	svc_entry

	@
	@ re-enable interrupts if appropriate
	@
	mrs	r9, cpsr
	tst	r3, #PSR_I_BIT
	biceq	r9, r9, #PSR_I_BIT
	msr	cpsr_c, r9

	@
	@ set args, then call main handler
	@
	@  r0 - address of faulting instruction
	@  r1 - pointer to registers on stack
	@
	mov	r0, r2				@ address (pc)
	mov	r1, sp				@ regs
	bl	do_PrefetchAbort		@ call abort handler

	@
	@ IRQs off again before pulling preserved data off the stack
	@
	disable_irq

	@
	@ restore SPSR and restart the instruction
	@
	ldr	r0, [sp, #S_PSR]
	msr	spsr_cxsf, r0
	ldmia	sp, {r0 - pc}^			@ load r0 - pc, cpsr

	.align	5
.LCcralign:
	.word	cr_alignment
#ifdef MULTI_ABORT
.LCprocfns:
	.word	processor
#endif
.LCfp:
	.word	fp_enter
#ifdef CONFIG_PREEMPT
.LCirq_stat:
	.word	irq_stat
#endif

/*
 * User mode handlers
 */
	.macro	usr_entry
	sub	sp, sp, #S_FRAME_SIZE
	stmib	sp, {r1 - r12}

	ldmia	r0, {r1 - r3}
	add	r0, sp, #S_PC		@ here for interlock avoidance
	mov	r4, #-1			@  ""  ""     ""        ""

	str	r1, [sp]		@ save the "real" r0 copied
					@ from the exception stack

#if __LINUX_ARM_ARCH__ < 6 && !defined(CONFIG_NEEDS_SYSCALL_FOR_CMPXCHG)
	@ make sure our user space atomic helper is aborted
	cmp	r2, #TASK_SIZE
	bichs	r3, r3, #PSR_Z_BIT
#endif

	@
	@ We are now ready to fill in the remaining blanks on the stack:
	@
	@  r2 - lr_<exception>, already fixed up for correct return/restart
	@  r3 - spsr_<exception>
	@  r4 - orig_r0 (see pt_regs definition in ptrace.h)
	@
	@ Also, separately save sp_usr and lr_usr
	@
	stmia	r0, {r2 - r4}
	stmdb	r0, {sp, lr}^

	@
	@ Enable the alignment trap while in kernel mode
	@
	alignment_trap r0

	@
	@ Clear FP to mark the first stack frame
	@
	zero_fp
	.endm

	.align	5
__dabt_usr:
	usr_entry

	@
	@ Call the processor-specific abort handler:
	@
	@  r2 - aborted context pc
	@  r3 - aborted context cpsr
	@
	@ The abort handler must return the aborted address in r0, and
	@ the fault status register in r1.
	@
#ifdef MULTI_ABORT
	ldr	r4, .LCprocfns
	mov	lr, pc
	ldr	pc, [r4]
#else
	bl	CPU_ABORT_HANDLER
#endif

	@
	@ IRQs on, then call the main handler
	@
	enable_irq
	mov	r2, sp
	adr	lr, ret_from_exception
	b	do_DataAbort

	.align	5
__irq_usr:
	usr_entry

	get_thread_info tsk
#ifdef CONFIG_PREEMPT
	ldr	r8, [tsk, #TI_PREEMPT]		@ get preempt count
	add	r7, r8, #1			@ increment it
	str	r7, [tsk, #TI_PREEMPT]
#endif

	irq_handler
#ifdef CONFIG_PREEMPT
	ldr	r0, [tsk, #TI_PREEMPT]
	str	r8, [tsk, #TI_PREEMPT]
	teq	r0, r7
	strne	r0, [r0, -r0]
#endif

	mov	why, #0
	b	ret_to_user

	.ltorg

	.align	5
__und_usr:
	usr_entry

	tst	r3, #PSR_T_BIT			@ Thumb mode?
	bne	fpundefinstr			@ ignore FP
	sub	r4, r2, #4

	@
	@ fall through to the emulation code, which returns using r9 if
	@ it has emulated the instruction, or the more conventional lr
	@ if we are to treat this as a real undefined instruction
	@
	@  r0 - instruction
	@
1:	ldrt	r0, [r4]
	adr	r9, ret_from_exception
	adr	lr, fpundefinstr
	@
	@ fallthrough to call_fpe
	@

/*
 * The out of line fixup for the ldrt above.
 */
	.section .fixup, "ax"
2:	mov	pc, r9
	.previous
	.section __ex_table,"a"
	.long	1b, 2b
	.previous

/*
 * Check whether the instruction is a co-processor instruction.
 * If yes, we need to call the relevant co-processor handler.
 *
 * Note that we don't do a full check here for the co-processor
 * instructions; all instructions with bit 27 set are well
 * defined.  The only instructions that should fault are the
 * co-processor instructions.  However, we have to watch out
 * for the ARM6/ARM7 SWI bug.
 *
 * Emulators may wish to make use of the following registers:
 *  r0  = instruction opcode.
 *  r2  = PC+4
 *  r10 = this threads thread_info structure.
 */
call_fpe:
	tst	r0, #0x08000000			@ only CDP/CPRT/LDC/STC have bit 27
#if defined(CONFIG_CPU_ARM610) || defined(CONFIG_CPU_ARM710)
	and	r8, r0, #0x0f000000		@ mask out op-code bits
	teqne	r8, #0x0f000000			@ SWI (ARM6/7 bug)?
#endif
	moveq	pc, lr
	get_thread_info r10			@ get current thread
	and	r8, r0, #0x00000f00		@ mask out CP number
	mov	r7, #1
	add	r6, r10, #TI_USED_CP
	strb	r7, [r6, r8, lsr #8]		@ set appropriate used_cp[]
#ifdef CONFIG_IWMMXT
	@ Test if we need to give access to iWMMXt coprocessors
	ldr	r5, [r10, #TI_FLAGS]
	rsbs	r7, r8, #(1 << 8)		@ CP 0 or 1 only
	movcss	r7, r5, lsr #(TIF_USING_IWMMXT + 1)
	bcs	iwmmxt_task_enable
#endif
	enable_irq
	add	pc, pc, r8, lsr #6
	mov	r0, r0

	mov	pc, lr				@ CP#0
	b	do_fpe				@ CP#1 (FPE)
	b	do_fpe				@ CP#2 (FPE)
	mov	pc, lr				@ CP#3
	mov	pc, lr				@ CP#4
	mov	pc, lr				@ CP#5
	mov	pc, lr				@ CP#6
	mov	pc, lr				@ CP#7
	mov	pc, lr				@ CP#8
	mov	pc, lr				@ CP#9
#ifdef CONFIG_VFP
	b	do_vfp				@ CP#10 (VFP)
	b	do_vfp				@ CP#11 (VFP)
#else
	mov	pc, lr				@ CP#10 (VFP)
	mov	pc, lr				@ CP#11 (VFP)
#endif
	mov	pc, lr				@ CP#12
	mov	pc, lr				@ CP#13
	mov	pc, lr				@ CP#14 (Debug)
	mov	pc, lr				@ CP#15 (Control)

do_fpe:
	ldr	r4, .LCfp
	add	r10, r10, #TI_FPSTATE		@ r10 = workspace
	ldr	pc, [r4]			@ Call FP module USR entry point

/*
 * The FP module is called with these registers set:
 *  r0  = instruction
 *  r2  = PC+4
 *  r9  = normal "successful" return address
 *  r10 = FP workspace
 *  lr  = unrecognised FP instruction return address
 */

	.data
ENTRY(fp_enter)
	.word	fpundefinstr
	.text

fpundefinstr:
	mov	r0, sp
	adr	lr, ret_from_exception
	b	do_undefinstr

	.align	5
__pabt_usr:
	usr_entry

	enable_irq				@ Enable interrupts
	mov	r0, r2				@ address (pc)
	mov	r1, sp				@ regs
	bl	do_PrefetchAbort		@ call abort handler
	/* fall through */
/*
 * This is the return code to user mode for abort handlers
 */
ENTRY(ret_from_exception)
	get_thread_info tsk
	mov	why, #0
	b	ret_to_user

/*
 * Register switch for ARMv3 and ARMv4 processors
 * r0 = previous task_struct, r1 = previous thread_info, r2 = next thread_info
 * previous and next are guaranteed not to be the same.
 */
ENTRY(__switch_to)
	add	ip, r1, #TI_CPU_SAVE
	ldr	r3, [r2, #TI_TP_VALUE]
	stmia	ip!, {r4 - sl, fp, sp, lr}	@ Store most regs on stack
	ldr	r6, [r2, #TI_CPU_DOMAIN]!
#if __LINUX_ARM_ARCH__ >= 6
#ifdef CONFIG_CPU_MPCORE
	clrex
#else
	strex	r5, r4, [ip]			@ Clear exclusive monitor
#endif
#endif
#if defined(CONFIG_CPU_XSCALE) && !defined(CONFIG_IWMMXT)
	mra	r4, r5, acc0
	stmia   ip, {r4, r5}
#endif
#if defined(CONFIG_HAS_TLS_REG)
	mcr	p15, 0, r3, c13, c0, 3		@ set TLS register
#elif !defined(CONFIG_TLS_REG_EMUL)
	mov	r4, #0xffff0fff
	str	r3, [r4, #-15]			@ TLS val at 0xffff0ff0
#endif
	mcr	p15, 0, r6, c3, c0, 0		@ Set domain register
#ifdef CONFIG_VFP
	@ Always disable VFP so we can lazily save/restore the old
	@ state. This occurs in the context of the previous thread.
	VFPFMRX	r4, FPEXC
	bic	r4, r4, #FPEXC_ENABLE
	VFPFMXR	FPEXC, r4
#endif
#if defined(CONFIG_IWMMXT)
	bl	iwmmxt_task_switch
#elif defined(CONFIG_CPU_XSCALE)
	add	r4, r2, #40			@ cpu_context_save->extra
	ldmib	r4, {r4, r5}
	mar	acc0, r4, r5
#endif
	ldmib	r2, {r4 - sl, fp, sp, pc}	@ Load all regs saved previously

	__INIT

/*
 * User helpers.
 *
 * These are segment of kernel provided user code reachable from user space
 * at a fixed address in kernel memory.  This is used to provide user space
 * with some operations which require kernel help because of unimplemented
 * native feature and/or instructions in many ARM CPUs. The idea is for
 * this code to be executed directly in user mode for best efficiency but
 * which is too intimate with the kernel counter part to be left to user
 * libraries.  In fact this code might even differ from one CPU to another
 * depending on the available  instruction set and restrictions like on
 * SMP systems.  In other words, the kernel reserves the right to change
 * this code as needed without warning. Only the entry points and their
 * results are guaranteed to be stable.
 *
 * Each segment is 32-byte aligned and will be moved to the top of the high
 * vector page.  New segments (if ever needed) must be added in front of
 * existing ones.  This mechanism should be used only for things that are
 * really small and justified, and not be abused freely.
 *
 * User space is expected to implement those things inline when optimizing
 * for a processor that has the necessary native support, but only if such
 * resulting binaries are already to be incompatible with earlier ARM
 * processors due to the use of unsupported instructions other than what
 * is provided here.  In other words don't make binaries unable to run on
 * earlier processors just for the sake of not using these kernel helpers
 * if your compiled code is not going to use the new instructions for other
 * purpose.
 */

	.align	5
	.globl	__kuser_helper_start
__kuser_helper_start:

/*
 * Reference prototype:
 *
 *	int __kernel_cmpxchg(int oldval, int newval, int *ptr)
 *
 * Input:
 *
 *	r0 = oldval
 *	r1 = newval
 *	r2 = ptr
 *	lr = return address
 *
 * Output:
 *
 *	r0 = returned value (zero or non-zero)
 *	C flag = set if r0 == 0, clear if r0 != 0
 *
 * Clobbered:
 *
 *	r3, ip, flags
 *
 * Definition and user space usage example:
 *
 *	typedef int (__kernel_cmpxchg_t)(int oldval, int newval, int *ptr);
 *	#define __kernel_cmpxchg (*(__kernel_cmpxchg_t *)0xffff0fc0)
 *
 * Atomically store newval in *ptr if *ptr is equal to oldval for user space.
 * Return zero if *ptr was changed or non-zero if no exchange happened.
 * The C flag is also set if *ptr was changed to allow for assembly
 * optimization in the calling code.
 *
 * For example, a user space atomic_add implementation could look like this:
 *
 * #define atomic_add(ptr, val) \
 *	({ register unsigned int *__ptr asm("r2") = (ptr); \
 *	   register unsigned int __result asm("r1"); \
 *	   asm volatile ( \
 *	       "1: @ atomic_add\n\t" \
 *	       "ldr	r0, [r2]\n\t" \
 *	       "mov	r3, #0xffff0fff\n\t" \
 *	       "add	lr, pc, #4\n\t" \
 *	       "add	r1, r0, %2\n\t" \
 *	       "add	pc, r3, #(0xffff0fc0 - 0xffff0fff)\n\t" \
 *	       "bcc	1b" \
 *	       : "=&r" (__result) \
 *	       : "r" (__ptr), "rIL" (val) \
 *	       : "r0","r3","ip","lr","cc","memory" ); \
 *	   __result; })
 */

__kuser_cmpxchg:				@ 0xffff0fc0

#if defined(CONFIG_NEEDS_SYSCALL_FOR_CMPXCHG)

	/*
	 * Poor you.  No fast solution possible...
	 * The kernel itself must perform the operation.
	 * A special ghost syscall is used for that (see traps.c).
	 */
	swi	#0x9ffff0
	mov	pc, lr

#elif __LINUX_ARM_ARCH__ < 6

	/*
	 * Theory of operation:
	 *
	 * We set the Z flag before loading oldval. If ever an exception
	 * occurs we can not be sure the loaded value will still be the same
	 * when the exception returns, therefore the user exception handler
	 * will clear the Z flag whenever the interrupted user code was
	 * actually from the kernel address space (see the usr_entry macro).
	 *
	 * The post-increment on the str is used to prevent a race with an
	 * exception happening just after the str instruction which would
	 * clear the Z flag although the exchange was done.
	 */
	teq	ip, ip			@ set Z flag
	ldr	ip, [r2]		@ load current val
	add	r3, r2, #1		@ prepare store ptr
	teqeq	ip, r0			@ compare with oldval if still allowed
	streq	r1, [r3, #-1]!		@ store newval if still allowed
	subs	r0, r2, r3		@ if r2 == r3 the str occured
	mov	pc, lr

#else

	ldrex	r3, [r2]
	subs	r3, r3, r0
	strexeq	r3, r1, [r2]
	rsbs	r0, r3, #0
	mov	pc, lr

#endif

	.align	5

/*
 * Reference prototype:
 *
 *	int __kernel_get_tls(void)
 *
 * Input:
 *
 *	lr = return address
 *
 * Output:
 *
 *	r0 = TLS value
 *
 * Clobbered:
 *
 *	the Z flag might be lost
 *
 * Definition and user space usage example:
 *
 *	typedef int (__kernel_get_tls_t)(void);
 *	#define __kernel_get_tls (*(__kernel_get_tls_t *)0xffff0fe0)
 *
 * Get the TLS value as previously set via the __ARM_NR_set_tls syscall.
 *
 * This could be used as follows:
 *
 * #define __kernel_get_tls() \
 *	({ register unsigned int __val asm("r0"); \
 *         asm( "mov r0, #0xffff0fff; mov lr, pc; sub pc, r0, #31" \
 *	        : "=r" (__val) : : "lr","cc" ); \
 *	   __val; })
 */

__kuser_get_tls:				@ 0xffff0fe0

#if !defined(CONFIG_HAS_TLS_REG) && !defined(CONFIG_TLS_REG_EMUL)

	ldr	r0, [pc, #(16 - 8)]		@ TLS stored at 0xffff0ff0
	mov	pc, lr

#else

	mrc	p15, 0, r0, c13, c0, 3		@ read TLS register
	mov	pc, lr

#endif

	.rep	5
	.word	0			@ pad up to __kuser_helper_version
	.endr

/*
 * Reference declaration:
 *
 *	extern unsigned int __kernel_helper_version;
 *
 * Definition and user space usage example:
 *
 *	#define __kernel_helper_version (*(unsigned int *)0xffff0ffc)
 *
 * User space may read this to determine the curent number of helpers
 * available.
 */

__kuser_helper_version:				@ 0xffff0ffc
	.word	((__kuser_helper_end - __kuser_helper_start) >> 5)

	.globl	__kuser_helper_end
__kuser_helper_end:


/*
 * Vector stubs.
 *
 * This code is copied to 0xffff0200 so we can use branches in the
 * vectors, rather than ldr's.  Note that this code must not
 * exceed 0x300 bytes.
 *
 * Common stub entry macro:
 *   Enter in IRQ mode, spsr = SVC/USR CPSR, lr = SVC/USR PC
 *
 * SP points to a minimal amount of processor-private memory, the address
 * of which is copied into r0 for the mode specific abort handler.
 */
	.macro	vector_stub, name, mode, correction=0
	.align	5

vector_\name:
	.if \correction
	sub	lr, lr, #\correction
	.endif

	@
	@ Save r0, lr_<exception> (parent PC) and spsr_<exception>
	@ (parent CPSR)
	@
	stmia	sp, {r0, lr}		@ save r0, lr
	mrs	lr, spsr
	str	lr, [sp, #8]		@ save spsr

	@
	@ Prepare for SVC32 mode.  IRQs remain disabled.
	@
	mrs	r0, cpsr
	eor	r0, r0, #(\mode ^ SVC_MODE)
	msr	spsr_cxsf, r0

	@
	@ the branch table must immediately follow this code
	@
	and	lr, lr, #0x0f
	mov	r0, sp
	ldr	lr, [pc, lr, lsl #2]
	movs	pc, lr			@ branch to handler in SVC mode
	.endm

	.globl	__stubs_start
__stubs_start:
/*
 * Interrupt dispatcher
 */
	vector_stub	irq, IRQ_MODE, 4

	.long	__irq_usr			@  0  (USR_26 / USR_32)
	.long	__irq_invalid			@  1  (FIQ_26 / FIQ_32)
	.long	__irq_invalid			@  2  (IRQ_26 / IRQ_32)
	.long	__irq_svc			@  3  (SVC_26 / SVC_32)
	.long	__irq_invalid			@  4
	.long	__irq_invalid			@  5
	.long	__irq_invalid			@  6
	.long	__irq_invalid			@  7
	.long	__irq_invalid			@  8
	.long	__irq_invalid			@  9
	.long	__irq_invalid			@  a
	.long	__irq_invalid			@  b
	.long	__irq_invalid			@  c
	.long	__irq_invalid			@  d
	.long	__irq_invalid			@  e
	.long	__irq_invalid			@  f

/*
 * Data abort dispatcher
 * Enter in ABT mode, spsr = USR CPSR, lr = USR PC
 */
	vector_stub	dabt, ABT_MODE, 8

	.long	__dabt_usr			@  0  (USR_26 / USR_32)
	.long	__dabt_invalid			@  1  (FIQ_26 / FIQ_32)
	.long	__dabt_invalid			@  2  (IRQ_26 / IRQ_32)
	.long	__dabt_svc			@  3  (SVC_26 / SVC_32)
	.long	__dabt_invalid			@  4
	.long	__dabt_invalid			@  5
	.long	__dabt_invalid			@  6
	.long	__dabt_invalid			@  7
	.long	__dabt_invalid			@  8
	.long	__dabt_invalid			@  9
	.long	__dabt_invalid			@  a
	.long	__dabt_invalid			@  b
	.long	__dabt_invalid			@  c
	.long	__dabt_invalid			@  d
	.long	__dabt_invalid			@  e
	.long	__dabt_invalid			@  f

/*
 * Prefetch abort dispatcher
 * Enter in ABT mode, spsr = USR CPSR, lr = USR PC
 */
	vector_stub	pabt, ABT_MODE, 4

	.long	__pabt_usr			@  0 (USR_26 / USR_32)
	.long	__pabt_invalid			@  1 (FIQ_26 / FIQ_32)
	.long	__pabt_invalid			@  2 (IRQ_26 / IRQ_32)
	.long	__pabt_svc			@  3 (SVC_26 / SVC_32)
	.long	__pabt_invalid			@  4
	.long	__pabt_invalid			@  5
	.long	__pabt_invalid			@  6
	.long	__pabt_invalid			@  7
	.long	__pabt_invalid			@  8
	.long	__pabt_invalid			@  9
	.long	__pabt_invalid			@  a
	.long	__pabt_invalid			@  b
	.long	__pabt_invalid			@  c
	.long	__pabt_invalid			@  d
	.long	__pabt_invalid			@  e
	.long	__pabt_invalid			@  f

/*
 * Undef instr entry dispatcher
 * Enter in UND mode, spsr = SVC/USR CPSR, lr = SVC/USR PC
 */
	vector_stub	und, UND_MODE

	.long	__und_usr			@  0 (USR_26 / USR_32)
	.long	__und_invalid			@  1 (FIQ_26 / FIQ_32)
	.long	__und_invalid			@  2 (IRQ_26 / IRQ_32)
	.long	__und_svc			@  3 (SVC_26 / SVC_32)
	.long	__und_invalid			@  4
	.long	__und_invalid			@  5
	.long	__und_invalid			@  6
	.long	__und_invalid			@  7
	.long	__und_invalid			@  8
	.long	__und_invalid			@  9
	.long	__und_invalid			@  a
	.long	__und_invalid			@  b
	.long	__und_invalid			@  c
	.long	__und_invalid			@  d
	.long	__und_invalid			@  e
	.long	__und_invalid			@  f

	.align	5

/*=============================================================================
 * Undefined FIQs
 *-----------------------------------------------------------------------------
 * Enter in FIQ mode, spsr = ANY CPSR, lr = ANY PC
 * MUST PRESERVE SVC SPSR, but need to switch to SVC mode to show our msg.
 * Basically to switch modes, we *HAVE* to clobber one register...  brain
 * damage alert!  I don't think that we can execute any code in here in any
 * other mode than FIQ...  Ok you can switch to another mode, but you can't
 * get out of that mode without clobbering one register.
 */
vector_fiq:
	disable_fiq
	subs	pc, lr, #4

/*=============================================================================
 * Address exception handler
 *-----------------------------------------------------------------------------
 * These aren't too critical.
 * (they're not supposed to happen, and won't happen in 32-bit data mode).
 */

vector_addrexcptn:
	b	vector_addrexcptn

/*
 * We group all the following data together to optimise
 * for CPUs with separate I & D caches.
 */
	.align	5

.LCvswi:
	.word	vector_swi

	.globl	__stubs_end
__stubs_end:

	.equ	stubs_offset, __vectors_start + 0x200 - __stubs_start

	.globl	__vectors_start
__vectors_start:
	swi	SYS_ERROR0
	b	vector_und + stubs_offset
	ldr	pc, .LCvswi + stubs_offset
	b	vector_pabt + stubs_offset
	b	vector_dabt + stubs_offset
	b	vector_addrexcptn + stubs_offset
	b	vector_irq + stubs_offset
	b	vector_fiq + stubs_offset

	.globl	__vectors_end
__vectors_end:

	.data

	.globl	cr_alignment
	.globl	cr_no_alignment
cr_alignment:
	.space	4
cr_no_alignment:
	.space	4
