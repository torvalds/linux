/*
 * linux/arch/unicore32/kernel/entry.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Low-level vector interface routines
 */
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/errno.h>
#include <asm/thread_info.h>
#include <asm/memory.h>
#include <asm/unistd.h>
#include <generated/asm-offsets.h>
#include "debug-macro.S"

@
@ Most of the stack format comes from struct pt_regs, but with
@ the addition of 8 bytes for storing syscall args 5 and 6.
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

	.macro	alignment_trap, rtemp
#ifdef CONFIG_ALIGNMENT_TRAP
	ldw	\rtemp, .LCcralign
	ldw	\rtemp, [\rtemp]
	movc	p0.c1, \rtemp, #0
#endif
	.endm

	.macro	load_user_sp_lr, rd, rtemp, offset = 0
	mov	\rtemp, asr
	xor	\rtemp, \rtemp, #(PRIV_MODE ^ SUSR_MODE)
	mov.a	asr, \rtemp			@ switch to the SUSR mode

	ldw	sp, [\rd+], #\offset		@ load sp_user
	ldw	lr, [\rd+], #\offset + 4	@ load lr_user

	xor	\rtemp, \rtemp, #(PRIV_MODE ^ SUSR_MODE)
	mov.a	asr, \rtemp			@ switch back to the PRIV mode
	.endm

	.macro	priv_exit, rpsr
	mov.a	bsr, \rpsr
	ldm.w	(r0 - r15), [sp]+
	ldm.b	(r16 - pc), [sp]+		@ load r0 - pc, asr
	.endm

	.macro	restore_user_regs, fast = 0, offset = 0
	ldw	r1, [sp+], #\offset + S_PSR	@ get calling asr
	ldw	lr, [sp+], #\offset + S_PC	@ get pc
	mov.a	bsr, r1				@ save in bsr_priv
	.if	\fast
	add	sp, sp, #\offset + S_R1		@ r0 is syscall return value
	ldm.w	(r1 - r15), [sp]+		@ get calling r1 - r15
	ldur	(r16 - lr), [sp]+		@ get calling r16 - lr
	.else
	ldm.w	(r0 - r15), [sp]+		@ get calling r0 - r15
	ldur	(r16 - lr), [sp]+		@ get calling r16 - lr
	.endif
	nop
	add	sp, sp, #S_FRAME_SIZE - S_R16
	mov.a	pc, lr				@ return
						@ and move bsr_priv into asr
	.endm

	.macro	get_thread_info, rd
	mov	\rd, sp >> #13
	mov	\rd, \rd << #13
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldw	\base, =(PKUNITY_INTC_BASE)
	ldw	\irqstat, [\base+], #0xC	@ INTC_ICIP
	ldw	\tmp,	  [\base+], #0x4	@ INTC_ICMR
	and.a	\irqstat, \irqstat, \tmp
	beq	1001f
	cntlz	\irqnr, \irqstat
	rsub	\irqnr, \irqnr, #31
1001:	/* EQ will be set if no irqs pending */
	.endm

#ifdef CONFIG_DEBUG_LL
	.macro	printreg, reg, temp
		adr	\temp, 901f
		stm	(r0-r3), [\temp]+
		stw	lr, [\temp+], #0x10
		mov	r0, \reg
		b.l	printhex8
		mov	r0, #':'
		b.l	printch
		mov	r0, pc
		b.l	printhex8
		adr	r0, 902f
		b.l	printascii
		adr	\temp, 901f
		ldm	(r0-r3), [\temp]+
		ldw	lr, [\temp+], #0x10
		b	903f
901:	.word	0, 0, 0, 0, 0	@ r0-r3, lr
902:	.asciz	": epip4d\n"
	.align
903:
	.endm
#endif

/*
 * These are the registers used in the syscall handler, and allow us to
 * have in theory up to 7 arguments to a function - r0 to r6.
 *
 * Note that tbl == why is intentional.
 *
 * We must set at least "tsk" and "why" when calling ret_with_reschedule.
 */
scno	.req	r21		@ syscall number
tbl	.req	r22		@ syscall table pointer
why	.req	r22		@ Linux syscall (!= 0)
tsk	.req	r23		@ current thread_info

