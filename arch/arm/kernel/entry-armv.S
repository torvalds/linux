/*
 *  linux/arch/arm/kernel/entry-armv.S
 *
 *  Copyright (C) 1996,1997,1998 Russell King.
 *  ARM700 fix by Matthew Godbolt (linux-user@willothewisp.demon.co.uk)
 *  nommu support by Hyok S. Choi (hyok.choi@samsung.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Low-level vector interface routines
 *
 *  Note:  there is a StrongARM bug in the STMIA rn, {regs}^ instruction
 *  that causes it to save wrong values...  Be aware!
 */

#include <asm/memory.h>
#include <asm/glue.h>
#include <asm/vfpmacros.h>
#include <mach/entry-macro.S>
#include <asm/thread_notify.h>
#include <asm/unwind.h>
#include <asm/unistd.h>
#include <asm/tls.h>

#include "entry-header.S"
#include <asm/entry-macro-multi.S>

/*
 * Interrupt handling.  Preserves r7, r8, r9
 */
	.macro	irq_handler
#ifdef CONFIG_MULTI_IRQ_HANDLER
	ldr	r5, =handle_arch_irq
	mov	r0, sp
	ldr	r5, [r5]
	adr	lr, BSYM(9997f)
	teq	r5, #0
	movne	pc, r5
#endif
	arch_irq_handler_default
9997:
	.endm

#ifdef CONFIG_KPROBES
	.section	.kprobes.text,"ax",%progbits
#else
	.text
#endif