/*
 * Interrupt handling.  Preserves r17, r18, r19
 */
	.macro	intr_handler
1:	get_irqnr_and_base r0, r6, r5, lr
	beq	2f
	mov	r1, sp
	@
	@ routine called with r0 = irq number, r1 = struct pt_regs *
	@
	adr	lr, 1b
	b	asm_do_IRQ
2:
	.endm

/*
 * PRIV mode handlers
 */
	.macro	priv_entry
	sub	sp, sp, #(S_FRAME_SIZE - 4)
	stm	(r1 - r15), [sp]+
	add	r5, sp, #S_R15
	stm	(r16 - r28), [r5]+

	ldm	(r1 - r3), [r0]+
	add	r5, sp, #S_SP - 4	@ here for interlock avoidance
	mov	r4, #-1			@  ""  ""      ""       ""
	add	r0, sp, #(S_FRAME_SIZE - 4)
	stw.w	r1, [sp+], #-4		@ save the "real" r0 copied
					@ from the exception stack

	mov	r1, lr

	@
	@ We are now ready to fill in the remaining blanks on the stack:
	@
	@  r0 - sp_priv
	@  r1 - lr_priv
	@  r2 - lr_<exception>, already fixed up for correct return/restart
	@  r3 - bsr_<exception>
	@  r4 - orig_r0 (see pt_regs definition in ptrace.h)
	@
	stm	(r0 - r4), [r5]+
	.endm

/*
 * User mode handlers
 *
 */
	.macro	user_entry
	sub	sp, sp, #S_FRAME_SIZE
	stm	(r1 - r15), [sp+]
	add	r4, sp, #S_R16
	stm	(r16 - r28), [r4]+

	ldm	(r1 - r3), [r0]+
	add	r0, sp, #S_PC		@ here for interlock avoidance
	mov	r4, #-1			@  ""  ""     ""        ""

	stw	r1, [sp]		@ save the "real" r0 copied
					@ from the exception stack

	@
	@ We are now ready to fill in the remaining blanks on the stack:
	@
	@  r2 - lr_<exception>, already fixed up for correct return/restart
	@  r3 - bsr_<exception>
	@  r4 - orig_r0 (see pt_regs definition in ptrace.h)
	@
	@ Also, separately save sp_user and lr_user
	@
	stm	(r2 - r4), [r0]+
	stur	(sp, lr), [r0-]

	@
	@ Enable the alignment trap while in kernel mode
	@
	alignment_trap r0

	@
	@ Clear FP to mark the first stack frame
	@
	zero_fp
	.endm

	.text

@
@ __invalid - generic code for failed exception
@			(re-entrant version of handlers)
@
__invalid:
	sub	sp, sp, #S_FRAME_SIZE
	stm	(r1 - r15), [sp+]
	add	r1, sp, #S_R16
	stm	(r16 - r28, sp, lr), [r1]+

	zero_fp

	ldm	(r4 - r6), [r0]+
	add	r0, sp, #S_PC		@ here for interlock avoidance
	mov	r7, #-1			@  ""   ""    ""        ""
	stw	r4, [sp]		@ save preserved r0
	stm	(r5 - r7), [r0]+	@ lr_<exception>,
					@ asr_<exception>, "old_r0"

	mov	r0, sp
	mov	r1, asr
	b	bad_mode
ENDPROC(__invalid)

	.align	5
__dabt_priv:
	priv_entry

	@
	@ get ready to re-enable interrupts if appropriate
	@
	mov	r17, asr
	cand.a	r3, #PSR_I_BIT
	bne	1f
	andn	r17, r17, #PSR_I_BIT
1:

	@
	@ Call the processor-specific abort handler:
	@
	@  r2 - aborted context pc
	@  r3 - aborted context asr
	@
	@ The abort handler must return the aborted address in r0, and
	@ the fault status register in r1.
	@
	movc	r1, p0.c3, #0		@ get FSR
	movc	r0, p0.c4, #0		@ get FAR

	@
	@ set desired INTR state, then call main handler
	@
	mov.a	asr, r17
	mov	r2, sp
	b.l	do_DataAbort

	@
	@ INTRs off again before pulling preserved data off the stack
	@
	disable_irq r0

	@
	@ restore BSR and restart the instruction
	@
	ldw	r2, [sp+], #S_PSR
	priv_exit r2				@ return from exception
ENDPROC(__dabt_priv)

	.align	5
__intr_priv:
	priv_entry

	intr_handler

	mov	r0, #0				@ epip4d
	movc	p0.c5, r0, #14
	nop; nop; nop; nop; nop; nop; nop; nop

	ldw	r4, [sp+], #S_PSR		@ irqs are already disabled

	priv_exit r4				@ return from exception
ENDPROC(__intr_priv)

	.ltorg

	.align	5
__extn_priv:
	priv_entry

	mov	r0, sp				@ struct pt_regs *regs
	mov	r1, asr
	b	bad_mode			@ not supported
ENDPROC(__extn_priv)

	.align	5
__pabt_priv:
	priv_entry

	@
	@ re-enable interrupts if appropriate
	@
	mov	r17, asr
	cand.a	r3, #PSR_I_BIT
	bne	1f
	andn	r17, r17, #PSR_I_BIT
1:

	@
	@ set args, then call main handler
	@
	@  r0 - address of faulting instruction
	@  r1 - pointer to registers on stack
	@
	mov	r0, r2			@ pass address of aborted instruction
	mov	r1, #5
	mov.a	asr, r17
	mov	r2, sp			@ regs
	b.l	do_PrefetchAbort	@ call abort handler

	@
	@ INTRs off again before pulling preserved data off the stack
	@
	disable_irq r0

	@
	@ restore BSR and restart the instruction
	@
	ldw	r2, [sp+], #S_PSR
	priv_exit r2			@ return from exception
ENDPROC(__pabt_priv)

	.align	5
.LCcralign:
	.word	cr_alignment

	.align	5
__dabt_user:
	user_entry

#ifdef CONFIG_UNICORE_FPU_F64
	cff	ip, s31
	cand.a	ip, #0x08000000		@ FPU execption traps?
	beq	209f

	ldw	ip, [sp+], #S_PC
	add	ip, ip, #4
	stw	ip, [sp+], #S_PC
	@
	@ fall through to the emulation code, which returns using r19 if
	@ it has emulated the instruction, or the more conventional lr
	@ if we are to treat this as a real extended instruction
	@
	@  r0 - instruction
	@
1:	ldw.u	r0, [r2]
	adr	r19, ret_from_exception
	adr	lr, 209f
	@
	@ fallthrough to call do_uc_f64
	@
/*
 * Check whether the instruction is a co-processor instruction.
 * If yes, we need to call the relevant co-processor handler.
 *
 * Note that we don't do a full check here for the co-processor
 * instructions; all instructions with bit 27 set are well
 * defined.  The only instructions that should fault are the
 * co-processor instructions.
 *
 * Emulators may wish to make use of the following registers:
 *  r0  = instruction opcode.
 *  r2  = PC
 *  r19 = normal "successful" return address
 *  r20 = this threads thread_info structure.
 *  lr  = unrecognised instruction return address
 */
	get_thread_info r20			@ get current thread
	and	r8, r0, #0x00003c00		@ mask out CP number
	mov	r7, #1
	stb	r7, [r20+], #TI_USED_CP + 2	@ set appropriate used_cp[]

	@ F64 hardware support entry point.
	@  r0  = faulted instruction
	@  r19 = return address
	@  r20 = fp_state
	enable_irq r4
	add	r20, r20, #TI_FPSTATE	@ r20 = workspace
	cff	r1, s31			@ get fpu FPSCR
	andn    r2, r1, #0x08000000
	ctf     r2, s31			@ clear 27 bit
	mov	r2, sp			@ nothing stacked - regdump is at TOS
	mov	lr, r19			@ setup for a return to the user code

	@ Now call the C code to package up the bounce to the support code
	@   r0 holds the trigger instruction
	@   r1 holds the FPSCR value
	@   r2 pointer to register dump
	b	ucf64_exchandler