/*
 * Invalid mode handlers
 */
	.macro	inv_entry, reason
	sub	sp, sp, #S_FRAME_SIZE
 ARM(	stmib	sp, {r1 - lr}		)
 THUMB(	stmia	sp, {r0 - r12}		)
 THUMB(	str	sp, [sp, #S_SP]		)
 THUMB(	str	lr, [sp, #S_LR]		)
	mov	r1, #\reason
	.endm

__pabt_invalid:
	inv_entry BAD_PREFETCH
	b	common_invalid
ENDPROC(__pabt_invalid)

__dabt_invalid:
	inv_entry BAD_DATA
	b	common_invalid
ENDPROC(__dabt_invalid)

__irq_invalid:
	inv_entry BAD_IRQ
	b	common_invalid
ENDPROC(__irq_invalid)

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
	b	bad_mode
ENDPROC(__und_invalid)

/*
 * SVC mode handlers
 */

#if defined(CONFIG_AEABI) && (__LINUX_ARM_ARCH__ >= 5)
#define SPFIX(code...) code
#else
#define SPFIX(code...)
#endif

	.macro	svc_entry, stack_hole=0
 UNWIND(.fnstart		)
 UNWIND(.save {r0 - pc}		)
	sub	sp, sp, #(S_FRAME_SIZE + \stack_hole - 4)
#ifdef CONFIG_THUMB2_KERNEL
 SPFIX(	str	r0, [sp]	)	@ temporarily saved
 SPFIX(	mov	r0, sp		)
 SPFIX(	tst	r0, #4		)	@ test original stack alignment
 SPFIX(	ldr	r0, [sp]	)	@ restored
#else
 SPFIX(	tst	sp, #4		)
#endif
 SPFIX(	subeq	sp, sp, #4	)
	stmia	sp, {r1 - r12}

	ldmia	r0, {r1 - r3}
	add	r5, sp, #S_SP - 4	@ here for interlock avoidance
	mov	r4, #-1			@  ""  ""      ""       ""
	add	r0, sp, #(S_FRAME_SIZE + \stack_hole - 4)
 SPFIX(	addeq	r0, r0, #4	)
	str	r1, [sp, #-4]!		@ save the "real" r0 copied
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
#ifdef MULTI_DABORT
	ldr	r4, .LCprocfns
	mov	lr, pc
	ldr	pc, [r4, #PROCESSOR_DABT_FUNC]
#else
	bl	CPU_DABORT_HANDLER
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
	disable_irq_notrace

	@
	@ restore SPSR and restart the instruction
	@
	ldr	r2, [sp, #S_PSR]
	svc_exit r2				@ return from exception
 UNWIND(.fnend		)
ENDPROC(__dabt_svc)

	.align	5
__irq_svc:
	svc_entry

#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off
#endif
#ifdef CONFIG_PREEMPT
	get_thread_info tsk
	ldr	r8, [tsk, #TI_PREEMPT]		@ get preempt count
	add	r7, r8, #1			@ increment it
	str	r7, [tsk, #TI_PREEMPT]
#endif

	irq_handler
#ifdef CONFIG_PREEMPT
	str	r8, [tsk, #TI_PREEMPT]		@ restore preempt count
	ldr	r0, [tsk, #TI_FLAGS]		@ get flags
	teq	r8, #0				@ if preempt count != 0
	movne	r0, #0				@ force flags to 0
	tst	r0, #_TIF_NEED_RESCHED
	blne	svc_preempt
#endif
	ldr	r4, [sp, #S_PSR]		@ irqs are already disabled
#ifdef CONFIG_TRACE_IRQFLAGS
	tst	r4, #PSR_I_BIT
	bleq	trace_hardirqs_on
#endif
	svc_exit r4				@ return from exception
 UNWIND(.fnend		)
ENDPROC(__irq_svc)

	.ltorg

#ifdef CONFIG_PREEMPT
svc_preempt:
	mov	r8, lr
1:	bl	preempt_schedule_irq		@ irq en/disable is done inside
	ldr	r0, [tsk, #TI_FLAGS]		@ get new tasks TI_FLAGS
	tst	r0, #_TIF_NEED_RESCHED
	moveq	pc, r8				@ go again
	b	1b
#endif

	.align	5
__und_svc:
#ifdef CONFIG_KPROBES
	@ If a kprobe is about to simulate a "stmdb sp..." instruction,
	@ it obviously needs free stack space which then will belong to
	@ the saved context.
	svc_entry 64
#else
	svc_entry
#endif

	@
	@ call emulation code, which returns using r9 if it has emulated
	@ the instruction, or the more conventional lr if we are to treat
	@ this as a real undefined instruction
	@
	@  r0 - instruction
	@
#ifndef	CONFIG_THUMB2_KERNEL
	ldr	r0, [r2, #-4]
#else
	ldrh	r0, [r2, #-2]			@ Thumb instruction at LR - 2
	and	r9, r0, #0xf800
	cmp	r9, #0xe800			@ 32-bit instruction if xx >= 0
	ldrhhs	r9, [r2]			@ bottom 16 bits
	orrhs	r0, r9, r0, lsl #16
#endif
	adr	r9, BSYM(1f)
	bl	call_fpe

	mov	r0, sp				@ struct pt_regs *regs
	bl	do_undefinstr

	@
	@ IRQs off again before pulling preserved data off the stack
	@
1:	disable_irq_notrace

	@
	@ restore SPSR and restart the instruction
	@
	ldr	r2, [sp, #S_PSR]		@ Get SVC cpsr
	svc_exit r2				@ return from exception
 UNWIND(.fnend		)
ENDPROC(__und_svc)

	.align	5
__pabt_svc:
	svc_entry

	@
	@ re-enable interrupts if appropriate
	@
	mrs	r9, cpsr
	tst	r3, #PSR_I_BIT
	biceq	r9, r9, #PSR_I_BIT

	mov	r0, r2			@ pass address of aborted instruction.
#ifdef MULTI_PABORT
	ldr	r4, .LCprocfns
	mov	lr, pc
	ldr	pc, [r4, #PROCESSOR_PABT_FUNC]
#else
	bl	CPU_PABORT_HANDLER
#endif
	msr	cpsr_c, r9			@ Maybe enable interrupts
	mov	r2, sp				@ regs
	bl	do_PrefetchAbort		@ call abort handler

	@
	@ IRQs off again before pulling preserved data off the stack
	@
	disable_irq_notrace

	@
	@ restore SPSR and restart the instruction
	@
	ldr	r2, [sp, #S_PSR]
	svc_exit r2				@ return from exception
 UNWIND(.fnend		)
ENDPROC(__pabt_svc)

	.align	5
.LCcralign:
	.word	cr_alignment
#ifdef MULTI_DABORT
.LCprocfns:
	.word	processor
#endif
.LCfp:
	.word	fp_enter

/*
 * User mode handlers
 *
 * EABI note: sp_svc is always 64-bit aligned here, so should S_FRAME_SIZE
 */

#if defined(CONFIG_AEABI) && (__LINUX_ARM_ARCH__ >= 5) && (S_FRAME_SIZE & 7)
#error "sizeof(struct pt_regs) must be a multiple of 8"
#endif

	.macro	usr_entry
 UNWIND(.fnstart	)
 UNWIND(.cantunwind	)	@ don't unwind the user space
	sub	sp, sp, #S_FRAME_SIZE
 ARM(	stmib	sp, {r1 - r12}	)
 THUMB(	stmia	sp, {r0 - r12}	)

	ldmia	r0, {r1 - r3}
	add	r0, sp, #S_PC		@ here for interlock avoidance
	mov	r4, #-1			@  ""  ""     ""        ""

	str	r1, [sp]		@ save the "real" r0 copied
					@ from the exception stack

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
 ARM(	stmdb	r0, {sp, lr}^			)
 THUMB(	store_user_sp_lr r0, r1, S_SP - S_PC	)

	@
	@ Enable the alignment trap while in kernel mode
	@
	alignment_trap r0

	@
	@ Clear FP to mark the first stack frame
	@
	zero_fp
	.endm

	.macro	kuser_cmpxchg_check
#if __LINUX_ARM_ARCH__ < 6 && !defined(CONFIG_NEEDS_SYSCALL_FOR_CMPXCHG)
#ifndef CONFIG_MMU
#warning "NPTL on non MMU needs fixing"
#else
	@ Make sure our user space atomic helper is restarted
	@ if it was interrupted in a critical region.  Here we
	@ perform a quick test inline since it should be false
	@ 99.9999% of the time.  The rest is done out of line.
	cmp	r2, #TASK_SIZE
	blhs	kuser_cmpxchg_fixup
#endif
#endif
	.endm

	.align	5
__dabt_usr:
	usr_entry
	kuser_cmpxchg_check

	@
	@ Call the processor-specific abort handler:
	@
	@  r2 - aborted context pc
	@  r3 - aborted context cpsr
	@
	@ The abort handler must return the aborted address in r0, and
	@ the fault status register in r1.
	@
#ifdef MULTI_DABORT
	ldr	r4, .LCprocfns
	mov	lr, pc
	ldr	pc, [r4, #PROCESSOR_DABT_FUNC]
#else
	bl	CPU_DABORT_HANDLER
#endif

	@
	@ IRQs on, then call the main handler
	@
	enable_irq
	mov	r2, sp
	adr	lr, BSYM(ret_from_exception)
	b	do_DataAbort
 UNWIND(.fnend		)
ENDPROC(__dabt_usr)

	.align	5
__irq_usr:
	usr_entry
	kuser_cmpxchg_check

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
 ARM(	strne	r0, [r0, -r0]	)
 THUMB(	movne	r0, #0		)
 THUMB(	strne	r0, [r0]	)
#endif

	mov	why, #0
	b	ret_to_user
 UNWIND(.fnend		)
ENDPROC(__irq_usr)

	.ltorg

	.align	5
__und_usr:
	usr_entry

	@
	@ fall through to the emulation code, which returns using r9 if
	@ it has emulated the instruction, or the more conventional lr
	@ if we are to treat this as a real undefined instruction
	@
	@  r0 - instruction
	@
	adr	r9, BSYM(ret_from_exception)
	adr	lr, BSYM(__und_usr_unknown)
	tst	r3, #PSR_T_BIT			@ Thumb mode?
	itet	eq				@ explicit IT needed for the 1f label
	subeq	r4, r2, #4			@ ARM instr at LR - 4
	subne	r4, r2, #2			@ Thumb instr at LR - 2
1:	ldreqt	r0, [r4]
#ifdef CONFIG_CPU_ENDIAN_BE8
	reveq	r0, r0				@ little endian instruction
#endif
	beq	call_fpe
	@ Thumb instruction
#if __LINUX_ARM_ARCH__ >= 7
2:
 ARM(	ldrht	r5, [r4], #2	)
 THUMB(	ldrht	r5, [r4]	)
 THUMB(	add	r4, r4, #2	)
	and	r0, r5, #0xf800			@ mask bits 111x x... .... ....
	cmp	r0, #0xe800			@ 32bit instruction if xx != 0
	blo	__und_usr_unknown
3:	ldrht	r0, [r4]
	add	r2, r2, #2			@ r2 is PC + 2, make it PC + 4
	orr	r0, r0, r5, lsl #16
#else
	b	__und_usr_unknown
#endif
 UNWIND(.fnend		)
ENDPROC(__und_usr)

	@
	@ fallthrough to call_fpe
	@

/*
 * The out of line fixup for the ldrt above.
 */
	.pushsection .fixup, "ax"
4:	mov	pc, r9
	.popsection
	.pushsection __ex_table,"a"
	.long	1b, 4b
#if __LINUX_ARM_ARCH__ >= 7
	.long	2b, 4b
	.long	3b, 4b
#endif
	.popsection

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
 * NEON is a special case that has to be handled here. Not all
 * NEON instructions are co-processor instructions, so we have
 * to make a special case of checking for them. Plus, there's
 * five groups of them, so we have a table of mask/opcode pairs
 * to check against, and if any match then we branch off into the
 * NEON handler code.
 *
 * Emulators may wish to make use of the following registers:
 *  r0  = instruction opcode.
 *  r2  = PC+4
 *  r9  = normal "successful" return address
 *  r10 = this threads thread_info structure.
 *  lr  = unrecognised instruction return address
 */
	@
	@ Fall-through from Thumb-2 __und_usr
	@
#ifdef CONFIG_NEON
	adr	r6, .LCneon_thumb_opcodes
	b	2f
#endif
call_fpe:
#ifdef CONFIG_NEON
	adr	r6, .LCneon_arm_opcodes
2:
	ldr	r7, [r6], #4			@ mask value
	cmp	r7, #0				@ end mask?
	beq	1f
	and	r8, r0, r7
	ldr	r7, [r6], #4			@ opcode bits matching in mask
	cmp	r8, r7				@ NEON instruction?
	bne	2b
	get_thread_info r10
	mov	r7, #1
	strb	r7, [r10, #TI_USED_CP + 10]	@ mark CP#10 as used
	strb	r7, [r10, #TI_USED_CP + 11]	@ mark CP#11 as used
	b	do_vfp				@ let VFP handler handle this
1:
#endif
	tst	r0, #0x08000000			@ only CDP/CPRT/LDC/STC have bit 27
	tstne	r0, #0x04000000			@ bit 26 set on both ARM and Thumb-2
#if defined(CONFIG_CPU_ARM610) || defined(CONFIG_CPU_ARM710)
	and	r8, r0, #0x0f000000		@ mask out op-code bits
	teqne	r8, #0x0f000000			@ SWI (ARM6/7 bug)?
#endif
	moveq	pc, lr
	get_thread_info r10			@ get current thread
	and	r8, r0, #0x00000f00		@ mask out CP number
 THUMB(	lsr	r8, r8, #8		)
	mov	r7, #1
	add	r6, r10, #TI_USED_CP
 ARM(	strb	r7, [r6, r8, lsr #8]	)	@ set appropriate used_cp[]
 THUMB(	strb	r7, [r6, r8]		)	@ set appropriate used_cp[]
#ifdef CONFIG_IWMMXT
	@ Test if we need to give access to iWMMXt coprocessors
	ldr	r5, [r10, #TI_FLAGS]
	rsbs	r7, r8, #(1 << 8)		@ CP 0 or 1 only
	movcss	r7, r5, lsr #(TIF_USING_IWMMXT + 1)
	bcs	iwmmxt_task_enable
#endif
 ARM(	add	pc, pc, r8, lsr #6	)
 THUMB(	lsl	r8, r8, #2		)
 THUMB(	add	pc, r8			)
	nop

	movw_pc	lr				@ CP#0
	W(b)	do_fpe				@ CP#1 (FPE)
	W(b)	do_fpe				@ CP#2 (FPE)
	movw_pc	lr				@ CP#3
#ifdef CONFIG_CRUNCH
	b	crunch_task_enable		@ CP#4 (MaverickCrunch)
	b	crunch_task_enable		@ CP#5 (MaverickCrunch)
	b	crunch_task_enable		@ CP#6 (MaverickCrunch)
#else
	movw_pc	lr				@ CP#4
	movw_pc	lr				@ CP#5
	movw_pc	lr				@ CP#6
#endif
	movw_pc	lr				@ CP#7
	movw_pc	lr				@ CP#8
	movw_pc	lr				@ CP#9
#ifdef CONFIG_VFP
	W(b)	do_vfp				@ CP#10 (VFP)
	W(b)	do_vfp				@ CP#11 (VFP)
#else
	movw_pc	lr				@ CP#10 (VFP)
	movw_pc	lr				@ CP#11 (VFP)
#endif
	movw_pc	lr				@ CP#12
	movw_pc	lr				@ CP#13
	movw_pc	lr				@ CP#14 (Debug)
	movw_pc	lr				@ CP#15 (Control)

#ifdef CONFIG_NEON
	.align	6

.LCneon_arm_opcodes:
	.word	0xfe000000			@ mask
	.word	0xf2000000			@ opcode

	.word	0xff100000			@ mask
	.word	0xf4000000			@ opcode

	.word	0x00000000			@ mask
	.word	0x00000000			@ opcode

.LCneon_thumb_opcodes:
	.word	0xef000000			@ mask
	.word	0xef000000			@ opcode

	.word	0xff100000			@ mask
	.word	0xf9000000			@ opcode

	.word	0x00000000			@ mask
	.word	0x00000000			@ opcode
#endif

do_fpe:
	enable_irq
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

	.pushsection .data
ENTRY(fp_enter)
	.word	no_fp
	.popsection

ENTRY(no_fp)
	mov	pc, lr
ENDPROC(no_fp)

__und_usr_unknown:
	enable_irq
	mov	r0, sp
	adr	lr, BSYM(ret_from_exception)
	b	do_undefinstr
ENDPROC(__und_usr_unknown)

	.align	5
__pabt_usr:
	usr_entry

	mov	r0, r2			@ pass address of aborted instruction.
#ifdef MULTI_PABORT
	ldr	r4, .LCprocfns
	mov	lr, pc
	ldr	pc, [r4, #PROCESSOR_PABT_FUNC]
#else
	bl	CPU_PABORT_HANDLER
#endif
	enable_irq				@ Enable interrupts
	mov	r2, sp				@ regs
	bl	do_PrefetchAbort		@ call abort handler
 UNWIND(.fnend		)
	/* fall through */
/*
 * This is the return code to user mode for abort handlers
 */
ENTRY(ret_from_exception)
 UNWIND(.fnstart	)
 UNWIND(.cantunwind	)
	get_thread_info tsk
	mov	why, #0
	b	ret_to_user
 UNWIND(.fnend		)
ENDPROC(__pabt_usr)
ENDPROC(ret_from_exception)

/*
 * Register switch for ARMv3 and ARMv4 processors
 * r0 = previous task_struct, r1 = previous thread_info, r2 = next thread_info
 * previous and next are guaranteed not to be the same.
 */
ENTRY(__switch_to)
 UNWIND(.fnstart	)
 UNWIND(.cantunwind	)
	add	ip, r1, #TI_CPU_SAVE
	ldr	r3, [r2, #TI_TP_VALUE]
 ARM(	stmia	ip!, {r4 - sl, fp, sp, lr} )	@ Store most regs on stack
 THUMB(	stmia	ip!, {r4 - sl, fp}	   )	@ Store most regs on stack
 THUMB(	str	sp, [ip], #4		   )
 THUMB(	str	lr, [ip], #4		   )
#ifdef CONFIG_CPU_USE_DOMAINS
	ldr	r6, [r2, #TI_CPU_DOMAIN]
#endif
	set_tls	r3, r4, r5
#if defined(CONFIG_CC_STACKPROTECTOR) && !defined(CONFIG_SMP)
	ldr	r7, [r2, #TI_TASK]
	ldr	r8, =__stack_chk_guard
	ldr	r7, [r7, #TSK_STACK_CANARY]
#endif
#ifdef CONFIG_CPU_USE_DOMAINS
	mcr	p15, 0, r6, c3, c0, 0		@ Set domain register
#endif
	mov	r5, r0
	add	r4, r2, #TI_CPU_SAVE
	ldr	r0, =thread_notify_head
	mov	r1, #THREAD_NOTIFY_SWITCH
	bl	atomic_notifier_call_chain
#if defined(CONFIG_CC_STACKPROTECTOR) && !defined(CONFIG_SMP)
	str	r7, [r8]
#endif
 THUMB(	mov	ip, r4			   )
	mov	r0, r5
 ARM(	ldmia	r4, {r4 - sl, fp, sp, pc}  )	@ Load all regs saved previously
 THUMB(	ldmia	ip!, {r4 - sl, fp}	   )	@ Load all regs saved previously
 THUMB(	ldr	sp, [ip], #4		   )
 THUMB(	ldr	pc, [ip]		   )
 UNWIND(.fnend		)
ENDPROC(__switch_to)

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
 THUMB(	.arm	)

	.macro	usr_ret, reg
#ifdef CONFIG_ARM_THUMB
	bx	\reg
#else
	mov	pc, \reg
#endif
	.endm

	.align	5
	.globl	__kuser_helper_start
__kuser_helper_start:

/*
 * Reference prototype:
 *
 *	void __kernel_memory_barrier(void)
 *
 * Input:
 *
 *	lr = return address
 *
 * Output:
 *
 *	none
 *
 * Clobbered:
 *
 *	none
 *
 * Definition and user space usage example:
 *
 *	typedef void (__kernel_dmb_t)(void);
 *	#define __kernel_dmb (*(__kernel_dmb_t *)0xffff0fa0)
 *
 * Apply any needed memory barrier to preserve consistency with data modified
 * manually and __kuser_cmpxchg usage.
 *
 * This could be used as follows:
 *
 * #define __kernel_dmb() \
 *         asm volatile ( "mov r0, #0xffff0fff; mov lr, pc; sub pc, r0, #95" \
 *	        : : : "r0", "lr","cc" )
 */

__kuser_memory_barrier:				@ 0xffff0fa0
	smp_dmb	arm
	usr_ret	lr

	.align	5

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
 * Notes:
 *
 *    - This routine already includes memory barriers as needed.
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
	stmfd	sp!, {r7, lr}
	ldr	r7, =1f			@ it's 20 bits
	swi	__ARM_NR_cmpxchg
	ldmfd	sp!, {r7, pc}
1:	.word	__ARM_NR_cmpxchg

#elif __LINUX_ARM_ARCH__ < 6

#ifdef CONFIG_MMU

	/*
	 * The only thing that can break atomicity in this cmpxchg
	 * implementation is either an IRQ or a data abort exception
	 * causing another process/thread to be scheduled in the middle
	 * of the critical sequence.  To prevent this, code is added to
	 * the IRQ and data abort exception handlers to set the pc back
	 * to the beginning of the critical section if it is found to be
	 * within that critical section (see kuser_cmpxchg_fixup).
	 */
1:	ldr	r3, [r2]			@ load current val
	subs	r3, r3, r0			@ compare with oldval
2:	streq	r1, [r2]			@ store newval if eq
	rsbs	r0, r3, #0			@ set return val and C flag
	usr_ret	lr

	.text
kuser_cmpxchg_fixup:
	@ Called from kuser_cmpxchg_check macro.
	@ r2 = address of interrupted insn (must be preserved).
	@ sp = saved regs. r7 and r8 are clobbered.
	@ 1b = first critical insn, 2b = last critical insn.
	@ If r2 >= 1b and r2 <= 2b then saved pc_usr is set to 1b.
	mov	r7, #0xffff0fff
	sub	r7, r7, #(0xffff0fff - (0xffff0fc0 + (1b - __kuser_cmpxchg)))
	subs	r8, r2, r7
	rsbcss	r8, r8, #(2b - 1b)
	strcs	r7, [sp, #S_PC]
	mov	pc, lr
	.previous

#else
#warning "NPTL on non MMU needs fixing"
	mov	r0, #-1
	adds	r0, r0, #0
	usr_ret	lr
#endif

#else

	smp_dmb	arm
1:	ldrex	r3, [r2]
	subs	r3, r3, r0
	strexeq	r3, r1, [r2]
	teqeq	r3, #1
	beq	1b
	rsbs	r0, r3, #0
	/* beware -- each __kuser slot must be 8 instructions max */
	ALT_SMP(b	__kuser_memory_barrier)
	ALT_UP(usr_ret	lr)

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
 *	none
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
	ldr	r0, [pc, #(16 - 8)]	@ read TLS, set in kuser_get_tls_init
	usr_ret	lr
	mrc	p15, 0, r0, c13, c0, 3	@ 0xffff0fe8 hardware TLS code
	.rep	4
	.word	0			@ 0xffff0ff0 software TLS value, then
	.endr				@ pad up to __kuser_helper_version

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

 THUMB(	.thumb	)

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
	eor	r0, r0, #(\mode ^ SVC_MODE | PSR_ISETSTATE)
	msr	spsr_cxsf, r0

	@
	@ the branch table must immediately follow this code
	@
	and	lr, lr, #0x0f
 THUMB(	adr	r0, 1f			)
 THUMB(	ldr	lr, [r0, lr, lsl #2]	)
	mov	r0, sp
 ARM(	ldr	lr, [pc, lr, lsl #2]	)
	movs	pc, lr			@ branch to handler in SVC mode
ENDPROC(vector_\name)

	.align	2
	@ handler addresses follow this label
1:
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
 ARM(	swi	SYS_ERROR0	)
 THUMB(	svc	#0		)
 THUMB(	nop			)
	W(b)	vector_und + stubs_offset
	W(ldr)	pc, .LCvswi + stubs_offset
	W(b)	vector_pabt + stubs_offset
	W(b)	vector_dabt + stubs_offset
	W(b)	vector_addrexcptn + stubs_offset
	W(b)	vector_irq + stubs_offset
	W(b)	vector_fiq + stubs_offset

	.globl	__vectors_end
__vectors_end:

	.data

	.globl	cr_alignment
	.globl	cr_no_alignment
cr_alignment:
	.space	4
cr_no_alignment:
	.space	4

#ifdef CONFIG_MULTI_IRQ_HANDLER
	.globl	handle_arch_irq
handle_arch_irq:
	.space	4
#endif