209:
#endif
	@
	@ Call the processor-specific abort handler:
	@
	@  r2 - aborted context pc
	@  r3 - aborted context asr
	@
	@ The abort handler must return the aborted address in r0, and
	@ the fault status register in r1.
	@
	movc	r1, p0.c3, #0		@ get FSR
	movc	r0, p0.c4, #0		@ get FAR

	@
	@ INTRs on, then call the main handler
	@
	enable_irq r2
	mov	r2, sp
	adr	lr, ret_from_exception
	b	do_DataAbort
ENDPROC(__dabt_user)

	.align	5
__intr_user:
	user_entry

	get_thread_info tsk

	intr_handler

	mov	why, #0
	b	ret_to_user
ENDPROC(__intr_user)

	.ltorg

	.align	5
__extn_user:
	user_entry

	mov	r0, sp
	mov	r1, asr
	b	bad_mode
ENDPROC(__extn_user)

	.align	5
__pabt_user:
	user_entry

	mov	r0, r2			@ pass address of aborted instruction.
	mov	r1, #5
	enable_irq r1			@ Enable interrupts
	mov	r2, sp			@ regs
	b.l	do_PrefetchAbort	@ call abort handler
	/* fall through */
/*
 * This is the return code to user mode for abort handlers
 */
ENTRY(ret_from_exception)
	get_thread_info tsk
	mov	why, #0
	b	ret_to_user
ENDPROC(__pabt_user)
ENDPROC(ret_from_exception)

/*
 * Register switch for UniCore V2 processors
 * r0 = previous task_struct, r1 = previous thread_info, r2 = next thread_info
 * previous and next are guaranteed not to be the same.
 */
ENTRY(__switch_to)
	add	ip, r1, #TI_CPU_SAVE
	stm.w	(r4 - r15), [ip]+
	stm.w	(r16 - r27, sp, lr), [ip]+

#ifdef	CONFIG_UNICORE_FPU_F64
	add	ip, r1, #TI_FPSTATE
	sfm.w	(f0  - f7 ), [ip]+
	sfm.w	(f8  - f15), [ip]+
	sfm.w	(f16 - f23), [ip]+
	sfm.w	(f24 - f31), [ip]+
	cff	r4, s31
	stw	r4, [ip]

	add	ip, r2, #TI_FPSTATE
	lfm.w	(f0  - f7 ), [ip]+
	lfm.w	(f8  - f15), [ip]+
	lfm.w	(f16 - f23), [ip]+
	lfm.w	(f24 - f31), [ip]+
	ldw	r4, [ip]
	ctf	r4, s31
#endif
	add	ip, r2, #TI_CPU_SAVE
	ldm.w	(r4 - r15), [ip]+
	ldm	(r16 - r27, sp, pc), [ip]+	@ Load all regs saved previously
ENDPROC(__switch_to)

	.align	5
/*
 * This is the fast syscall return path.  We do as little as
 * possible here, and this includes saving r0 back into the PRIV
 * stack.
 */
ret_fast_syscall:
	disable_irq r1				@ disable interrupts
	ldw	r1, [tsk+], #TI_FLAGS
	cand.a	r1, #_TIF_WORK_MASK
	bne	fast_work_pending

	@ fast_restore_user_regs
	restore_user_regs fast = 1, offset = S_OFF

/*
 * Ok, we need to do extra processing, enter the slow path.
 */
fast_work_pending:
	stw.w	r0, [sp+], #S_R0+S_OFF		@ returned r0
work_pending:
	cand.a	r1, #_TIF_NEED_RESCHED
	bne	work_resched
	mov	r0, sp				@ 'regs'
	mov	r2, why				@ 'syscall'
	cand.a	r1, #_TIF_SIGPENDING		@ delivering a signal?
	cmovne	why, #0				@ prevent further restarts
	b.l	do_notify_resume
	b	ret_slow_syscall		@ Check work again

work_resched:
	b.l	schedule
/*
 * "slow" syscall return path.  "why" tells us if this was a real syscall.
 */
ENTRY(ret_to_user)
ret_slow_syscall:
	disable_irq r1				@ disable interrupts
	get_thread_info tsk			@ epip4d, one path error?!
	ldw	r1, [tsk+], #TI_FLAGS
	cand.a	r1, #_TIF_WORK_MASK
	bne	work_pending
no_work_pending:
	@ slow_restore_user_regs
	restore_user_regs fast = 0, offset = 0
ENDPROC(ret_to_user)

/*
 * This is how we return from a fork.
 */
ENTRY(ret_from_fork)
	b.l	schedule_tail
	b	ret_slow_syscall
ENDPROC(ret_from_fork)

ENTRY(ret_from_kernel_thread)
	b.l	schedule_tail
	mov	r0, r5
	adr	lr, ret_slow_syscall
	mov	pc, r4
ENDPROC(ret_from_kernel_thread)

/*=============================================================================
 * SWI handler
 *-----------------------------------------------------------------------------
 */
	.align	5
ENTRY(vector_swi)
	sub	sp, sp, #S_FRAME_SIZE
	stm	(r0 - r15), [sp]+		@ Calling r0 - r15
	add	r8, sp, #S_R16
	stm	(r16 - r28), [r8]+		@ Calling r16 - r28
	add	r8, sp, #S_PC
	stur	(sp, lr), [r8-]			@ Calling sp, lr
	mov	r8, bsr				@ called from non-REAL mode
	stw	lr, [sp+], #S_PC		@ Save calling PC
	stw	r8, [sp+], #S_PSR		@ Save ASR
	stw	r0, [sp+], #S_OLD_R0		@ Save OLD_R0
	zero_fp

	/*
	 * Get the system call number.
	 */
	sub	ip, lr, #4
	ldw.u	scno, [ip]			@ get SWI instruction

#ifdef CONFIG_ALIGNMENT_TRAP
	ldw	ip, __cr_alignment
	ldw	ip, [ip]
	movc	p0.c1, ip, #0                   @ update control register
#endif
	enable_irq ip

	get_thread_info tsk
	ldw	tbl, =sys_call_table		@ load syscall table pointer

	andn	scno, scno, #0xff000000		@ mask off SWI op-code
	andn	scno, scno, #0x00ff0000		@ mask off SWI op-code

	stm.w	(r4, r5), [sp-]			@ push fifth and sixth args
	ldw	ip, [tsk+], #TI_FLAGS		@ check for syscall tracing
	cand.a	ip, #_TIF_SYSCALL_TRACE		@ are we tracing syscalls?
	bne	__sys_trace

	csub.a	scno, #__NR_syscalls		@ check upper syscall limit
	adr	lr, ret_fast_syscall		@ return address
	bea	1f
	ldw	pc, [tbl+], scno << #2		@ call sys_* routine
1:
	add	r1, sp, #S_OFF
2:	mov	why, #0				@ no longer a real syscall
	b	sys_ni_syscall			@ not private func

	/*
	 * This is the really slow path.  We're going to be doing
	 * context switches, and waiting for our parent to respond.
	 */
__sys_trace:
	mov	r2, scno
	add	r1, sp, #S_OFF
	mov	r0, #0				@ trace entry [IP = 0]
	b.l	syscall_trace

	adr	lr, __sys_trace_return		@ return address
	mov	scno, r0			@ syscall number (possibly new)
	add	r1, sp, #S_R0 + S_OFF		@ pointer to regs
	csub.a	scno, #__NR_syscalls		@ check upper syscall limit
	bea	2b
	ldm	(r0 - r3), [r1]+		@ have to reload r0 - r3
	ldw	pc, [tbl+], scno << #2		@ call sys_* routine

__sys_trace_return:
	stw.w	r0, [sp+], #S_R0 + S_OFF	@ save returned r0
	mov	r2, scno
	mov	r1, sp
	mov	r0, #1				@ trace exit [IP = 1]
	b.l	syscall_trace
	b	ret_slow_syscall

	.align	5
#ifdef CONFIG_ALIGNMENT_TRAP
	.type	__cr_alignment, #object
__cr_alignment:
	.word	cr_alignment
#endif
	.ltorg

#ifdef CONFIG_UNICORE32_OLDABI
/*
 * Special system call wrappers
 */
ENTRY(sys_clone_wrapper)
		add	ip, sp, #S_OFF
		stw	ip, [sp+], #4
		b	sys_clone
ENDPROC(sys_clone_wrapper)

ENTRY(sys_sigreturn_wrapper)
		add	r0, sp, #S_OFF
		mov	why, #0		@ prevent syscall restart handling
		b	__sys_sigreturn
ENDPROC(sys_sigreturn_wrapper)
#endif

ENTRY(sys_rt_sigreturn)
		add	r0, sp, #S_OFF
		mov	why, #0		@ prevent syscall restart handling
		b	__sys_rt_sigreturn
ENDPROC(sys_rt_sigreturn)

	__INIT

/*
 * Vector stubs.
 *
 * This code is copied to 0xffff0200 so we can use branches in the
 * vectors, rather than ldr's.  Note that this code must not
 * exceed 0x300 bytes.
 *
 * Common stub entry macro:
 *   Enter in INTR mode, bsr = PRIV/USER ASR, lr = PRIV/USER PC
 *
 * SP points to a minimal amount of processor-private memory, the address
 * of which is copied into r0 for the mode specific abort handler.
 */
	.macro	vector_stub, name, mode
	.align	5

vector_\name:
	@
	@ Save r0, lr_<exception> (parent PC) and bsr_<exception>
	@ (parent ASR)
	@
	stw	r0, [sp]
	stw	lr, [sp+], #4		@ save r0, lr
	mov	lr, bsr
	stw	lr, [sp+], #8		@ save bsr

	@
	@ Prepare for PRIV mode.  INTRs remain disabled.
	@
	mov	r0, asr
	xor	r0, r0, #(\mode ^ PRIV_MODE)
	mov.a	bsr, r0

	@
	@ the branch table must immediately follow this code
	@
	and	lr, lr, #0x03
	add	lr, lr, #1
	mov	r0, sp
	ldw	lr, [pc+], lr << #2
	mov.a	pc, lr			@ branch to handler in PRIV mode
ENDPROC(vector_\name)
	.align	2
	@ handler addresses follow this label
	.endm

	.globl	__stubs_start
__stubs_start:
/*
 * Interrupt dispatcher
 */
	vector_stub	intr, INTR_MODE

	.long	__intr_user			@  0  (USER)
	.long	__invalid			@  1
	.long	__invalid			@  2
	.long	__intr_priv			@  3  (PRIV)

/*
 * Data abort dispatcher
 * Enter in ABT mode, bsr = USER ASR, lr = USER PC
 */
	vector_stub	dabt, ABRT_MODE

	.long	__dabt_user			@  0  (USER)
	.long	__invalid			@  1
	.long	__invalid			@  2  (INTR)
	.long	__dabt_priv			@  3  (PRIV)

/*
 * Prefetch abort dispatcher
 * Enter in ABT mode, bsr = USER ASR, lr = USER PC
 */
	vector_stub	pabt, ABRT_MODE

	.long	__pabt_user			@  0 (USER)
	.long	__invalid			@  1
	.long	__invalid			@  2 (INTR)
	.long	__pabt_priv			@  3 (PRIV)

/*
 * Undef instr entry dispatcher
 * Enter in EXTN mode, bsr = PRIV/USER ASR, lr = PRIV/USER PC
 */
	vector_stub	extn, EXTN_MODE

	.long	__extn_user			@  0 (USER)
	.long	__invalid			@  1
	.long	__invalid			@  2 (INTR)
	.long	__extn_priv			@  3 (PRIV)

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
	jepriv	SYS_ERROR0
	b	vector_extn + stubs_offset
	ldw	pc, .LCvswi + stubs_offset
	b	vector_pabt + stubs_offset
	b	vector_dabt + stubs_offset
	jepriv	SYS_ERROR0
	b	vector_intr + stubs_offset
	jepriv	SYS_ERROR0

	.globl	__vectors_end
__vectors_end:

	.data

	.globl	cr_alignment
	.globl	cr_no_alignment
cr_alignment:
	.space	4
cr_no_alignment:
	.space	4
